#include "sylvan_int.h"
#include "sylvan_levels.h"
#include "sylvan_mtbdd_int.h"

/// Handling of variable levels
static LEVEL *var_to_level = NULL;   // get the level of a "real variable"
static BDDVAR *level_to_var = NULL;   // get the "real variable" of a level
static MTBDD *levels = NULL;            // array holding the 1-node BDD for each level
static int *order_locks = NULL;         // array holding orderlock marks, 1 means locked, 0 otherwise
static size_t levels_count = 0;         // number of created levels
static size_t levels_size = 0;          // size of the 3 arrays

MTBDD mtbdd_newlevel(void)
{
    mtbdd_newlevels(1);
    return levels[levels_count - 1];
}

int mtbdd_newlevels(size_t amount)
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
        order_locks = realloc(order_locks, sizeof(int[levels_size]));
        var_to_level = realloc(var_to_level, sizeof(LEVEL[levels_size]));
        level_to_var = realloc(level_to_var, sizeof(BDDVAR[levels_size]));

        if (!(levels && order_locks && var_to_level && level_to_var)) {
            fprintf(stderr, "mtbdd_newlevels failed to allocate new memory!");
            return 0;
        }
    }
    for (size_t i = 0; i < amount; i++) {
        // reminder: makenode(var, low, high)
        levels[levels_count] = mtbdd_makenode(levels_count, mtbdd_false, mtbdd_true);
        var_to_level[levels_count] = levels_count;
        level_to_var[levels_count] = levels_count;
        levels_count++;
        order_locks[levels_count] = 0;
    }
    return 1;
}

int mtbdd_getorderlock(LEVEL level)
{
    assert((int) level >= 0 && level < mtbdd_levelscount()); // bound check
    return order_locks[level];
}

void mtbdd_setorderlock(LEVEL level, int is_locked)
{
    assert((int) level >= 0 && level < mtbdd_levelscount()); // bound check
    assert(is_locked == 0 || is_locked == 1); // orderlock check
    order_locks[level] = is_locked;
}

LEVEL mtbdd_getlevel(MTBDD node)
{
    return mtbdd_var_to_level(mtbdd_getvar(node));
}

void mtbdd_resetlevels(void)
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
        free(order_locks);
        order_locks = NULL;
    }
}

size_t mtbdd_levelscount(void)
{
    return levels_count;
}

MTBDD mtbdd_ithlevel(LEVEL level)
{
    if (level < levels_count) return levels[level_to_var[level]];
    else return mtbdd_invalid;
}

LEVEL mtbdd_var_to_level(BDDVAR var)
{
    if (var < levels_count) return var_to_level[var];
    else return var;
}

BDDVAR mtbdd_level_to_var(LEVEL level)
{
    if (level < levels_count) return level_to_var[level];
    else return level;
}

LEVEL mtbdd_node_to_level(MTBDD node)
{
    return mtbdd_var_to_level(mtbdd_getvar(node));
}

BDDVAR mtbdd_nextlow(BDDVAR var)
{
    uint32_t level = mtbdd_var_to_level(var);
    return level <= 0 ? var : mtbdd_level_to_var(level - 1);
}

BDDVAR mtbdd_nexthigh(BDDVAR var)
{
    uint32_t level = mtbdd_var_to_level(var);
    return level >= mtbdd_levelscount() - 1 ? var : mtbdd_level_to_var(level + 1);
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

void mtbdd_varswap(BDDVAR var)
{
    level_to_var[var_to_level[var]] = var + 1;
    level_to_var[var_to_level[var + 1]] = var;
    uint32_t save = var_to_level[var];
    var_to_level[var] = var_to_level[var + 1];
    var_to_level[var + 1] = save;
}

void mtbdd_varswap_adj(BDDVAR x, BDDVAR y)
{
    level_to_var[var_to_level[x]] = y;
    level_to_var[var_to_level[y]] = x;
    uint32_t save = var_to_level[x];
    var_to_level[x] = var_to_level[y];
    var_to_level[y] = save;
}



/**
 * Sort level counts using gnome sort.
 * @param level
 * @param level_counts
 */
static inline void gnome_sort(int *levels, const int *level_counts)
{
    unsigned int i = 1;
    unsigned int j = 2;
    while (i < mtbdd_levelscount()) {
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

VOID_TASK_IMPL_3(mtbdd_countlevels, int*, arr, uint64_t, first, uint64_t, count)
{
    // Divide-and-conquer if count above COUNT_NODES_BLOCK_SIZE
    if (count > COUNT_NODES_BLOCK_SIZE) {
        SPAWN(mtbdd_countlevels, arr, first, count / 2);
        CALL(mtbdd_countlevels, arr, first + count / 2, count - count / 2);
        SYNC(mtbdd_countlevels);
    } else {
        int tmp[mtbdd_levelscount()];
        size_t i;
        for (i = 0; i < mtbdd_levelscount(); i++) tmp[i] = 0;

        const size_t end = first + count;

        for (; first < end; first++) {
            if (!llmsset_is_marked(nodes, first)) continue; // unused bucket
            mtbddnode_t node = MTBDD_GETNODE(first);
            if (mtbddnode_isleaf(node)) continue; // a leaf
            tmp[mtbddnode_getvariable(node)]++; // update the variable
        }
        /* these are atomic operations on a hot location with false sharing inside another
           thread's program stack... can't get much worse! */
        for (i = 0; i < mtbdd_levelscount(); i++) __sync_add_and_fetch(&arr[i], tmp[i]);
    }
}

VOID_TASK_IMPL_2(mtbdd_count_sort_levels, int*, levels_arr, uint64_t, threshold)
{
    int level_counts[mtbdd_levelscount()];
    for (size_t i = 0; i < mtbdd_levelscount(); i++) level_counts[i] = 0;

    mtbdd_countlevels(level_counts);

    // set levels below the threshold to -1
    for (int i = 0; i < (int) mtbdd_levelscount(); i++) {
        if (level_counts[mtbdd_level_to_var(i)] < (int)threshold) levels_arr[i] = -1;
        else levels_arr[i] = i;
    }

    gnome_sort(levels_arr, level_counts);
}