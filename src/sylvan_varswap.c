#include <sylvan_int.h>
#include "sylvan_varswap.h"
#include "sylvan_levels.h"
#include "sylvan_align.h"
#include "sylvan_interact.h"

reorder_result_t swap_node(size_t index);

reorder_result_t swap_mapnode(size_t index);

int is_node_dependent_on(mtbddnode_t node, BDDVAR var);

#if !SYLVAN_USE_LINEAR_PROBING
/*!
   \brief Adjacent variable swap phase 0 (Chaining compatible)
   \details Clear hashes of nodes with var and var+1, Removes exactly the nodes
   that will be changed from the hash table.
*/
VOID_TASK_DECL_4(sylvan_varswap_p0, uint32_t, uint64_t, uint64_t, _Atomic (reorder_result_t) *)

#endif

/*!
   @brief Adjacent variable swap phase 2
   @details Handle all trivial cases where no node is created, mark cases that are not trivial.
   @return number of nodes that were marked
*/
TASK_DECL_4(size_t, sylvan_varswap_p1, uint32_t, size_t, size_t, _Atomic (reorder_result_t) *)

/*!
   @brief Adjacent variable swap phase 2
   @details Handle the not so trivial cases. (creates new nodes)
*/
VOID_TASK_DECL_4(sylvan_varswap_p2, uint32_t, size_t, size_t, _Atomic (reorder_result_t) *)

/*!
   \brief Adjacent variable swap phase 3
   \details Recovery phase, restore the nodes that were marked in phase 1.
*/
VOID_TASK_DECL_2(sylvan_varswap_p3, uint32_t, _Atomic (reorder_result_t) *)

TASK_4(int, llmsset_rehash_swap, llmsset_t, dbs, int, var, size_t, first, size_t, count)
{
    if (count > 512) {
        size_t split = count / 2;
        SPAWN(llmsset_rehash_swap, dbs, var, first, split);
        int bad = CALL(llmsset_rehash_swap, dbs, var, first + split, count - split);
        return bad + SYNC(llmsset_rehash_swap);
    }

    int bad = 0;
    const size_t end = first + count;

    for (first = llmsset_next(first - 1); first < end; first = llmsset_next(first)) {
        if (var < 0) {
            if (llmsset_rehash_bucket(nodes, first) == 0) bad++;
        } else {
            mtbddnode_t node = MTBDD_GETNODE(first);
            uint32_t nvar = mtbddnode_getvariable(node);
            if (nvar == (uint32_t) var || nvar == ((uint32_t) var + 1)) {
                if (llmsset_rehash_bucket(nodes, first) == 0) bad++;
            }
        }
    }

    return bad;
}

VOID_TASK_1(sylvan_varswap_rehash, uint32_t, var)
{
    (void) var;
    CALL(sylvan_rehash_all);
    // clear hash array
//    llmsset_clear_hashes(nodes);
    // rehash marked nodes
//    CALL(llmsset_rehash_swap, nodes, var, 0, nodes->table_size);
}

void sylvan_reorder_resdescription(reorder_result_t result, char *buf, size_t buf_len)
{
    (void) buf_len;
    assert(buf_len >= 100);
    switch (result) {
        case SYLVAN_REORDER_ROLLBACK:
            sprintf(buf, "SYLVAN_REORDER: the operation was aborted and rolled back (%d)", result);
            break;
        case SYLVAN_REORDER_SUCCESS:
            sprintf(buf, "SYLVAN_REORDER: success (%d)", result);
            break;
        case SYLVAN_REORDER_P0_CLEAR_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash in phase 0, no marked nodes remaining (%d)", result);
            break;
        case SYLVAN_REORDER_P1_REHASH_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash in phase 1, no marked nodes remaining (%d)", result);
            break;
        case SYLVAN_REORDER_P1_REHASH_FAIL_MARKED:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash in phase 1, and marked nodes remaining (%d)", result);
            break;
        case SYLVAN_REORDER_P2_REHASH_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash in phase 2, no marked nodes remaining (%d)", result);
            break;
        case SYLVAN_REORDER_P2_CREATE_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot create node in phase 2 (ergo marked nodes remaining) (%d)", result);
            break;
        case SYLVAN_REORDER_P2_REHASH_AND_CREATE_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash and cannot create node in phase 2 (%d)", result);
            break;
        case SYLVAN_REORDER_P3_REHASH_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash in phase 3, maybe there are marked nodes remaining (%d)",
                    result);
            break;
        case SYLVAN_REORDER_P3_CLEAR_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot clear in phase 3, maybe there are marked nodes remaining (%d)",
                    result);
            break;
        case SYLVAN_REORDER_NO_REGISTERED_VARS:
            sprintf(buf, "SYLVAN_REORDER: the operation failed fast because there are no registered variables (%d)",
                    result);
            break;
        case SYLVAN_REORDER_NOT_INITIALISED:
            sprintf(buf, "SYLVAN_REORDER: please make sure you first initialize reordering (%d)", result);
            break;
        case SYLVAN_REORDER_ALREADY_RUNNING:
            sprintf(buf, "SYLVAN_REORDER: cannot start reordering when it is already running (%d)", result);
            break;
        default:
            sprintf(buf, "SYLVAN_REORDER: UNKNOWN ERROR (%d)", result);
            break;
    }
}

