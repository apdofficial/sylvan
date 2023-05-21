#include <sylvan_int.h>
#include "sylvan_varswap.h"
#include "sylvan_levels.h"
#include "sylvan_align.h"
#include "sylvan_interact.h"

#define STATS 0 // useful information w.r.t. dynamic reordering

void swap_node(size_t index);

void swap_mapnode(size_t index);

int is_node_dependent_on(mtbddnode_t node, BDDVAR var);

#if SYLVAN_USE_LINEAR_PROBING
/*!
   \brief Adjacent variable swap phase 0 (Linear probing compatible)
   \details Clear hashes of the entire table.
*/
#define sylvan_varswap_p0() llmsset_clear_hashes(nodes);
#else
VOID_TASK_DECL_4(sylvan_varswap_p0, uint32_t, uint64_t, uint64_t, _Atomic (reorder_result_t) *);
/*!
   \brief Adjacent variable swap phase 0 (Chaining compatible)
   \details Clear hashes of nodes with var and var+1, Removes exactly the nodes
   that will be changed from the hash table.
*/
#define sylvan_varswap_p0(var, result) CALL(sylvan_varswap_p0, var, 2, nodes->table_size, result)
#endif

TASK_DECL_4(size_t, sylvan_varswap_p1, uint32_t, size_t, size_t, _Atomic (reorder_result_t) *);
/*!
   \brief Adjacent variable swap phase 1
   \details Handle all trivial cases where no node is created, mark cases that are not trivial.
   \return number of nodes that were marked
*/
#define sylvan_varswap_p1(var, first, count, result) CALL(sylvan_varswap_p1, var, first, count, result)

VOID_TASK_DECL_4(sylvan_varswap_p2, uint32_t, size_t, size_t, _Atomic (reorder_result_t) *);
/*!
   \brief Adjacent variable swap phase 2
   \details Handle the not so trivial cases. (creates new nodes)
*/
#define sylvan_varswap_p2(var, result) CALL(sylvan_varswap_p2, var, 0, nodes->table_size, result)

void sylvan_varswap_resdescription(reorder_result_t result, char *buf, size_t buf_len)
{
    assert(buf_len >= 100);
    switch (result) {
        case SYLVAN_REORDER_ROLLBACK:
            sprintf(buf, "SYLVAN_VARSWAP_ROLLBACK: the operation was aborted and rolled back");
            break;
        case SYLVAN_REORDER_SUCCESS:
            sprintf(buf, "SYLVAN_REORDER_SUCCESS: success (%d)", result);
            break;
        case SYLVAN_REORDER_P1_REHASH_FAIL:
            sprintf(buf, "SYLVAN_REORDER_P1_REHASH_FAIL: cannot rehash in phase 1, no marked nodes remaining (%d)",
                    result);
            break;
        case SYLVAN_REORDER_P1_REHASH_FAIL_MARKED:
            sprintf(buf,
                    "SYLVAN_REORDER_P1_REHASH_FAIL_MARKED: cannot rehash in phase 1, and marked nodes remaining (%d)",
                    result);
            break;
        case SYLVAN_REORDER_P2_REHASH_FAIL:
            sprintf(buf, "SYLVAN_REORDER_P2_REHASH_FAIL: cannot rehash in phase 2, no marked nodes remaining (%d)",
                    result);
            break;
        case SYLVAN_REORDER_P2_CREATE_FAIL:
            sprintf(buf,
                    "SYLVAN_REORDER_P2_CREATE_FAIL: cannot create node in phase 2 (ergo marked nodes remaining) (%d)",
                    result);
            break;
        case SYLVAN_REORDER_P2_REHASH_AND_CREATE_FAIL:
            sprintf(buf,
                    "SYLVAN_REORDER_P2_REHASH_AND_CREATE_FAIL: cannot rehash and cannot create node in phase 2 (%d)",
                    result);
            break;
        case SYLVAN_REORDER_NOT_INITIALISED:
            sprintf(buf, "SYLVAN_REORDER_NOT_INITIALISED: please make sure you first initialize reordering (%d)",
                    result);
            break;
        case SYLVAN_REORDER_ALREADY_RUNNING:
            sprintf(buf, "SYLVAN_REORDER_ALREADY_RUNNING: cannot start reordering when it is already running (%d)",
                    result);
            break;
        default:
            sprintf(buf, "SYLVAN_REORDER: UNKNOWN ERROR (%d)", result);
            break;
    }
}

