#include <sylvan_int.h>
#include <sylvan_align.h>

#define node_ref_set(dd, v) levels_node_ref_count_set(levels, (dd) & SYLVAN_TABLE_MASK_INDEX, v)
#define node_ref_inc(dd) levels_node_ref_count_add(levels, (dd) & SYLVAN_TABLE_MASK_INDEX, 1)
#define node_ref_dec(dd) levels_node_ref_count_add(levels, (dd) & SYLVAN_TABLE_MASK_INDEX, -1)

#define var_inc(lvl) levels_var_count_add(levels, lvl, 1); levels_nodes_count_add(levels, 1)
#define var_dec(lvl) levels_var_count_add(levels, lvl, -1); levels_nodes_count_add(levels, -1)

#define ref_inc(lvl) levels_ref_count_add(levels, lvl, 1)
#define ref_dec(lvl) levels_ref_count_add(levels, lvl, -1)

#define is_node_dead(dd) levels_is_node_dead(levels, dd & SYLVAN_TABLE_MASK_INDEX)

#define delete_node_ref(idx) CALL(delete_node_ref, idx)
VOID_TASK_DECL_1(delete_node_ref, size_t)

static inline int is_node_dependent_on(mtbddnode_t node, BDDVAR var)
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

reorder_result_t swap_node(mtbddnode_t node, size_t index);

reorder_result_t swap_mapnode(mtbddnode_t node, size_t index);

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
   @brief Adjacent variable swap phase 3
   @details Recovery phase, restore the nodes that were marked in phase 1.
*/
VOID_TASK_DECL_2(sylvan_varswap_p3, uint32_t, _Atomic (reorder_result_t) *)

TASK_IMPL_1(reorder_result_t, sylvan_varswap, uint32_t, pos)
{
    assert(pos != sylvan_invalid);
    if (pos == sylvan_invalid) return SYLVAN_REORDER_NO_REGISTERED_VARS;
    sylvan_stats_count(SYLVAN_RE_SWAP_COUNT);

    _Atomic (reorder_result_t) result = SYLVAN_REORDER_SUCCESS;

    // Check whether the two projection functions involved in this
    // swap are isolated. At the end, we'll be able to tell how many
    // isolated projection functions are there by checking only these
    // two functions again. This is done to eliminate the isolated
    // projection functions from the node count.
    BDDVAR x = levels->level_to_order[pos];
    BDDVAR y = levels->level_to_order[pos + 1];
    int isolated1 = levels_is_isolated(levels, x);
    int isolated2 = levels_is_isolated(levels, y);
    int isolated = -(isolated1 + isolated2);

    levels_bitmap_p2_realloc(nodes->table_size);

    //TODO: investigate the implications of swapping only the mappings (eg., sylvan operations referring to variables)
//    if (interact_test(levels, x, y) == 0) { }

    sylvan_clear_cache();

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
        levels_p2_clear_all();
    }

    if (levels->ref_count_size > 0) {
        isolated +=
                levels_is_isolated(levels, levels->level_to_order[pos]) +
                levels_is_isolated(levels, levels->level_to_order[pos + 1]);
        levels->isolated_count += isolated;
    }


    // gc the dead nodes
    size_t index = llmsset_next(1);
    while (index != llmsset_nindex) {
        if (index == 0 || index == 1 || index == sylvan_invalid) index = llmsset_next(index);
        if (index == 0 || index == 1 || index == sylvan_invalid) continue;
        if (is_node_dead(index)) {
            delete_node_ref(index);
#if !SYLVAN_USE_LINEAR_PROBING
            llmsset_clear_one_hash(nodes, index);
            llmsset_clear_one_data(nodes, index);
#endif
        }
        index = llmsset_next(index);
    }

#if SYLVAN_USE_LINEAR_PROBING
    sylvan_clear_and_mark();
    sylvan_rehash_all();