void sylvan_print_reorder_res(reorder_result_t result)
{
    char buff[100];
    sylvan_reorder_resdescription(result, buff, 100);
    if (!sylvan_reorder_issuccess(result)) fprintf(stderr, "%s\n", buff);
    else fprintf(stdout, "%s\n", buff);
}

TASK_IMPL_1(reorder_result_t, sylvan_varswap, uint32_t, pos)
{
    sylvan_stats_count(SYLVAN_RE_SWAP_COUNT);

    _Atomic (reorder_result_t) result = SYLVAN_REORDER_SUCCESS;

    // Check whether the two projection functions involved in this
    // swap are isolated. At the end, we'll be able to tell how many
    // isolated projection functions are there by checking only these
    // two functions again. This is done to eliminate the isolated
    // projection functions from the node count.

    int x_isolated = atomic_load_explicit(&levels->ref_count[levels->level_to_order[pos]], memory_order_relaxed) <= 1;
    int y_isolated =
            atomic_load_explicit(&levels->ref_count[levels->level_to_order[pos + 1]], memory_order_relaxed) <= 1;
    int isolated = -(x_isolated + y_isolated);

    //TODO: investigate the implications of swapping only the mappings (eg., sylvan operations referring to variables)
//    if (!interact_test(levels, levels->level_to_order[pos], levels->level_to_order[pos + 1])) {
//        levels->order_to_level[levels->level_to_order[pos]] = pos + 1;
//        levels->order_to_level[levels->level_to_order[pos + 1]] = pos;
//        uint32_t save = levels->level_to_order[pos];
//        levels->level_to_order[pos] = levels->level_to_order[pos + 1];
//        levels->level_to_order[pos + 1] = save;
//        return SYLVAN_REORDER_SUCCESS;
//    }

    CALL(sylvan_clear_cache);

#if SYLVAN_USE_LINEAR_PROBING
    // clear the entire table
    llmsset_clear_hashes(nodes);
#else
    // clear hashes of nodes with <var> and <var+1>
    CALL(sylvan_varswap_p0, pos, 0, nodes->table_size, &result);
    if (sylvan_reorder_issuccess(result) == 0) return result; // fail fast
#endif

    // handle all trivial cases, mark cases that are not trivial (no nodes are created)
    size_t marked_count = CALL(sylvan_varswap_p1, pos, 0, nodes->table_size, &result);
    if (sylvan_reorder_issuccess(result) == 0) return result; // fail fast

    if (marked_count > 0) {
        // do the not so trivial cases (creates new nodes)
        CALL(sylvan_varswap_p2, pos, 0, nodes->table_size, &result);
        if (sylvan_reorder_issuccess(result) == 0) {
            CALL(sylvan_varswap_p3, pos, &result);
        }
    }

    x_isolated = atomic_load_explicit(&levels->ref_count[levels->level_to_order[pos]], memory_order_relaxed) <= 1;
    y_isolated = atomic_load_explicit(&levels->ref_count[levels->level_to_order[pos + 1]], memory_order_relaxed) <= 1;
    isolated += x_isolated + y_isolated;
    levels->isolated_count += isolated;

    // swap the mappings
    levels->order_to_level[levels->level_to_order[pos]] = pos + 1;
    levels->order_to_level[levels->level_to_order[pos + 1]] = pos;
    uint32_t save = levels->level_to_order[pos];
    levels->level_to_order[pos] = levels->level_to_order[pos + 1];
    levels->level_to_order[pos + 1] = save;

    sylvan_clear_and_mark();
    sylvan_rehash_all();

    return result;
}

