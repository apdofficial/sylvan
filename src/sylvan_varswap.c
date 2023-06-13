#include <sylvan_int.h>
#include <sylvan_align.h>

/**
 * @brief Check if a node is dependent on node with label <var> or <var>+1
 */
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

#if !SYLVAN_USE_LINEAR_PROBING
/*!
   \brief Adjacent variable swap phase 0 (Chaining compatible)
   \details Clear hashes of nodes with var and var+1, Removes exactly the nodes
   that will be changed from the hash table.
*/
VOID_TASK_DECL_4(sylvan_varswap_p0, uint32_t, size_t, size_t, _Atomic (reorder_result_t) *)

#define sylvan_varswap_p0(pos, result) CALL(sylvan_varswap_p0, pos, 0, nodes->table_size, result)
#endif

/*!
   @brief Adjacent variable swap phase 2
   @details Handle all trivial cases where no node is created, mark cases that are not trivial.
   @return number of nodes that were marked
*/
TASK_DECL_4(size_t, sylvan_varswap_p1, uint32_t, size_t, size_t, _Atomic (reorder_result_t) *)

#define sylvan_varswap_p1(pos, result) CALL(sylvan_varswap_p1, pos, 0, nodes->table_size, result)

/*!
   @brief Adjacent variable swap phase 2
   @details Handle the not so trivial cases. (creates new nodes)
*/
VOID_TASK_DECL_3(sylvan_varswap_p2, size_t, size_t, _Atomic (reorder_result_t) *)

#define sylvan_varswap_p2(result) CALL(sylvan_varswap_p2, 0, nodes->table_size, result)

/*!
   @brief Adjacent variable swap phase 3
   @details Recovery phase, restore the nodes that were marked in phase 1.
*/
VOID_TASK_DECL_2(sylvan_varswap_p3, uint32_t, _Atomic (reorder_result_t) *)

#define sylvan_varswap_p3(pos, result) CALL(sylvan_varswap_p3, pos, result)


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
    BDDVAR xIndex = reorder_db->levels.level_to_order[pos];
    BDDVAR yIndex = reorder_db->levels.level_to_order[pos + 1];
    int isolated = -(mrc_is_var_isolated(&reorder_db->mrc, xIndex) + mrc_is_var_isolated(&reorder_db->mrc, yIndex));

    //TODO: investigate the implications of swapping only the mappings (eg., sylvan operations referring to variables)
    if (interact_test(&reorder_db->matrix, xIndex, yIndex) == 0) {
//        printf("non-interacting swap: %d %d\n", xIndex, yIndex);
//        reorder_db->levels.order_to_level[reorder_db->levels.level_to_order[pos]] = pos + 1;
//        reorder_db->levels.order_to_level[reorder_db->levels.level_to_order[pos + 1]] = pos;
//        uint32_t save = reorder_db->levels.level_to_order[pos];
//        reorder_db->levels.level_to_order[pos] = reorder_db->levels.level_to_order[pos + 1];
//        reorder_db->levels.level_to_order[pos + 1] = save;
//        return result;
    }
    sylvan_clear_cache();

#if SYLVAN_USE_LINEAR_PROBING
    // clear the entire table
    llmsset_clear_hashes(nodes);
#else
    // clear hashes of nodes with <var> and <var+1>
    sylvan_varswap_p0(pos, &result);
    if (sylvan_reorder_issuccess(result) == 0) return result; // fail fast