void sylvan_print_varswap_res(reorder_result_t result)
{
    char buff[100];
    sylvan_varswap_resdescription(result, buff, 100);
    if (!sylvan_varswap_issuccess(result)) fprintf(stderr, "%s\n", buff);
    else fprintf(stdout, "%s\n", buff);
}

/**
 * Custom makenode that doesn't trigger garbage collection.
 * Instead, returns mtbdd_invalid if we can't create the node.
 */
MTBDD mtbdd_varswap_makenode(BDDVAR var, MTBDD low, MTBDD high)
{
    struct mtbddnode n;
    uint64_t index;
    int mark;
    int created;

    if (low == high) return low;

    if (MTBDD_HASMARK(low)) {
        mark = 1;
        low = MTBDD_TOGGLEMARK(low);
        high = MTBDD_TOGGLEMARK(high);
    } else {
        mark = 0;
    }

    mtbddnode_makenode(&n, var, low, high);

    index = llmsset_lookup(nodes, n.a, n.b, &created);
    if (index == 0) return mtbdd_invalid;

    if (created) sylvan_stats_count(BDD_NODES_CREATED);
    else sylvan_stats_count(BDD_NODES_REUSED);

    return mark ? index | mtbdd_complement : index;
}

/**
 * Custom makemapnode that doesn't trigger garbage collection.
 * Instead, returns mtbdd_invalid if we can't create the node.
 */
MTBDD mtbdd_varswap_makemapnode(BDDVAR var, MTBDD low, MTBDD high)
{
    struct mtbddnode n;
    uint64_t index;
    int created;

    // in an MTBDDMAP, the low edges eventually lead to 0 and cannot have a low mark
    assert(!MTBDD_HASMARK(low));

    mtbddnode_makemapnode(&n, var, low, high);

    index = llmsset_lookup(nodes, n.a, n.b, &created);
    if (index == 0) return mtbdd_invalid;

    if (created) sylvan_stats_count(BDD_NODES_CREATED);
    else sylvan_stats_count(BDD_NODES_REUSED);

    return index;
}