#if !SYLVAN_USE_LINEAR_PROBING
/**
 * Implementation of the zero phase of variable swapping.
 * For all <var+1> nodes, make <var> and rehash.
 *
 * Removes exactly the nodes that will be changed from the hash table.
 */
VOID_TASK_IMPL_4(sylvan_varswap_p0,
                 uint32_t, var, /** variable label **/
                 uint64_t, first, /** index in the unique table **/
                 uint64_t, count, /** index in the unique table **/
                 _Atomic (reorder_result_t)*, result)
{
    // divide and conquer (if count above BLOCKSIZE)
    if (count > BLOCKSIZE) {
        size_t split = count / 2;
        SPAWN(sylvan_varswap_p0, var, first, split, result);
        CALL(sylvan_varswap_p0, var, first + split, count - split, result);
        SYNC(sylvan_varswap_p0);
        return;
    }

    // skip buckets 0 and 1
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }

    const size_t end = first + count;

    for (first = llmsset_next(first - 1); first < end; first = llmsset_next(first)) {
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) continue; // a leaf
        uint32_t nvar = mtbddnode_getvariable(node);
        if (nvar == var || nvar == (var + 1)) {
            if (llmsset_clear_one(nodes, first) != 1) {
//                atomic_exchange(result, SYLVAN_REORDER_P0_CLEAR_FAIL);
            }
        }
    }
}

#endif

/**
 * Implementation of the first phase of variable swapping.
 * For all <var+1> nodes, set variable label to <var> and rehash.
 * For all <var> nodes not depending on <var+1>, set variable label to <var+1> and rehash.
 * For all <var> nodes depending on <var+1>, stay <var> and mark. (no rehash)
 * Returns number of marked nodes left.
 *
 * This algorithm is also used for the recovery phase 1. This is an identical
 * phase, except marked <var> nodes are unmarked. If the recovery flag is set, then only <var+1>
 * nodes are rehashed.
 */
TASK_IMPL_4(size_t, sylvan_varswap_p1,
            uint32_t, var, /** variable label **/
            size_t, first,  /** starting node index in the unique table **/
            size_t, count, /** number of nodes to visit form the starting index **/
            _Atomic (reorder_result_t)*, result)
{
    // divide and conquer (if count above BLOCKSIZE)
    if (count > BLOCKSIZE) {
        size_t split = count / 2;
        SPAWN(sylvan_varswap_p1, var, first, split, result);
        uint64_t res1 = CALL(sylvan_varswap_p1, var, first + split, count - split, result);
        uint64_t res2 = SYNC(sylvan_varswap_p1);
        return res1 + res2;
    }

    // count number of marked
    size_t marked = 0;

    // skip buckets 0 and 1
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }

    const size_t end = first + count;

    for (first = llmsset_next(first - 1); first < end; first = llmsset_next(first)) {
        if (atomic_load_explicit(result, memory_order_relaxed) != SYLVAN_REORDER_SUCCESS) return marked; // fail fast
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) continue; // a leaf
        uint32_t nvar = mtbddnode_getvariable(node);
        if (nvar >= mtbdd_levelscount()) continue;  // not registered <var>

        if (nvar == (var + 1)) {
            // if <var+1>, then replace with <var> and rehash
            mtbddnode_setvariable(node, var);
            if (llmsset_rehash_bucket(nodes, first) != 1) {
                atomic_exchange(result, SYLVAN_REORDER_P1_REHASH_FAIL);
            }
            continue;
        } else if (nvar != var) {
            continue; // not <var> or <var+1>
        }

        if (mtbddnode_getflag(node)) {
            // marked node, remove mark and rehash (we are apparently recovering)
            mtbddnode_setflag(node, 0);
            if (llmsset_rehash_bucket(nodes, first) != 1) {
                atomic_exchange(result, SYLVAN_REORDER_P3_REHASH_FAIL);
            }
            continue;
        }

        if (mtbddnode_ismapnode(node)) {
            MTBDD f0 = mtbddnode_getlow(node);
            if (f0 == mtbdd_false) {
                // we are at the end of a chain
                mtbddnode_setvariable(node, var + 1);
                llmsset_rehash_bucket(nodes, first);
            } else {
                // not the end of a chain, so f0 is the next in chain
                uint32_t vf0 = mtbdd_getvar(f0);
                if (vf0 > var + 1) {
                    // next in chain wasn't <var+1>...
                    mtbddnode_setvariable(node, var + 1);
                    if (!llmsset_rehash_bucket(nodes, first)) {
                        atomic_exchange(result, SYLVAN_REORDER_P1_REHASH_FAIL);
                    }
                } else {
                    // mark for phase 2
                    mtbddnode_setflag(node, 1);
                    marked++;
                }
            }
        } else {
            if (is_node_dependent_on(node, var)) {
                // mark for phase 2
                mtbddnode_setflag(node, 1);
                marked++;
            } else {
                mtbddnode_setvariable(node, var + 1);
                if (!llmsset_rehash_bucket(nodes, first)) {
                    atomic_exchange(result, SYLVAN_REORDER_P1_REHASH_FAIL);
                }
            }
        }
    }
    return marked;
}

