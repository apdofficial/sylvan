#include <sylvan_int.h>
#include <sylvan_align.h>
#include <errno.h>      // for errno

static size_t levels_size = 0; // size of the arrays in levels_t used to realloc memory

size_t levels_count_get(levels_t* self)
{
    return self->count;
}

uint64_t levels_new_one(levels_t* self)
{
    levels_new_many(self, 1);
    return self->table[levels_count_get(self) - 1];
}

int levels_new_many(levels_t* self, size_t amount)
{
    if (self->count + amount >= levels_size) {
        // just round up to the next multiple of 64 value
        // probably better than doubling anyhow...
        levels_size = (self->count + amount + 63) & (~63LL);
        self->table = (_Atomic (uint64_t) *) realloc(self->table, sizeof(uint64_t[levels_size]));
        self->level_to_order = (_Atomic (uint32_t) *) realloc(self->level_to_order, sizeof(uint32_t[levels_size]));
        self->order_to_level = (_Atomic (uint32_t) *) realloc(self->order_to_level, sizeof(uint32_t[levels_size]));

        if (self->table == NULL || self->level_to_order == NULL || self->order_to_level == NULL) {
            fprintf(stderr, "levels_new_many failed to realloc new memory: %s!\n", strerror(errno));
            exit(1);
        }
    }
    for (size_t i = 0; i < amount; i++) {
        (void)self->count;
        self->table[self->count] = mtbdd_makenode(self->count, mtbdd_false, mtbdd_true);
        self->level_to_order[self->count] = self->count;
        self->order_to_level[self->count] = self->count;
        self->count++;
    }
    return 1;
}

int levels_new_node(levels_t* self, uint32_t level, uint64_t low, uint64_t high)
{
    if (level >= self->count) {
        fprintf(stderr, "mtbdd_levels_makenode failed. Out of level bounds.");
        return 0;
    }

    BDDVAR order = self->level_to_order[level];
    self->table[order] = mtbdd_makenode(order, low, high);

    return 1;
}

void levels_reset(levels_t* self)
{
    if (levels_size != 0) {
        if (!self->table) free(self->table);
        self->table = NULL;

        if (!self->level_to_order) free(self->level_to_order);
        self->level_to_order = NULL;

        if (!self->order_to_level) free(self->order_to_level);
        self->order_to_level = NULL;

        self->count = 0;
        levels_size = 0;
    }
}

uint64_t levels_ithlevel(levels_t* self, uint32_t level)
{
    if (level < self->count) {
        return self->table[self->level_to_order[level]];
    } else {
        size_t amount = level - self->count + 1;
        levels_new_many(self, amount);
        return self->table[self->level_to_order[level]];
    }
}

uint32_t levels_order_to_level(levels_t *self, uint32_t var)
{
    if (var < self->count) return self->order_to_level[var];
    else return var;
}

uint32_t levels_level_to_order(levels_t *self, uint32_t level)
{
    if (level < self->count) return self->level_to_order[level];
    else return level;
}

/**
 * This function is called during garbage collection and
 * marks all managed level BDDs so they are kept.
 */
VOID_TASK_0(mtbdd_gc_mark_managed_refs)
{
    for (size_t i = 0; i < reorder_db->levels.count; i++) {
        llmsset_mark(nodes, MTBDD_STRIPMARK(reorder_db->levels.table[i]));
    }
}

void levels_gc_add_mark_managed_refs(void)
{
    sylvan_gc_add_mark(TASK(mtbdd_gc_mark_managed_refs));
}

/**
 * Sort level counts using gnome sort.
 */
void levels_gnome_sort(levels_t *self, int *levels_arr, const _Atomic (size_t) *level_counts)
{
    unsigned int i = 1;
    unsigned int j = 2;
    while (i < self->count) {
        long p = levels_arr[i - 1] == -1 ? -1 : (long) level_counts[self->level_to_order[levels_arr[i - 1]]];
        long q = levels_arr[i] == -1 ? -1 : (long) level_counts[self->level_to_order[levels_arr[i]]];
        if (p < q) {
            int t = levels_arr[i];
            levels_arr[i] = levels_arr[i - 1];
            levels_arr[i - 1] = t;
            if (--i) continue;
        }
        i = j++;
    }
}

// set levels below the threshold to -1
void levels_mark_threshold(levels_t *self, int *level, const _Atomic (size_t) *level_counts, uint32_t threshold)
{
    for (unsigned int i = 0; i < self->count; i++) {
        if (level_counts[self->level_to_order[i]] < threshold) level[i] = -1;
        else level[i] = i;
    }
}