TASK_IMPL_1(reorder_result_t, sylvan_varswap, uint32_t, pos)
{
    sylvan_stats_count(SYLVAN_RE_SWAP_COUNT);

    _Atomic (reorder_result_t) result = SYLVAN_REORDER_SUCCESS;

    /* Check whether the two projection functions involved in this
    ** swap are isolated. At the end, we'll be able to tell how many
    ** isolated projection functions are there by checking only these
    ** two functions again. This is done to eliminate the isolated
    ** projection functions from the node count.
    */
    size_t x_isolated = atomic_load_explicit(&levels->ref_count[levels->level_to_order[pos]], memory_order_relaxed) == 1;
    size_t y_isolated = atomic_load_explicit(&levels->ref_count[levels->level_to_order[pos + 1]], memory_order_relaxed) == 1;
    size_t isolated = -(x_isolated + y_isolated);

//    if (interact_test(levels, levels->level_to_order[pos], levels->level_to_order[pos + 1]) == 0) {
//        levels->order_to_level[levels->level_to_order[pos]] = pos + 1;
//        levels->order_to_level[levels->level_to_order[pos + 1]] = pos;
//        uint32_t save = levels->level_to_order[pos];
//        levels->level_to_order[pos] = levels->level_to_order[pos + 1];
//        levels->level_to_order[pos + 1] = save;
//        return result;
//    }

    // ensure that the cache is cleared
    CALL(sylvan_clear_cache);

#if SYLVAN_USE_LINEAR_PROBING
    // clear the entire table
    sylvan_varswap_p0();
#else
    // first clear hashes of nodes with <var> and <var+1>
    sylvan_varswap_p0(pos, &result);
#endif

    // handle all trivial cases, mark cases that are not trivial (no nodes are created)
    uint64_t marked_count = sylvan_varswap_p1(pos, 0, nodes->table_size, &result);

    if (marked_count > 0) {

        // do the not so trivial cases (creates new nodes)
        sylvan_varswap_p2(pos, &result);

        if (result != SYLVAN_REORDER_SUCCESS) {
#if STATS
            printf("Recovery time!\n");
#endif
#if SYLVAN_USE_LINEAR_PROBING
            // clear the entire table
            sylvan_varswap_p0();
#else
            // first clear hashes of nodes with <var> and <var+1>
            sylvan_varswap_p0(pos, &result);
#endif
            // handle all trivial cases, mark cases that are not trivial
            marked_count = sylvan_varswap_p1(pos, 0, nodes->table_size, &result);

            if (marked_count > 0 && result == SYLVAN_REORDER_SUCCESS) {
                // do the not so trivial cases (but won't create new nodes this time)
                sylvan_varswap_p2(pos, &result);
                if (result != SYLVAN_REORDER_SUCCESS) {
                    // actually, we should not see this!
                    fprintf(stderr, "sylvan: recovery varswap failed!\n");
                    return SYLVAN_REORDER_P2_REHASH_AND_CREATE_FAIL;
                }
            } else {
                return SYLVAN_REORDER_P1_REHASH_FAIL_MARKED;
            }
#if STATS
            printf("Recovery good.\n");
#endif
            return SYLVAN_REORDER_ROLLBACK;
        }
    }

    x_isolated = atomic_load_explicit(&levels->ref_count[levels->level_to_order[pos]], memory_order_relaxed) == 1;
    y_isolated = atomic_load_explicit(&levels->ref_count[levels->level_to_order[pos + 1]], memory_order_relaxed) == 1;
    isolated += x_isolated + y_isolated;
    levels->isolated_count += isolated;

    levels->order_to_level[levels->level_to_order[pos]] = pos + 1;
    levels->order_to_level[levels->level_to_order[pos + 1]] = pos;
    uint32_t save = levels->level_to_order[pos];
    levels->level_to_order[pos] = levels->level_to_order[pos + 1];
    levels->level_to_order[pos + 1] = save;

    CALL(sylvan_clear_and_mark);
    CALL(sylvan_rehash_all);

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
        if (mtbddnode_isleaf(node)) {
            printf("sylvan_varswap_p0: leaf\n");
            continue; // a leaf
        }
        uint32_t nvar = mtbddnode_getvariable(node);
        if (nvar == var || nvar == (var + 1)) {
            llmsset_clear_one(nodes, first);
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
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) {
            printf("sylvan_varswap_p1: leaf\n");
            continue; // a leaf
        }
        uint32_t nvar = mtbddnode_getvariable(node);
        if (nvar >= mtbdd_levelscount()) continue;  // not registered <var>

        if (nvar == (var + 1)) {
            // if <var+1>, then replace with <var> and rehash
            mtbddnode_setvariable(node, var);
            if (llmsset_rehash_bucket(nodes, first) != 1) {
                fprintf(stderr, "sylvan_varswap_p1: llmsset_rehash_bucket(%zu) failed!\n", first);
                *result = SYLVAN_REORDER_P1_REHASH_FAIL;
            }
            continue;
        } else if (nvar != var) {
            continue; // not <var> or <var+1>
        }

        if (mtbddnode_getflag(node)) {
            // marked node, remove mark and rehash (we are apparently recovering)
            mtbddnode_setflag(node, 0);
            llmsset_rehash_bucket(nodes, first);
            if (llmsset_rehash_bucket(nodes, first) != 1) {
                fprintf(stderr, "sylvan_varswap_p1:recovery: llmsset_rehash_bucket(%zu) failed!\n", first);
                *result = SYLVAN_REORDER_P1_REHASH_FAIL;
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
                        fprintf(stderr, "sylvan_varswap_p1: llmsset_rehash_bucket(%zu) failed!\n", first);
                        *result = SYLVAN_REORDER_P1_REHASH_FAIL;
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
                    fprintf(stderr, "sylvan_varswap_p1: llmsset_rehash_bucket(%zu) failed!\n", first);
                    *result = SYLVAN_REORDER_P1_REHASH_FAIL;
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

    const size_t end = first + count;
    for (first = llmsset_next(first - 1); first < end; first = llmsset_next(first)) {
        if (*result != SYLVAN_REORDER_SUCCESS) return; // the table is full
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) {
            printf("sylvan_varswap_p2: leaf\n");
            continue; // a leaf
        }
        if (!mtbddnode_getflag(node)) continue; // an unmarked node
        if (mtbddnode_getvariable(node) >= mtbdd_levelscount()) continue; // not registered <var>
        if (mtbddnode_ismapnode(node)) {
            swap_mapnode(first);
        } else {
            swap_node(first);
        }
    }
}

/**
 *
 * Swap a node <var> with its successor nodes <var+1>.
 *
 * @preconditions:
 *  - node is marked
 *  - node is not a leaf
 *  - node is <var>
 *  - node childrens are <var+1>
 */
void swap_node(size_t index)
{
    mtbddnode_t node = MTBDD_GETNODE(index);
    BDDVAR var = mtbddnode_getvariable(node);
    // obtain cofactors
    MTBDD f0 = mtbddnode_getlow(node);
    MTBDD f1 = mtbddnode_gethigh(node);
    MTBDD f00, f01, f10, f11;

    f01 = f00 = f0;
    if (!mtbdd_isleaf(f0)) {
        mtbddnode_t n0 = MTBDD_GETNODE(f0);
        if (mtbddnode_getvariable(n0) == var) {
            f00 = node_getlow(f0, n0);
            f01 = node_gethigh(f0, n0);
        }
    }
    f11 = f10 = f1;
    if (!mtbdd_isleaf(f1)) {
        mtbddnode_t n1 = MTBDD_GETNODE(f1);
        if (mtbddnode_getvariable(n1) == var) {
            f10 = node_getlow(f1, n1);
            f11 = node_gethigh(f1, n1);
        }
    }

    // there are 3 cases to consider:

//    // 1. # of nodes is increased at <var+1> level due to f1 having higher <var> index than f0
//    if (mtbdd_getvar(f1) == var && mtbdd_getvar(f0) > var) {
//        // this is the case when # of nodes is increased at <var+1> level (other levels don't change # of nodes)
//        atomic_fetch_add(&levels->ref_count[levels->level_to_order[var + 1]], 1);
//        atomic_fetch_add(&levels->var_count[levels->level_to_order[var + 1]], 1);
//        // now we have two nodes at level <var+1> pointing to f10 and F01 which will be added after the swap so we increase the ref count
//        atomic_fetch_add(&levels->ref_count[levels->level_to_order[mtbdd_getvar(f10)]], 1);
//    }
//
//    // 2. # of nodes is increased at <var+1> level due to f0 having higher <var> index than f1
//    if (mtbdd_getvar(f1) > var && mtbdd_getvar(f0) == var) {
//        // this is the case when # of nodes is increased at <var+1> level (other levels don't change # of nodes)
//        atomic_fetch_add(&levels->ref_count[levels->level_to_order[var + 1]], 1);
//        atomic_fetch_add(&levels->var_count[levels->level_to_order[var + 1]], 1);
//        // now we have two nodes at level <var+1> pointing to f10 and F01 which will be added after the swap so we increase the ref count
//        atomic_fetch_add(&levels->ref_count[levels->level_to_order[mtbdd_getvar(f01)]], 1);
//    }
//
//    // 3. # of nodes is decreased at <var+1> level due to f10 and f01 pointing to the same children
//    if (mtbdd_getvar(f1) == var && mtbdd_getvar(f0) == var && f10 == f01) {
//        // this is the case when # of nodes is decreased at <var+1> level (other levels don't change # of nodes)
//        atomic_fetch_add(&levels->var_count[levels->level_to_order[var + 1]], -1);
//        atomic_fetch_add(&levels->ref_count[levels->level_to_order[mtbdd_getvar(f0)]], -1);
//        // now we have one less node at level <var+1> pointing to f10 / f01 so we decrease the ref count
//        atomic_fetch_add(&levels->ref_count[levels->level_to_order[mtbdd_getvar(f10)]], -1);
//    }

    // Create the new high child.
    f1 = mtbdd_varswap_makenode(var + 1, f01, f11);
    if (f1 == mtbdd_invalid) {
        fprintf(stderr, "sylvan_varswap_p2: SYLVAN_VARSWAP_P2_CREATE_FAIL\n");
        return;
    }

    // Create the low high child.
    f0 = mtbdd_varswap_makenode(var + 1, f00, f10);
    if (f0 == mtbdd_invalid) {
        fprintf(stderr, "sylvan_varswap_p2: SYLVAN_VARSWAP_P2_CREATE_FAIL\n");
        return;
    }

    // update node, which also removes the mark
    mtbddnode_makenode(node, var, f0, f1);
    llmsset_rehash_bucket(nodes, index);
}

/**
 *
 * Swap a map node <var> with its successor nodes <var+1>.
 *
 * @preconditions:
 *  - node is a map node
 *  - node is marked
 *  - node is not a leaf
 *  - node is <var>
 *  - node childrens are <var+1>
 */
void swap_mapnode(size_t index)
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
        fprintf(stderr, "sylvan_varswap_p2: SYLVAN_VARSWAP_P2_CREATE_FAIL\n");
        return;
    } else {
        mtbddnode_makemapnode(node, var, f0, f01);
        llmsset_rehash_bucket(nodes, var);
    }
}