int is_node_dependent_on(mtbddnode_t node, BDDVAR var)
{
    MTBDD f0 = mtbddnode_getlow(node);
    if (!mtbdd_isleaf(f0)) {
        uint32_t vf0 = mtbdd_getvar(f0);
        if (vf0 == var || vf0 == var + 1) return 1;
    }
    MTBDD f1 = mtbddnode_gethigh(node);
    if (!mtbdd_isleaf(f1)) {
        uint32_t vf1 = mtbdd_getvar(f1);
        if (vf1 == var || vf1 == var + 1) return 1;
    }
    return 0;
}

/**
 * Implementation of second phase of variable swapping.
 * For all nodes marked in the first phase:
 * - determine F00, F01, F10, F11
 * - obtain nodes F0 [var+1,F00,F10] and F1 [var+1,F01,F11]
 *   (and F0<>F1, trivial proof)
 * - in-place substitute outgoing edges with new F0 and F1
 * - and rehash into hash table
 * Returns 0 if there was no error, or 1 if nodes could not be
 * rehashed, or 2 if nodes could not be created, or 3 if both.
 */
VOID_TASK_IMPL_4(sylvan_varswap_p2,
                 uint32_t, var,
                 size_t, first,
                 size_t, count,
                 _Atomic (reorder_result_t)*, result)
{
    // divide and conquer (if count above BLOCKSIZE)
    if (count > BLOCKSIZE) {
        size_t split = count / 2;
        SPAWN(sylvan_varswap_p2, var, first, split, result);
        CALL(sylvan_varswap_p2, var, first + split, count - split, result);
        SYNC(sylvan_varswap_p2);
        return;
    }
    // skip buckets 0 and 1
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }
    reorder_result_t res;
    const size_t end = first + count;
    for (first = llmsset_next(first - 1); first < end; first = llmsset_next(first)) {
        if (atomic_load_explicit(result, memory_order_relaxed) != SYLVAN_REORDER_SUCCESS) return;  // fail fast
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) continue; // a leaf
        if (!mtbddnode_getflag(node)) continue; // an unmarked node
        if (mtbddnode_getvariable(node) >= mtbdd_levelscount()) continue; // not registered <var>
        if (mtbddnode_ismapnode(node)) {
            res = swap_mapnode(first);
        } else {
            res = swap_node(first);
        }
        if (sylvan_reorder_issuccess(res) == 0) { // if we failed let the parent know
            atomic_exchange(result, res);
        }
    }
}

#define ref_dec(level) atomic_fetch_add_explicit(&levels->ref_count[levels->level_to_order[level]], -1, memory_order_relaxed)
#define ref_inc(level) atomic_fetch_add_explicit(&levels->ref_count[levels->level_to_order[level]], 1, memory_order_relaxed)
#define var_dec(level) atomic_fetch_add_explicit(&levels->var_count[levels->level_to_order[level]], -1, memory_order_relaxed)
#define var_inc(level) atomic_fetch_add_explicit(&levels->var_count[levels->level_to_order[level]], 1, memory_order_relaxed)

