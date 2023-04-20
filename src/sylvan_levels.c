#include <sylvan_int.h>
#include <sylvan_mtbdd_int.h>
#include <sylvan_align.h>
#include <errno.h>      // for errno

static size_t levels_size; // size of the arrays in levels_t used to realloc memory

levels_t mtbdd_levels_create()
{
    levels_t dbs = (struct levels_db *) alloc_aligned(sizeof(struct levels_db));
    if (dbs == 0) {
        fprintf(stderr, "mtbdd_levels_create: Unable to allocate memory: %s!\n", strerror(errno));
        exit(1);
    }
    dbs->table = NULL;
    dbs->level_to_order = NULL;
    dbs->order_to_level = NULL;

    // one data bucket is uint64_t, 8 bytes
    // bitmap_visited_size: 1 bit per bucket results in nodes->max_size / 8
    dbs->bitmap1_size = nodes->max_size / 8;
    dbs->bitmap1 = (_Atomic (uint64_t) *) alloc_aligned(dbs->bitmap1_size);

    if (dbs->bitmap1 == 0) {
        fprintf(stderr, "mtbdd_levels_create: Unable to allocate memory: %s!\n", strerror(errno));
        exit(1);
    }

    levels_size = 0;
    dbs->count = 0;

    return dbs;
}

void mtbdd_levels_free(levels_t dbs)
{
    mtbdd_resetlevels();
    free(dbs);
}

size_t mtbdd_levelscount(void)
{
    return levels->count;
}

MTBDD mtbdd_newlevel(void)
{
    mtbdd_newlevels(1);
    return levels->table[levels->count - 1];
}

int mtbdd_newlevels(size_t amount)
{
    if (levels->count + amount >= levels_size) {
#if 0
        if (levels_size == 0) levels_size = 1; // start here
        while (levels_count + amount >= levels_size) levels_size *= 2;
#else
        // just round up to the next multiple of 64 value
        // probably better than doubling anyhow...
        levels_size = (levels->count + amount + 63) & (~63LL);
#endif

        levels->table = (_Atomic (uint32_t) *) realloc(levels->table, sizeof(uint64_t[levels_size]));
        levels->level_to_order = (_Atomic (uint32_t) *) realloc(levels->level_to_order, sizeof(uint32_t[levels_size]));
        levels->order_to_level = (_Atomic (uint32_t) *) realloc(levels->order_to_level, sizeof(uint32_t[levels_size]));

        if (levels->table == 0 || levels->level_to_order == 0 || levels->order_to_level == 0) {
            fprintf(stderr, "mtbdd_newlevels failed to allocate new memory!");
            return 0;
        }
    }
    for (size_t i = 0; i < amount; i++) {
        levels->table[levels->count] = mtbdd_makenode(levels->count, mtbdd_false, mtbdd_true);
        levels->level_to_order[levels->count] = levels->count;
        levels->order_to_level[levels->count] = levels->count;
        levels->count++;
    }
    return 1;
}

void mtbdd_resetlevels(void)
{
    if (levels_size != 0) {
        free(levels->table);
        levels->table = NULL;
        free(levels->level_to_order);
        levels->level_to_order = NULL;
        free(levels->order_to_level);
        levels->order_to_level = NULL;
        levels->count = 0;
        levels_size = 0;
    }
}

MTBDD mtbdd_ithlevel(uint32_t level)
{
    if (level < levels->count) return levels->table[levels->level_to_order[level]];
    else return mtbdd_invalid;
}

uint32_t mtbdd_var_to_level(BDDVAR var)
{
    if (var < levels->count) return levels->order_to_level[var];
    else return var;
}

BDDVAR mtbdd_level_to_var(uint32_t level)
{
    if (level < levels->count) return levels->level_to_order[level];
    else return level;
}

/**
 * This function is called during garbage collection and
 * marks all managed level BDDs so they are kept.
 */
VOID_TASK_0(mtbdd_gc_mark_managed_refs)
{
    for (size_t i = 0; i < levels->count; i++) {
        llmsset_mark(nodes, MTBDD_STRIPMARK(levels->table[i]));
    }
}

