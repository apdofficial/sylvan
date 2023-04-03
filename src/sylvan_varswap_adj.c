#include <sylvan_int.h>
#include <sys/time.h>
#include "sylvan_varswap_adj.h"
#include "sylvan_levels.h"

#define ENABLE_ERROR_LOGS   1 // critical errors that cause varswap to fail
#define ENABLE_INFO_LOGS    1 // useful information w.r.t. dynamic reordering
#define ENABLE_DEBUG_LOGS   0 // useful only for development purposes

#define LOG_ERROR(s, ...)   { if (ENABLE_ERROR_LOGS) fprintf(stderr, s,  ##__VA_ARGS__); }
#define LOG_DEBUG(s, ...)   { if (ENABLE_DEBUG_LOGS) fprintf(stdout, s,  ##__VA_ARGS__); }
#define LOG_INFO(s, ...)    { if (ENABLE_INFO_LOGS)  fprintf(stdout, s,  ##__VA_ARGS__); }


#if SYLVAN_USE_CHAINING
VOID_TASK_DECL_5(sylvan_varswap_p0_adj, uint32_t, uint32_t,size_t, size_t, volatile varswap_res_t*);
/**
 * @brief Implementation of the zero phase of variable swapping.
 * @details Clear hashes of nodes with variables <x> and <y>
 * Removes exactly the nodes that will be changed from the hash table.
 */
#define sylvan_varswap_p0_adj(x, y, first, count, result) CALL(sylvan_varswap_p0_adj, x, y, first, count, result)
#else
#define sylvan_varswap_p0() llmsset_clear_hashes(nodes);
#endif

#if SYLVAN_USE_CHAINING

TASK_DECL_5(uint64_t, sylvan_varswap_p1_adj, uint32_t, uint32_t, size_t, size_t, volatile varswap_res_t*);
#define sylvan_varswap_p1_adj(x, y, first, count, result) CALL(sylvan_varswap_p1_adj, x, y, first, count, result)

VOID_TASK_DECL_5(sylvan_varswap_p2_adj, uint32_t, uint32_t, size_t, size_t, volatile varswap_res_t*);
#define sylvan_varswap_p2_adj(x, y, first, count, result) CALL(sylvan_varswap_p2_adj, x, y, first, count, result)

/**
 * Custom makenode that doesn't trigger garbage collection.
 * Instead, returns mtbdd_invalid if we can't create the node.
 */
MTBDD mtbdd_varswap_makenode_adj(uint32_t var, MTBDD low, MTBDD high)
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
MTBDD mtbdd_varswap_makemapnode_adj(uint32_t var, MTBDD low, MTBDD high)
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

VOID_TASK_IMPL_5(sylvan_varswap_p0_adj,
                 uint32_t, x, /** variable label **/
                 uint32_t, y, /** variable label **/
                 size_t, first, /** index in the unique table **/
                 size_t, count, /** index in the unique table **/
                 volatile varswap_res_t*, result)
{
    // divide and conquer (if count above BLOCKSIZE)
    if (count > BLOCKSIZE) {
        SPAWN(sylvan_varswap_p0_adj, x, y, first, count / 2, result);
        CALL(sylvan_varswap_p0_adj, x, y, first + count / 2, count - count / 2, result);
        SYNC(sylvan_varswap_p0_adj);
        return;
    }

    // skip buckets 0 and 1
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }

    const size_t end = first + count;

    for (; first < end; first++) {
        if (!llmsset_is_marked(nodes, first)) continue; // an unused bucket
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) continue; // a leaf
        uint32_t nvar = mtbddnode_getvariable(node);
        if (nvar == x || nvar == y) llmsset_clear_one(nodes, first);
    }
}

#endif