reorder_result_t swap_node(size_t index)
{
    MTBDD newf1, newf0, f1, f0, f11, f10, f01, f00;

    mtbddnode_t node = MTBDD_GETNODE(index);
    BDDVAR var = mtbddnode_getvariable(node);

    // obtain cofactors
    f0 = mtbddnode_getlow(node);
    f1 = mtbddnode_gethigh(node);

    f01 = f00 = f0;
    if (!mtbdd_isleaf(f0) && mtbdd_getvar(f0) == var) {
        f00 = mtbdd_getlow(f0);
        f01 = mtbdd_gethigh(f0);
    }

    f11 = f10 = f1;
    if (!mtbdd_isleaf(f1) && mtbdd_getvar(f1) == var) {
        f10 = mtbdd_getlow(f1);
        f11 = mtbdd_gethigh(f1);
    }

//    ref_dec(mtbdd_getvar(f0));

    if (f10 == f00) {
        newf0 = f00;
//        ref_inc(mtbdd_getvar(newf0));
    } else {
        // Create the low high child.
        // since we maintain the invariant that the low child has higher index than the high child we use <var+1> as the index
        newf0 = mtbdd_varswap_makenode(var + 1, f00, f10);
        if (f0 == mtbdd_invalid) return SYLVAN_REORDER_P2_CREATE_FAIL;
//        ref_inc(mtbdd_getvar(f10));
//        ref_inc(mtbdd_getvar(f00));
    }

//    ref_dec(mtbdd_getvar(f1));

    if (f11 == f01) {
        newf1 = f11;
//        ref_inc(mtbdd_getvar(newf1));
    } else {
        // Create the new high child.
        // since we maintain the invariant that the low child has higher index than the high child we use <var+1> as the index
        newf1 = mtbdd_varswap_makenode(var + 1, f01, f11);
        if (f1 == mtbdd_invalid) return SYLVAN_REORDER_P2_CREATE_FAIL;
//        ref_inc(mtbdd_getvar(f01));
//        ref_inc(mtbdd_getvar(f11));
    }

    // 1. # of nodes is increased at <var+1> level due to only f1 having <var> index
    if (mtbdd_getvar(f1) == var && mtbdd_getvar(f0) > var) {
        // we mutate <var> since we did not swap the mapping yet
        var_inc(var);
    }
    // 2. # of nodes is increased at <var+1> level due to only f0 having <var> index
    if (mtbdd_getvar(f0) == var && mtbdd_getvar(f1) > var) {
        // we mutate <var> since we did not swap the mapping yet
        var_inc(var);
    }
    // 3. # of nodes is decreased at <var+1> level due to f10 and f01 pointing to the same children
    if (mtbdd_getvar(f1) == var && mtbdd_getvar(f0) == var && f10 == f01) {
        // we mutate <var> since we did not swap the mapping yet
        var_dec(var);
    }

    // update node, which also removes the mark
    mtbddnode_makenode(node, var, newf0, newf1);
    llmsset_rehash_bucket(nodes, index);

    return SYLVAN_REORDER_SUCCESS;
}

reorder_result_t swap_mapnode(size_t index)
{
    mtbddnode_t node = MTBDD_GETNODE(index);
    BDDVAR var = mtbddnode_getvariable(node);

    // it is a map node, swap places with next in chain
    MTBDD f0 = mtbddnode_getlow(node);
    MTBDD f1 = mtbddnode_gethigh(node);
    mtbddnode_t n0 = MTBDD_GETNODE(f0);
    MTBDD f00 = node_getlow(f0, n0);
    MTBDD f01 = node_gethigh(f0, n0);
    f0 = mtbdd_varswap_makemapnode(var + 1, f00, f1);
    if (f0 == mtbdd_invalid) {
        return SYLVAN_REORDER_P2_CREATE_FAIL;
    } else {
        mtbddnode_makemapnode(node, var, f0, f01);
        llmsset_rehash_bucket(nodes, var);
    }
    return SYLVAN_REORDER_SUCCESS;
}

/**
 * Implementation of third phase of variable swapping.
 */
VOID_TASK_IMPL_2(sylvan_varswap_p3, uint32_t, pos, _Atomic (reorder_result_t)*, result)
{
#if SYLVAN_USE_LINEAR_PROBING
    // clear the entire table
    llmsset_clear_hashes(nodes);
#else
    // clear hashes of nodes with <var> and <var+1>
    CALL(sylvan_varswap_p0, pos, 0, nodes->table_size, result);
    if (sylvan_reorder_issuccess(*result) == 0) {
        atomic_exchange(result, SYLVAN_REORDER_P3_CLEAR_FAIL);
    }
#endif
    // at this point we already have nodes marked from P2 so we will unmark them now in P1
    size_t marked_count = CALL(sylvan_varswap_p1, pos, 0, nodes->table_size, result);
    if (marked_count > 0 && sylvan_reorder_issuccess(*result)) {
        // do the not so trivial cases (but won't create new nodes this time)
        CALL(sylvan_varswap_p2, pos, 0, nodes->table_size, result);
    }
}