#endif

    // handle all trivial cases, mark cases that are not trivial (no nodes are created)
    size_t marked_count = sylvan_varswap_p1(pos, &result);

    if (sylvan_reorder_issuccess(result) == 0) return result; // fail fast
    if (marked_count > 0) {
        // do the not so trivial cases (creates new nodes)
        sylvan_varswap_p2(&result);
        if (sylvan_reorder_issuccess(result) == 0) {
            sylvan_varswap_p3(pos, &result);
        }
    }

    // collect garbage (dead nodes)
    mrc_gc(&reorder_db->mrc, reorder_db->node_ids);

    isolated += mrc_is_var_isolated(&reorder_db->mrc, xIndex) + mrc_is_var_isolated(&reorder_db->mrc, yIndex);
    reorder_db->mrc.isolated_count += isolated;


    // swap the mappings
    reorder_db->levels.order_to_level[reorder_db->levels.level_to_order[pos]] = pos + 1;
    reorder_db->levels.order_to_level[reorder_db->levels.level_to_order[pos + 1]] = pos;
    uint32_t save = reorder_db->levels.level_to_order[pos];
    reorder_db->levels.level_to_order[pos] = reorder_db->levels.level_to_order[pos + 1];
    reorder_db->levels.level_to_order[pos + 1] = save;

    return result;
}

#if !SYLVAN_USE_LINEAR_PROBING
/**
 * Implementation of the zero phase of variable swapping.
 * For all <var+1> nodes, make <var> and rehash.
 *
 * Removes exactly the nodes that will be changed from the hash table.
 */
