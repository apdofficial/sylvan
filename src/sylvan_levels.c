#include "sylvan_int.h"
#include "sylvan_levels.h"
#include "sylvan_mtbdd_int.h"

/// Handling of variable levels
static uint32_t *var_to_level = NULL;  // get the level of a "real variable"
static uint32_t *level_to_var = NULL;  // get the "real variable" of a level
static MTBDD *levels = NULL;           // array holding the 1-node BDD for each level

static size_t levels_count = 0;        // number of created levels
static size_t levels_size = 0;         // size of the 3 arrays

void mtbdd_levels_new(size_t amount)
{
    if (levels_count + amount >= levels_size) {
#if 0
        if (levels_size == 0) levels_size = 1; // start here
        while (levels_count + amount >= levels_size) levels_size *= 2;
#else
        // just round up to the next multiple of 64 value
        // probably better than doubling anyhow...
        levels_size = (levels_count + amount + 63) & (~63LL);
#endif
        levels = realloc(levels, sizeof(MTBDD[levels_size]));
        var_to_level = realloc(var_to_level, sizeof(uint32_t[levels_size]));
        level_to_var = realloc(level_to_var, sizeof(uint32_t[levels_size]));
    }
    for (size_t i = 0; i < amount; i++) {
        // reminder: makenode(var, low, high)
        levels[levels_count] = mtbdd_makenode(levels_count, mtbdd_false, mtbdd_true);
        var_to_level[levels_count] = levels_count;
        level_to_var[levels_count] = levels_count;
        levels_count++;
    }
}

void mtbdd_levels_reset(void)
{
    levels_count = 0;
}

size_t mtbdd_levels_size(void)
{
    return levels_count;
}

MTBDD mtbdd_ithlevel(uint32_t level)
{
    if (level < levels_count) {
        return levels[level_to_var[level]];
    } else {
        return mtbdd_invalid;
    }
}

uint32_t mtbdd_var_to_level(uint32_t var)
{
    if (var < levels_count) {
        return var_to_level[var];
    } else {
        return var;
    }
}

uint32_t mtbdd_level_to_var(uint32_t level)
{
    if (level < levels_count) {
        return level_to_var[level];
    } else {
        return level;
    }
}

uint32_t mtbdd_node_to_level(MTBDD node)
{
    return mtbdd_var_to_level(mtbdd_getvar(node));
}

/**
 * This function is called during garbage collection and
 * marks all managed level BDDs so they are kept.
 */
VOID_TASK_0(mtbdd_gc_mark_managed_refs)
{
    for (size_t i = 0; i < levels_count; i++) {
        llmsset_mark(nodes, MTBDD_STRIPMARK(levels[i]));
    }
}

void mtbdd_levels_gc_add_mark_managed_refs(void)
{
    sylvan_gc_add_mark(TASK(mtbdd_gc_mark_managed_refs));
}

void mtbdd_levels_varswap(uint32_t var)
{
    level_to_var[var_to_level[var]] = var + 1;
    level_to_var[var_to_level[var + 1]] = var;
    uint32_t save = var_to_level[var];
    var_to_level[var] = var_to_level[var + 1];
    var_to_level[var + 1] = save;
}


void sylvan_levels_destroy(void)
{
    if (levels_size != 0) {
        free(levels);
        levels = NULL;
        free(var_to_level);
        var_to_level = NULL;
        free(level_to_var);
        level_to_var = NULL;
        levels_count = 0;
        levels_size = 0;
    }
}

/**
 * Sort level counts using gnome sort.
 * @param level
 * @param level_counts
 */
static inline void sort_level_counts(int *levels, const size_t *level_counts)
{
    unsigned int i = 1;
    unsigned int j = 2;
    while (i < mtbdd_levels_size()) {
        long p = levels[i - 1] == -1 ? -1 : (long) level_counts[mtbdd_level_to_var(levels[i - 1])];
        long q = levels[i] == -1 ? -1 : (long) level_counts[mtbdd_level_to_var(levels[i])];
        if (p < q) {
            int t = levels[i];
            levels[i] = levels[i - 1];
            levels[i - 1] = t;
            if (--i) continue;
        }
        i = j++;
    }
}


VOID_TASK_IMPL_3(mtbdd_count_levels, size_t*, arr, size_t, first, size_t, count)
{
    // Divide-and-conquer if count above COUNT_NODES_BLOCK_SIZE
    if (count > COUNT_NODES_BLOCK_SIZE) {
        SPAWN(mtbdd_count_levels, arr, first, count / 2);
        CALL(mtbdd_count_levels, arr, first + count / 2, count - count / 2);
        SYNC(mtbdd_count_levels);
    } else {
        size_t tmp[mtbdd_levels_size()], i;
        for (i = 0; i < mtbdd_levels_size(); i++) tmp[i] = 0;

        const size_t end = first + count;

        for (; first < end; first++) {
            if (!llmsset_is_marked(nodes, first)) continue; // unused bucket
            mtbddnode_t node = MTBDD_GETNODE(first);
            if (mtbddnode_isleaf(node)) continue; // a leaf
            tmp[mtbddnode_getvariable(node)]++; // update the variable
        }
        /* these are atomic operations on a hot location with false sharing inside another
           thread's program stack... can't get much worse! */
        for (i = 0; i < mtbdd_levels_size(); i++) __sync_add_and_fetch(&arr[i], tmp[i]);
    }
}

VOID_TASK_IMPL_2(mtbdd_count_sort_levels, int*, levels, size_t, threshold)
{
    size_t level_counts[mtbdd_levels_size()];
    for (size_t i = 0; i < mtbdd_levels_size(); i++) level_counts[i] = 0;

    mtbdd_count_levels(level_counts);

    // set levels below the threshold to -1
    for (int i = 0; i < (int) mtbdd_levels_size(); i++) {
        if (level_counts[mtbdd_level_to_var(i)] < threshold) {
            levels[i] = -1;
        } else {
            levels[i] = i;
        }
    }
    sort_level_counts(levels, level_counts);
}