void mtbdd_levels_gc_add_mark_managed_refs(void)
{
    sylvan_gc_add_mark(TASK(mtbdd_gc_mark_managed_refs));
}

/**
 * Sort level counts using gnome sort.
 * @param level
 * @param level_counts
 */
void gnome_sort(int *levels_arr, const _Atomic (size_t) *level_counts)
{
    unsigned int i = 1;
    unsigned int j = 2;
    while (i < levels->count) {
        long p = levels_arr[i - 1] == -1 ? -1 : (long) level_counts[mtbdd_level_to_var(levels_arr[i - 1])];
        long q = levels_arr[i] == -1 ? -1 : (long) level_counts[mtbdd_level_to_var(levels_arr[i])];
        if (p < q) {
            int t = levels_arr[i];
            levels_arr[i] = levels_arr[i - 1];
            levels_arr[i - 1] = t;
            if (--i) continue;
        }
        i = j++;
    }
}

VOID_TASK_IMPL_3(sylvan_count_levelnodes, _Atomic (size_t)*, arr, size_t, first, size_t, count)
{
    if (count > COUNT_NODES_BLOCK_SIZE) {
        SPAWN(sylvan_count_levelnodes, arr, first, count / 2);
        CALL(sylvan_count_levelnodes, arr, first + count / 2, count - count / 2);
        SYNC(sylvan_count_levelnodes);
    } else {
        size_t tmp[levels->count];
        size_t i;
        for (i = 0; i < levels->count; i++) tmp[i] = 0;

        const size_t end = first + count;

        for (; first < end; first++) {
            if (!llmsset_is_marked(nodes, first)) continue; // unused bucket
            mtbddnode_t node = MTBDD_GETNODE(first);
            if (mtbddnode_isleaf(node)) continue; // a leaf
            tmp[mtbddnode_getvariable(node)]++; // update the variable
        }
        for (i = 0; i < levels->count; i++) {
            atomic_fetch_add(&arr[i], tmp[i]);
        }
    }
}

TASK_IMPL_3(size_t, sylvan_count_nodes, BDDVAR, var, size_t, first, size_t, count)
{
    if (count > COUNT_NODES_BLOCK_SIZE) {
        size_t split = count / 2;
        SPAWN(sylvan_count_nodes, var, first, split);
        size_t a = CALL(sylvan_count_nodes, var, first + split, count - split);
        size_t b = SYNC(sylvan_count_nodes);
        return a + b;
    } else {
        size_t var_count = 0;
        const size_t end = first + count;
        for (; first < end; first++) {
            if (!llmsset_is_marked(nodes, first)) continue; // unused bucket
            mtbddnode_t node = MTBDD_GETNODE(first);
            if (mtbddnode_getvariable(node) != var) continue; // not the right variable
            var_count++;
        }
        return var_count;
    }
}

// set levels below the threshold to -1
void mtbdd_mark_threshold(int *level, const _Atomic (size_t) *level_counts, uint32_t threshold)
{
    for (unsigned int i = 0; i < levels->count; i++) {
        if (level_counts[mtbdd_level_to_var(i)] < threshold) level[i] = -1;
        else level[i] = i;
    }
}

VOID_TASK_IMPL_3(sylvan_init_subtables, char**, subtables, size_t, first, size_t, count)
{
    if (count > COUNT_NODES_BLOCK_SIZE) {
        size_t split = count / 2;
        SPAWN(sylvan_init_subtables, subtables, first, split);
        CALL(sylvan_init_subtables, subtables, first + split, count - split);
        SYNC(sylvan_init_subtables);
    } else {
        const size_t end = first + count;
        for (; first < end; first++) {
            if (!llmsset_is_marked(nodes, first)) continue; // unused bucket
            mtbddnode_t node = MTBDD_GETNODE(first);
            BDDVAR var = mtbddnode_getvariable(node);
            if (var < levels->count) subtables[var][first] = 1;
        }
    }
}