VOID_TASK_IMPL_4(sylvan_varswap_p0, uint32_t, var, size_t, first, size_t, count, _Atomic (reorder_result_t)*,
                 result)
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
    roaring_uint32_iterator_t *it = roaring_create_iterator(reorder_db->node_ids);
    roaring_move_uint32_iterator_equalorlarger(it, first);

    while (it->has_value && it->current_value < end) {
        mtbddnode_t node = MTBDD_GETNODE(it->current_value);
        if (mtbddnode_isleaf(node)) {
            roaring_advance_uint32_iterator(it);
            continue; // a leaf
        }
        uint32_t nvar = mtbddnode_getvariable(node);
        if (nvar == var || nvar == (var + 1)) {
            llmsset_clear_one_hash(nodes, it->current_value);
        }
        roaring_advance_uint32_iterator(it);
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
TASK_IMPL_4(size_t, sylvan_varswap_p1, uint32_t, var, size_t, first, size_t, count, _Atomic (reorder_result_t)*, result)
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

    roaring_uint32_iterator_t *it = roaring_create_iterator(reorder_db->node_ids);
    if (!roaring_move_uint32_iterator_equalorlarger(it, first)) return marked;

    while (it->has_value && it->current_value < end) {
        if (atomic_load_explicit(result, memory_order_relaxed) != SYLVAN_REORDER_SUCCESS) return marked; // fail fast
        mtbddnode_t node = MTBDD_GETNODE(it->current_value);
        if (mtbddnode_isleaf(node)) {
            roaring_advance_uint32_iterator(it);
            continue; // a leaf
        }
        uint32_t nvar = mtbddnode_getvariable(node);

        if (nvar == (var + 1)) {
            // if <var+1>, then replace with <var> and rehash
            mrc_var_nnodes_add(&reorder_db->mrc, var, 1);
            mrc_var_nnodes_add(&reorder_db->mrc, var + 1, -1);

            mtbddnode_setvariable(node, var);
            if (llmsset_rehash_bucket(nodes, it->current_value) != 1) {
                atomic_store(result, SYLVAN_REORDER_P1_REHASH_FAIL);
                return marked;
            }
            roaring_advance_uint32_iterator(it);
            continue;
        } else if (nvar != var) {
            roaring_advance_uint32_iterator(it);
            continue; // not <var> or <var+1>
        }

        // level = <var>
        if (mtbddnode_getmark(node)) {
            // marked node, remove mark and rehash (we are apparently recovering)
            mtbddnode_setmark(node, 0);
            llmsset_rehash_bucket(nodes, first);
            continue;
        }

        if (mtbddnode_ismapnode(node)) {
            MTBDD f0 = mtbddnode_getlow(node);
            if (f0 == mtbdd_false) {
                // we are at the end of a chain
                mrc_var_nnodes_add(&reorder_db->mrc, var + 1, 1);
                mrc_var_nnodes_add(&reorder_db->mrc, var, -1);

                mtbddnode_setvariable(node, var + 1);
                llmsset_rehash_bucket(nodes, it->current_value);
            } else {
                // not the end of a chain, so f0 is the next in chain
                uint32_t vf0 = mtbdd_getvar(f0);
                if (vf0 > var + 1) {
                    // next in chain wasn't <var+1>...
                    mrc_var_nnodes_add(&reorder_db->mrc, var + 1, 1);
                    mrc_var_nnodes_add(&reorder_db->mrc, var, -1);

                    mtbddnode_setvariable(node, var + 1);
                    if (!llmsset_rehash_bucket(nodes, it->current_value)) {
                        atomic_store(result, SYLVAN_REORDER_P1_REHASH_FAIL);
                        return marked;
                    }
                } else {
                    // mark for phase 2
                    mtbddnode_setmark(node, 1);
                    marked++;
                }
            }
        } else {
            if (is_node_dependent_on(node, var)) {
                // mark for phase 2
                mtbddnode_setmark(node, 1);
                marked++;
            } else {
                mrc_var_nnodes_add(&reorder_db->mrc, var + 1, 1);
                mrc_var_nnodes_add(&reorder_db->mrc, var, -1);
                mtbddnode_setvariable(node, var + 1);
                if (!llmsset_rehash_bucket(nodes, it->current_value)) {
                    atomic_store(result, SYLVAN_REORDER_P1_REHASH_FAIL);
                    return marked;
                }
            }
        }
        roaring_advance_uint32_iterator(it);
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
VOID_TASK_IMPL_3(sylvan_varswap_p2, size_t, first, size_t, count, _Atomic (reorder_result_t)*, result)
{
    // divide and conquer (if count above BLOCKSIZE)
    if (count > BLOCKSIZE) {
        size_t split = count / 2;
        SPAWN(sylvan_varswap_p2, first, split, result);
        CALL(sylvan_varswap_p2, first + split, count - split, result);
        SYNC(sylvan_varswap_p2);
        return;
    }
    // skip buckets 0 and 1
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }

    const size_t end = first + count;

    roaring_bitmap_t *tmp = roaring_bitmap_copy(reorder_db->node_ids);
    roaring_uint32_iterator_t *it = roaring_create_iterator(tmp);
    if (!roaring_move_uint32_iterator_equalorlarger(it, first)) return;

    while (it->has_value && it->current_value < end) {
        if (atomic_load_explicit(result, memory_order_relaxed) != SYLVAN_REORDER_SUCCESS) return;  // fail fast
        size_t index = it->current_value;
        roaring_advance_uint32_iterator(it);

        mtbddnode_t node = MTBDD_GETNODE(index);
        if (mtbddnode_isleaf(node)) continue; // a leaf
        if (!mtbddnode_getmark(node)) continue; // an unmarked node

        BDDVAR var = mtbddnode_getvariable(node);
        if (mtbddnode_ismapnode(node)) {
            MTBDD newf0, f1, f0, f01, f00;
            int created = 0;

            // it is a map node, swap places with next in chain
            f0 = mtbddnode_getlow(node);
            f1 = mtbddnode_gethigh(node);
            mtbddnode_t n0 = MTBDD_GETNODE(f0);
            f00 = node_getlow(f0, n0);
            f01 = node_gethigh(f0, n0);

            mrc_ref_nodes_add(&reorder_db->mrc, f0 & SYLVAN_TABLE_MASK_INDEX, -1);
            newf0 = mtbdd_varswap_makemapnode(var + 1, f00, f1, &created);
            if (newf0 == mtbdd_invalid) {
                atomic_store(result, SYLVAN_REORDER_P2_CREATE_FAIL);
                return;
            }
            if (created) {
                mrc_nnodes_add(&reorder_db->mrc, 1);
                mrc_var_nnodes_add(&reorder_db->mrc, var + 1, 1);
                mrc_ref_nodes_set(&reorder_db->mrc, newf0 & SYLVAN_TABLE_MASK_INDEX, 1);
                mrc_ref_nodes_add(&reorder_db->mrc, f00 & SYLVAN_TABLE_MASK_INDEX, 1);
                mrc_ref_nodes_add(&reorder_db->mrc, f1 & SYLVAN_TABLE_MASK_INDEX, 1);
            } else {
                mrc_ref_nodes_add(&reorder_db->mrc, newf0 & SYLVAN_TABLE_MASK_INDEX, 1);
            }

            mtbddnode_makemapnode(node, var, f0, f01);
            llmsset_rehash_bucket(nodes, index);
        } else {
            MTBDD newf1, newf0, f1, f0, f11, f10, f01, f00;
            int created0, created1 = 0;

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

            mrc_ref_nodes_add(&reorder_db->mrc, f1 & SYLVAN_TABLE_MASK_INDEX, -1);
            newf1 = mtbdd_varswap_makenode(var + 1, f01, f11, &created1);
            if (newf1 == mtbdd_invalid) {
                atomic_store(result, SYLVAN_REORDER_P2_CREATE_FAIL);
                return;
            }
            if (created1) {
                mrc_nnodes_add(&reorder_db->mrc, 1);
                mrc_var_nnodes_add(&reorder_db->mrc, var + 1, 1);
                mrc_ref_nodes_set(&reorder_db->mrc, newf1 & SYLVAN_TABLE_MASK_INDEX, 1);
                mrc_ref_nodes_add(&reorder_db->mrc, f11 & SYLVAN_TABLE_MASK_INDEX, 1);
                mrc_ref_nodes_add(&reorder_db->mrc, f01 & SYLVAN_TABLE_MASK_INDEX, 1);
            } else {
                mrc_ref_nodes_add(&reorder_db->mrc, newf1 & SYLVAN_TABLE_MASK_INDEX, 1);
            }

            mrc_ref_nodes_add(&reorder_db->mrc, f0 & SYLVAN_TABLE_MASK_INDEX, -1);
            newf0 = mtbdd_varswap_makenode(var + 1, f00, f10, &created0);
            if (newf0 == mtbdd_invalid) {
                atomic_store(result, SYLVAN_REORDER_P2_CREATE_FAIL);
                return;
            }
            if (created0) {
                mrc_nnodes_add(&reorder_db->mrc, 1);
                mrc_var_nnodes_add(&reorder_db->mrc, var + 1, 1);
                mrc_ref_nodes_set(&reorder_db->mrc, newf0 & SYLVAN_TABLE_MASK_INDEX, 1);
                mrc_ref_nodes_add(&reorder_db->mrc, f00 & SYLVAN_TABLE_MASK_INDEX, 1);
                mrc_ref_nodes_add(&reorder_db->mrc, f10 & SYLVAN_TABLE_MASK_INDEX, 1);
            } else {
                mrc_ref_nodes_add(&reorder_db->mrc, newf0 & SYLVAN_TABLE_MASK_INDEX, 1);
            }

            // update node, which also removes the mark
            mtbddnode_makenode(node, var, newf0, newf1);
            llmsset_rehash_bucket(nodes, index);
        }
    }
}

VOID_TASK_IMPL_2(sylvan_varswap_p3, uint32_t, pos, _Atomic (reorder_result_t)*, result)
{
#if SYLVAN_USE_LINEAR_PROBING
    // clear the entire table
    llmsset_clear_hashes(nodes);
#else
    // clear hashes of nodes with <var> and <var+1>
    sylvan_varswap_p0(pos, result);
#endif
    printf("Running recovery after running out of memory...\n");
    // at this point we already have nodes marked from P2 so we will unmark them now in P1
    size_t marked_count = sylvan_varswap_p1(pos, result);
    if (marked_count > 0 && sylvan_reorder_issuccess(*result)) {
        // do the not so trivial cases (but won't create new nodes this time)
        sylvan_varswap_p2(result);
    }
}