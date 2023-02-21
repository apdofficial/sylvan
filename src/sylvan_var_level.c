#include "sylvan_int.h"
#include "sylvan_var_level.h"
#include "sylvan_mtbdd_int.h"


//// Handling of variable levels
static uint32_t *var_to_level = NULL;  // get the level of a "real variable"
static uint32_t *level_to_var = NULL;  // get the "real variable" of a level
static MTBDD *levels = NULL;           // array holding the 1-node BDD for each level
static size_t levels_count = 0;        // number of created levels
static size_t levels_size = 0;         // size of the 3 arrays


/**
 * Create the next level and return the BDD representing the variable (ithlevel)
 * The BDDs representing managed levels are always kept during garbage collection.
 * NOTE: not currently thread-safe.
 */
MTBDD
mtbdd_newlevel(void)
{
    mtbdd_newlevels(1);
    return levels[levels_count-1];
}


/**
 * Create the next <amount> levels
 * The BDDs representing managed levels are always kept during garbage collection.
 * NOTE: not currently thread-safe.
 */
void
mtbdd_newlevels(size_t amount)
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
    for (size_t i=0; i<amount; i++) {
        // reminder: makenode(var, low, high)
        levels[levels_count] = mtbdd_makenode(levels_count, mtbdd_false, mtbdd_true);
        var_to_level[levels_count] = levels_count;
        level_to_var[levels_count] = levels_count;
        levels_count++;
    }
}

/**
 * Reset all levels.
 */
void
mtbdd_levels_reset(void)
{
    levels_count = 0;
}

/**
 * Create or get the BDD representing "if <level> then true else false"
 * Returns mtbdd_invalid for unmanaged levels.
 */
MTBDD
mtbdd_ithlevel(uint32_t level)
{
    if (level < levels_count) return levels[level_to_var[level]];
    else return mtbdd_invalid;
}

/**
 * Get the current level of the given internal variable <var>
 */
uint32_t mtbdd_var_to_level(uint32_t var)
{
    if (var < levels_count) return var_to_level[var];
    else return var;
}

/**
 * Get the current internal variable of the given level <level>
 */
uint32_t
mtbdd_level_to_var(uint32_t level)
{
    if (level < levels_count) return level_to_var[level];
    else return level;
}

/**
 * Return the level of the given internal node.
 */
uint32_t
mtbdd_getlevel(MTBDD node)
{
    return mtbdd_var_to_level(mtbdd_getvar(node));
}

/**
 * This function is called during garbage collection and
 * marks all managed level BDDs so they are kept.
 */
VOID_TASK_0(mtbdd_gc_mark_managed_refs)
{
    for (size_t i=0; i<levels_count; i++) {
        llmsset_mark(nodes, MTBDD_STRIPMARK(levels[i]));
    }
}

void
sylvan_gc_add_mark_managed_refs(void)
{
    sylvan_gc_add_mark(TASK(mtbdd_gc_mark_managed_refs));
}

void
sylvan_var_level_update(uint32_t var)
{
    level_to_var[var_to_level[var]] = var+1;
    level_to_var[var_to_level[var+1]] = var;
    uint32_t save = var_to_level[var];
    var_to_level[var] = var_to_level[var+1];
    var_to_level[var+1] = save;
}

size_t
sylvan_get_levels_count(void)
{
    return levels_count;
}

size_t
sylvan_get_real_var(uint32_t level)
{
    return level_to_var[level];
}

void
sylvan_levels_destroy(void)
{
     if (levels_size != 0) {
        free(levels);
        free(var_to_level);
        free(level_to_var);
        levels_count = 0;
        levels_size = 0;
    }
}

/**
 * Count the number of nodes per real variable level.
 *
 * Results are stored atomically in arr.
 *
 * To make this somewhat scalable, we use a standard binary reduction pattern with local arrays...
 * Fortunately, we only do this once per call to dynamic variable reordering.
 */
VOID_TASK_IMPL_3(sylvan_count_nodes, size_t*, arr, size_t, first, size_t, count)
{
    if (count > 4096) {
        /* 4096, because that is not very small, and not very large */
        /* typical kind of parameter that is open to tweaking, though I don't expect it matters so much */
        /* too small is bad for the atomic operations, too large is bad for work-stealing */
        /* with 2^20 - 2^25 nodes table size, this is 256 - 8192 tasks */
        SPAWN(sylvan_count_nodes, arr, first, count/2);
        CALL(sylvan_count_nodes, arr, first+count/2, count-count/2);
        SYNC(sylvan_count_nodes);
    } else {
        size_t tmp[sylvan_get_levels_count()], i;
        for (i=0; i<sylvan_get_levels_count(); i++) tmp[i] = 0;

        const size_t end = first + count;

        for (; first < end; first++) {
            if (!llmsset_is_marked(nodes, first)) continue; // unused bucket
            mtbddnode_t node = MTBDD_GETNODE(first);
            if (mtbddnode_isleaf(node)) continue; // a leaf
            tmp[mtbddnode_getvariable(node)]++; // update the variable
        }

        /* these are atomic operations on a hot location with false sharing inside another
           thread's program stack... can't get much worse! */
        for (i=0; i<sylvan_get_levels_count(); i++) __sync_add_and_fetch(&arr[i], tmp[i]);
    }
}