TASK_IMPL_5(uint64_t, sylvan_varswap_p1_adj,
            uint32_t, x, /** variable label **/
            uint32_t, y, /** variable label **/
            size_t, first,  /** starting node index in the unique table **/
            size_t, count, /** number of nodes to visit form the starting index **/
            volatile varswap_res_t*, result)
{
    // divide and conquer (if count above BLOCKSIZE)
    if (count > BLOCKSIZE) {
        SPAWN(sylvan_varswap_p1_adj, x, y, first, count / 2, result);
        uint64_t res1 = CALL(sylvan_varswap_p1_adj, x, y, first + count / 2, count - count / 2, result);
        uint64_t res2 = SYNC(sylvan_varswap_p1_adj);
        return res1 + res2;
    }

    // count number of marked
    uint64_t marked = 0;

    // skip buckets 0 and 1
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }

    const size_t end = first + count;

    for (; first < end; first++) {
        if (!llmsset_is_marked(nodes, first)) continue; // an unused bucket
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) continue; // a leaf
        uint32_t nvar = mtbddnode_getvariable(node);

        if (nvar == y) {
            // if <var+1>, then replace with <var> and rehash
            mtbddnode_setvariable(node, x);
            if (llmsset_rehash_bucket(nodes, first) != 1) {
                LOG_ERROR("sylvan_varswap_p1_adj: llmsset_rehash_bucket(%zu) failed!\n", first);
                *result = SYLVAN_VARSWAP_P1_REHASH_FAIL;
            }
            continue;
        } else if (nvar != x) {
            continue; // not <var> or <var+1>
        }

        // nvar == <var>
        if (mtbddnode_getp2mark(node)) {
            // marked node, remove mark and rehash (we are apparently recovering)
            mtbddnode_setp2mark(node, 0);
            llmsset_rehash_bucket(nodes, first);
            if (llmsset_rehash_bucket(nodes, first) != 1) {
                LOG_ERROR("sylvan_varswap_p1_adj:recovery: llmsset_rehash_bucket(%zu) failed!\n", first);
                *result = SYLVAN_VARSWAP_P1_REHASH_FAIL;
            }
            continue;
        }

        if (mtbddnode_ismapnode(node)) {
            MTBDD f0 = mtbddnode_getlow(node);
            if (f0 == mtbdd_false) {
                // we are at the end of a chain
                mtbddnode_setvariable(node, y);
                llmsset_rehash_bucket(nodes, first);
            } else {
                // not the end of a chain, so f0 is the next in chain
                uint32_t vf0 = mtbdd_getvar(f0);
                if (vf0 > y) {
                    // next in chain wasn't <var+1>...
                    mtbddnode_setvariable(node, y);
                    if (llmsset_rehash_bucket(nodes, first) != 1) {
                        LOG_ERROR("sylvan_varswap_p1_adj: llmsset_clear_one(%zu) failed!\n", first);
                        *result = SYLVAN_VARSWAP_P1_REHASH_FAIL;
                    }
                } else {
                    // mark for phase 2
                    mtbddnode_setp2mark(node, 1);
                    marked++;
                }
            }
        } else {
            int p2 = 0;
            MTBDD f0 = mtbddnode_getlow(node);
            if (!mtbdd_isleaf(f0)) {
                uint32_t vf0 = mtbdd_getvar(f0);
                if (vf0 == x || vf0 == y) p2 = 1;
            }
            if (!p2) {
                MTBDD f1 = mtbddnode_gethigh(node);
                if (!mtbdd_isleaf(f1)) {
                    uint32_t vf1 = mtbdd_getvar(f1);
                    if (vf1 == x || vf1 == y) p2 = 1;
                }
            }
            if (p2) {
                // mark for phase 2
                mtbddnode_setp2mark(node, 1);
                marked++;
            } else {
                mtbddnode_setvariable(node, y);
                if (llmsset_rehash_bucket(nodes, first) != 1) {
                    LOG_ERROR("sylvan_varswap_p1_adj: llmsset_rehash_bucket(%zu) failed!\n", first);
                    *result = SYLVAN_VARSWAP_P1_REHASH_FAIL;
                }
            }
        }
    }
    return marked;
}