#endif

    // swap the mappings
    levels->order_to_level[levels->level_to_order[pos]] = pos + 1;
    levels->order_to_level[levels->level_to_order[pos + 1]] = pos;
    uint32_t save = levels->level_to_order[pos];
    levels->level_to_order[pos] = levels->level_to_order[pos + 1];
    levels->level_to_order[pos + 1] = save;

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
    for (first = llmsset_next(first-1); first < end; first = llmsset_next(first)) {
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) continue; // a leaf
        uint32_t nvar = mtbddnode_getvariable(node);
        if (nvar == var || nvar == (var + 1)) {
            llmsset_clear_one_hash(nodes, first);
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

    for (first = llmsset_next(first-1); first < end; first = llmsset_next(first)) {
        if (atomic_load_explicit(result, memory_order_relaxed) != SYLVAN_REORDER_SUCCESS) return marked; // fail fast
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) continue; // a leaf
        uint32_t nvar = mtbddnode_getvariable(node);

        if (nvar == (var + 1)) {
            // if <var+1>, then replace with <var> and rehash
            var_inc(var);
            var_dec(var + 1);
            mtbddnode_setvariable(node, var);
            if (llmsset_rehash_bucket(nodes, first) != 1) {
                atomic_store(result, SYLVAN_REORDER_P1_REHASH_FAIL);
            }
            continue;
        } else if (nvar != var) {
            continue; // not <var> or <var+1>
        }

        if (mtbddnode_ismapnode(node)) {
            MTBDD f0 = mtbddnode_getlow(node);
            if (f0 == mtbdd_false) {
                // we are at the end of a chain
                var_inc(var + 1);
                var_dec(var);
                mtbddnode_setvariable(node, var + 1);
                llmsset_rehash_bucket(nodes, first);
            } else {
                // not the end of a chain, so f0 is the next in chain
                uint32_t vf0 = mtbdd_getvar(f0);
                if (vf0 > var + 1) {
                    // next in chain wasn't <var+1>...
                    var_inc(var + 1);
                    var_dec(var);
                    mtbddnode_setvariable(node, var + 1);
                    if (!llmsset_rehash_bucket(nodes, first)) {
                        atomic_store(result, SYLVAN_REORDER_P1_REHASH_FAIL);
                    }
                } else {
                    // mark for phase 2
                    levels_p2_set(first);
                    marked++;
                }
            }
        } else {
            if (is_node_dependent_on(node, var)) {
                // mark for phase 2
                levels_p2_set(first);
                marked++;
            } else {
                var_inc(var + 1);
                var_dec(var);
                mtbddnode_setvariable(node, var + 1);
                if (!llmsset_rehash_bucket(nodes, first)) {
                    atomic_store(result, SYLVAN_REORDER_P1_REHASH_FAIL);
                }
            }
        }
    }
    return marked;
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

    for (first = levels_p2_next(first-1); first < end; first = levels_p2_next(first)) {
        mtbddnode_t node = MTBDD_GETNODE(first);

        if (atomic_load_explicit(result, memory_order_relaxed) != SYLVAN_REORDER_SUCCESS) return;  // fail fast
        if (mtbddnode_ismapnode(node)) {
            res = swap_mapnode(node, first);
        } else {
            res = swap_node(node, first);
        }
        if (sylvan_reorder_issuccess(res) == 0) { // if we failed let the parent know
            atomic_store(result, res);
        }
    }
}

reorder_result_t swap_node(mtbddnode_t node, size_t index)
{
    MTBDD newf1, newf0, f1, f0, f11, f10, f01, f00;
    BDDVAR var = mtbddnode_getvariable(node);

    int created0 = 0;
    int created1 = 0;

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

    // The new nodes required at level i (i.e., (xi, F01, F11) and (xi, F00, F10)) may be
    // degenerate nodes (e.g., in the case that F11 = F01 or F10 == F00),
    // or may already exist in the DAG as required to implement other functions.

    node_ref_dec(f1);
    newf1 = mtbdd_varswap_makenode(var + 1, f01, f11, &created1);
    if (newf1 == mtbdd_invalid) return SYLVAN_REORDER_P2_CREATE_FAIL;
    if (created1) {
        var_inc(var + 1);
        node_ref_set(newf1, 1);
        node_ref_inc(f11);
        node_ref_inc(f01);
    } else {
        node_ref_inc(newf1);
    }

    node_ref_dec(f0);
    newf0 = mtbdd_varswap_makenode(var + 1, f00, f10, &created0);
    if (newf0 == mtbdd_invalid) return SYLVAN_REORDER_P2_CREATE_FAIL;
    if (created0) {
        var_inc(var + 1);
        node_ref_set(newf0, 1);
        node_ref_inc(f00);
        node_ref_inc(f10);
    } else {
        node_ref_inc(newf0);
    }

    // update node, which also removes the mark
    mtbddnode_makenode(node, var, newf0, newf1);
    llmsset_rehash_bucket(nodes, index);

    return SYLVAN_REORDER_SUCCESS;
}

reorder_result_t swap_mapnode(mtbddnode_t node, size_t index)
{
    // TODO: implement ref/var counters
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
    }

    mtbddnode_makemapnode(node, var, f0, f01);
    llmsset_rehash_bucket(nodes, index);

    return SYLVAN_REORDER_SUCCESS;
}

VOID_TASK_IMPL_2(sylvan_varswap_p3, uint32_t, pos, _Atomic (reorder_result_t)*, result)
{
#if SYLVAN_USE_LINEAR_PROBING
    // clear the entire table
    llmsset_clear_hashes(nodes);
#else
    // clear hashes of nodes with <var> and <var+1>
    CALL(sylvan_varswap_p0, pos, 0, nodes->table_size, result);
#endif
    printf("Running recovery after running out of memory...\n");
    // at this point we already have nodes marked from P2 so we will unmark them now in P1
    size_t marked_count = CALL(sylvan_varswap_p1, pos, 0, nodes->table_size, result);
    if (marked_count > 0 && sylvan_reorder_issuccess(*result)) {
        // do the not so trivial cases (but won't create new nodes this time)
        CALL(sylvan_varswap_p2, pos, 0, nodes->table_size, result);
    }
}

VOID_TASK_IMPL_1(delete_node_ref, size_t, index)
{
    mtbddnode_t f = MTBDD_GETNODE(index);
    var_dec(mtbddnode_getvariable(f));

    if (!mtbddnode_isleaf(f)) {
        MTBDD f1 = mtbddnode_gethigh(f);
        if (f1 != sylvan_invalid && (f1 & SYLVAN_TABLE_MASK_INDEX) != 0 && (f1 & SYLVAN_TABLE_MASK_INDEX) != 1) {
            node_ref_dec(f1);
            ref_dec(mtbdd_getvar(f1));
        }

        MTBDD f0 = mtbddnode_getlow(f);
        if (f0 != sylvan_invalid && (f0 & SYLVAN_TABLE_MASK_INDEX) != 0 && (f0 & SYLVAN_TABLE_MASK_INDEX) != 1) {
            node_ref_dec(f0);
            ref_dec(mtbdd_getvar(f0));
        }
    }
}