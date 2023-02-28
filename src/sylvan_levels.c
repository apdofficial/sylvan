#include "sylvan_int.h"
#include "sylvan_levels.h"
#include "sylvan_mtbdd_int.h"

/// Handling of variable levels
static uint32_t *var_to_level = NULL;  // get the level of a "real variable"
static uint32_t *level_to_var = NULL;  // get the "real variable" of a level
static MTBDD *levels = NULL;           // array holding the 1-node BDD for each level

static size_t levels_count = 0;        // number of created levels
static size_t levels_size = 0;         // size of the 3 arrays


void
mtbdd_levels_new(size_t amount)
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


void
mtbdd_levels_reset(void)
{
    levels_count = 0;
}

size_t
mtbdd_levels_size(void)
{
    return levels_count;
}


MTBDD
mtbdd_levels_ithlevel(uint32_t level)
{
    if (level < levels_count) {
        return levels[level_to_var[level]];
    } else {
        return mtbdd_invalid;
    }
}


uint32_t mtbdd_levels_var_to_level(uint32_t var)
{
    if (var < levels_count){
        return var_to_level[var];
    } else {
        return var;
    }
}


uint32_t
mtbdd_levels_level_to_var(uint32_t level)
{
    if (level < levels_count){
        return level_to_var[level];
    } else {
        return level;
    }
}


uint32_t
mtbdd_levels_node_to_level(MTBDD node)
{
    uint32_t var  = mtbdd_getvar(node);
    return mtbdd_levels_var_to_level(var);
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
mtbdd_levels_gc_add_mark_managed_refs(void)
{
    sylvan_gc_add_mark(TASK(mtbdd_gc_mark_managed_refs));
}

void
mtbdd_levels_varswap(uint32_t var)
{
#if 0
    // the variable itself is incremented but that does not
    // necessarily mean that we get a successor variable
    level_to_var[var_to_level[var]] = var+1;
    level_to_var[var_to_level[var+1]] = var;
    uint32_t save = var_to_level[var];
    var_to_level[var] = var_to_level[var+1];
    var_to_level[var+1] = save;
#else
    uint32_t curr_level = var_to_level[var];
    uint32_t curr_var = var;

    uint32_t next_level = var_to_level[var]+1;
    uint32_t next_var = level_to_var[next_level];

    level_to_var[curr_level] = next_var;
    level_to_var[next_level] = curr_var;

    var_to_level[curr_var] = next_level;
    var_to_level[next_var] = curr_level;
#endif
}

void
sylvan_levels_destroy(void)
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


VOID_TASK_IMPL_3(mtbdd_levels_count_nodes, size_t*, arr, size_t, first, size_t, count)
{
    if (count > COUNT_NODES_BLOCK_SIZE) {
        SPAWN(mtbdd_levels_count_nodes, arr, first, count / 2);
        CALL(mtbdd_levels_count_nodes, arr, first + count / 2, count - count / 2);
        SYNC(mtbdd_levels_count_nodes);
    } else {
        size_t tmp[mtbdd_levels_size()], i;
        for (i=0; i < mtbdd_levels_size(); i++) tmp[i] = 0;

        const size_t end = first + count;

        for (; first < end; first++) {
            if (!llmsset_is_marked(nodes, first)) continue; // unused bucket
            mtbddnode_t node = MTBDD_GETNODE(first);
            if (mtbddnode_isleaf(node)) continue; // a leaf
            tmp[mtbddnode_getvariable(node)]++; // update the variable
        }
        /* these are atomic operations on a hot location with false sharing inside another
           thread's program stack... can't get much worse! */
        for (i=0; i < mtbdd_levels_size(); i++) __sync_add_and_fetch(&arr[i], tmp[i]);
    }
}