VOID_TASK_IMPL_5(sylvan_varswap_p2_adj,
                 uint32_t, x,
                 uint32_t, y,
                 size_t, first,
                 size_t, count,
                 volatile varswap_res_t*, result)
{
    // divide and conquer (if count above BLOCKSIZE)
    if (count > BLOCKSIZE) {
        SPAWN(sylvan_varswap_p2_adj, x, y, first, count / 2, result);
        CALL(sylvan_varswap_p2_adj, x, y, first + count / 2, count - count / 2, result);
        SYNC(sylvan_varswap_p2_adj);
        return;
    }

    /* skip buckets 0 and 1 */
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }

    // first, find all nodes that need to be replaced
    const size_t end = first + count;

    for (; first < end; first++) {
        if (*result != SYLVAN_VARSWAP_SUCCESS) return; // the table is full
        if (!llmsset_is_marked(nodes, first)) continue; // an unused bucket

        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) continue; // a leaf
        if (!mtbddnode_getp2mark(node)) continue; // an unmarked node

        if (mtbddnode_ismapnode(node)) {
            // it is a map node, swap places with next in chain
            MTBDD f0 = mtbddnode_getlow(node);
            MTBDD f1 = mtbddnode_gethigh(node);
            mtbddnode_t n0 = MTBDD_GETNODE(f0);
            MTBDD f00 = node_getlow(f0, n0);
            MTBDD f01 = node_gethigh(f0, n0);
            f0 = mtbdd_varswap_makemapnode_adj(y, f00, f1);
            if (f0 == mtbdd_invalid) {
                LOG_ERROR("sylvan_varswap_p2_adj: SYLVAN_VARSWAP_P2_CREATE_FAIL\n");
                *result = SYLVAN_VARSWAP_P2_CREATE_FAIL;
                return;
            } else {
                mtbddnode_makemapnode(node, x, f0, f01);
                llmsset_rehash_bucket(nodes, first);
            }
        } else {
            // obtain cofactors
            MTBDD f0 = mtbddnode_getlow(node);
            MTBDD f1 = mtbddnode_gethigh(node);
            MTBDD f00, f01, f10, f11;
            f00 = f01 = f0;
            if (!mtbdd_isleaf(f0)) {
                mtbddnode_t n0 = MTBDD_GETNODE(f0);
                if (mtbddnode_getvariable(n0) == x) {
                    f00 = node_getlow(f0, n0);
                    f01 = node_gethigh(f0, n0);
                }
            }
            f10 = f11 = f1;
            if (!mtbdd_isleaf(f1)) {
                mtbddnode_t n1 = MTBDD_GETNODE(f1);
                if (mtbddnode_getvariable(n1) == x) {
                    f10 = node_getlow(f1, n1);
                    f11 = node_gethigh(f1, n1);
                }
            }

            // compute new f0 and f1
            f0 = mtbdd_varswap_makenode_adj(y, f00, f10);
            f1 = mtbdd_varswap_makenode_adj(y, f01, f11);
            if (f0 == mtbdd_invalid || f1 == mtbdd_invalid) {
                *result = SYLVAN_VARSWAP_P2_CREATE_FAIL;
                LOG_ERROR("sylvan_varswap_p2: SYLVAN_VARSWAP_P2_CREATE_FAIL\n");
                return;
            } else {
                // update node, which also removes the mark
                mtbddnode_makenode(node, x, f0, f1);
                llmsset_rehash_bucket(nodes, first);
            }
        }
    }
}

TASK_IMPL_2(varswap_res_t, sylvan_varswap_adj, uint32_t, x, uint32_t, y)
{
    varswap_res_t result = SYLVAN_VARSWAP_SUCCESS;

    // ensure that the cache is cleared
    sylvan_clear_cache();

#if SYLVAN_USE_CHAINING
    // first clear hashes of nodes with va variables <x> and <y>
    sylvan_varswap_p0_adj(x, y, 0, nodes->table_size, &result);
#else
    // clear the entire table
    sylvan_varswap_p0();
#endif

    // handle all trivial cases, mark cases that are not trivial
    uint64_t marked_count = sylvan_varswap_p1_adj(x, y, 0, nodes->table_size, &result);

    if (marked_count > 0) {
        // do the not so trivial cases (creates new nodes)
        sylvan_varswap_p2_adj(x, y, 0, nodes->table_size, &result);

        if (result != SYLVAN_VARSWAP_SUCCESS) {
            LOG_INFO("Recovery time!\n");

#if SYLVAN_USE_CHAINING
            // first clear hashes of nodes with <var> and <var+1>
            sylvan_varswap_p0_adj(x, y, 0, nodes->table_size, &result);
#else
            // clear the entire table
            sylvan_varswap_p0();
#endif

            // handle all trivial cases, mark cases that are not trivial
            marked_count = sylvan_varswap_p1_adj(x, y, 0, nodes->table_size, &result);

            if (marked_count > 0 && result == SYLVAN_VARSWAP_SUCCESS) {
                // do the not so trivial cases (but won't create new nodes this time)
                sylvan_varswap_p2_adj(x, y, 0, nodes->table_size, &result);
                if (result != SYLVAN_VARSWAP_SUCCESS) {
                    // actually, we should not see this!
                    LOG_ERROR("sylvan: recovery varswap failed!\n");
                    return SYLVAN_VARSWAP_P2_REHASH_AND_CREATE_FAIL;
                }
            } else {
                return SYLVAN_VARSWAP_P1_REHASH_FAIL_MARKED;
            }

            LOG_INFO("Recovery good.\n");
            return SYLVAN_VARSWAP_ROLLBACK;
        }
    }

    mtbdd_varswap_adj(x, y);

    sylvan_clear_and_mark();
    sylvan_rehash_all();

    return result;
}