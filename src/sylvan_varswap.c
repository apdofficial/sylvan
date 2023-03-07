#include <sylvan_int.h>
#include <sys/time.h>
#include "sylvan_varswap.h"
#include "sylvan_levels.h"

#define ENABLE_ERROR_LOGS   0 // critical errors that cause varswap to fail
#define ENABLE_INFO_LOGS    0 // useful information w.r.t. varswap
#define ENABLE_DEBUG_LOGS   0 // useful only for development purposes

/* Obtain current wallclock time */
static double wctime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}
static double t_start = 0.0;

#define LOG_ERROR(s, ...)   { if (ENABLE_ERROR_LOGS) fprintf(stderr, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); }
#define LOG_DEBUG(s, ...)   { if (ENABLE_DEBUG_LOGS) fprintf(stdout, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); }
#define LOG_INFO(s, ...)    { if (ENABLE_INFO_LOGS)  fprintf(stdout, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); }

void sylvan_varswap_init(void)
{
    t_start = wctime();
}

void sylvan_varswap_quit(void)
{
    t_start = 0.0;
}


VOID_TASK_DECL_4(sylvan_varswap_p0, uint32_t, size_t, size_t, volatile sylvan_varswap_res_t*);
/*!
   \brief Adjacent variable swap phase 0
   \details Clear hashes of nodes with var and var+1, Removes exactly the nodes
   that will be changed from the hash table.
   \param var variable label to be swapped
   \param first starting index in the unique table
   \param count ending index number of nodes to consider in the unique table
   \param result pointer to sylvan_var_swap_res_t
*/
#define sylvan_varswap_p0(var, first, count, result) CALL(sylvan_varswap_p0, var, first, count, result)


TASK_DECL_4(uint64_t, sylvan_varswap_p1, uint32_t, size_t, size_t, volatile sylvan_varswap_res_t*);
/*!
   \brief Adjacent variable swap phase 1
   \details Handle all trivial cases where no node is created, mark cases that are not trivial.
   \param var variable label to be swapped
   \param first starting index in the unique table
   \param count ending index number of nodes to consider in the unique table
   \param result pointer to sylvan_var_swap_res_t
   \return number of nodes that were marked
*/
#define sylvan_varswap_p1(var, first, count, result) CALL(sylvan_varswap_p1, var, first, count, result)


VOID_TASK_DECL_4(sylvan_varswap_p2, uint32_t, size_t, size_t, volatile sylvan_varswap_res_t*);
/*!
   \brief Adjacent variable swap phase 2
   \details Handle the not so trivial cases. (creates new nodes)
   \param var variable label to be swapped
   \param first starting index in the unique table
   \param count ending index number of nodes to consider in the unique table
   \param result pointer to sylvan_var_swap_res_tw
*/
#define sylvan_varswap_p2(var, first, count, result) CALL(sylvan_varswap_p2, var, first, count, result)

void sylvan_print_varswap_res(char *tag, sylvan_varswap_res_t result)
{
    size_t msgLen = 100;
    char msg[msgLen];
    switch (result) {
        case SYLVAN_VARSWAP_ROLLBACK:
            sprintf(msg, "SYLVAN_VARSWAP_ROLLBACK: the operation was aborted and rolled back");
            break;
        case SYLVAN_VARSWAP_SUCCESS:
            sprintf(msg, "SYLVAN_VARSWAP_SUCCESS: success");
            break;
        case SYLVAN_VARSWAP_P1_REHASH_FAIL:
            sprintf(msg, "SYLVAN_VARSWAP_P1_REHASH_FAIL: cannot rehash in phase 1, no marked nodes remaining");
            break;
        case SYLVAN_VARSWAP_P1_REHASH_FAIL_MARKED:
            sprintf(msg, "SYLVAN_VARSWAP_P1_REHASH_FAIL_MARKED: cannot rehash in phase 1, and marked nodes remaining");
            break;
        case SYLVAN_VARSWAP_P2_REHASH_FAIL:
            sprintf(msg, "SYLVAN_VARSWAP_P2_REHASH_FAIL: cannot rehash in phase 2, no marked nodes remaining");
            break;
        case SYLVAN_VARSWAP_P2_CREATE_FAIL:
            sprintf(msg, "SYLVAN_VARSWAP_P2_CREATE_FAIL: cannot create node in phase 2 (ergo marked nodes remaining)");
            break;
        case SYLVAN_VARSWAP_P2_REHASH_AND_CREATE_FAIL:
            sprintf(msg, "SYLVAN_VARSWAP_P2_REHASH_AND_CREATE_FAIL: cannot rehash and cannot create node in phase 2");
            break;
        default:
            sprintf(msg, "SYLVAN_VARSWAP: UNKNOWN ERROR");
            break;
    }
    printf("%s: %s\n", tag, msg);
}

/**
 * Custom makenode that doesn't trigger garbage collection.
 * Instead, returns mtbdd_invalid if we can't create the node.
 */
MTBDD mtbdd_varswap_makenode(uint32_t var, MTBDD low, MTBDD high)
{
    struct mtbddnode n;
    uint64_t index;
    int mark, created;

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
MTBDD mtbdd_varswap_makemapnode(uint32_t var, MTBDD low, MTBDD high)
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

TASK_IMPL_2(sylvan_varswap_res_t, sylvan_varswap, uint32_t, var, int, recovery)
{
    sylvan_varswap_res_t result = SYLVAN_VARSWAP_SUCCESS;

    // first clear hashes of nodes with <var> and <var+1>
    sylvan_varswap_p0(var, 0, nodes->table_size, &result);
    // handle all trivial cases, mark cases that are not trivial
    uint64_t marked_count = sylvan_varswap_p1(var, 0, nodes->table_size, &result);

    if (marked_count != 0) {
        // do the not so trivial cases (creates new nodes)
        sylvan_varswap_res_t result = SYLVAN_VARSWAP_SUCCESS;
        sylvan_varswap_p2(var, 0, nodes->table_size, &result);

        if (result != SYLVAN_VARSWAP_SUCCESS) return result;
    }
    return SYLVAN_VARSWAP_SUCCESS;
    (void)recovery;
}

TASK_IMPL_1(sylvan_varswap_res_t, sylvan_simple_varswap, uint32_t, var)
{
    sylvan_varswap_res_t result = SYLVAN_VARSWAP_SUCCESS;

    // ensure that the cache is cleared
    sylvan_clear_cache();
    // first clear hashes of nodes with <var> and <var+1>
    sylvan_varswap_p0(var, 0, nodes->table_size, &result);
    // handle all trivial cases, mark cases that are not trivial
    uint64_t marked_count = sylvan_varswap_p1(var, 0, nodes->table_size, &result);

    if (marked_count > 0) {
        // do the not so trivial cases (creates new nodes)
        sylvan_varswap_p2(var, 0, nodes->table_size, &result);

        if (result != SYLVAN_VARSWAP_SUCCESS) {
            LOG_INFO("Recovery time!\n");
            // clear hashes again of nodes with <var> and <var+1>
            sylvan_varswap_p0(var, 0, nodes->table_size, &result);
            // handle all trivial cases, mark cases that are not trivial
            marked_count = sylvan_varswap_p1(var, 0, nodes->table_size, &result);

            if (marked_count > 0 && result == SYLVAN_VARSWAP_SUCCESS){
                // do the not so trivial cases (but won't create new nodes this time)
                sylvan_varswap_p2(var, 0, nodes->table_size, &result);
                if (result != SYLVAN_VARSWAP_SUCCESS) {
                    // actually, we should not see this!
                    LOG_ERROR("sylvan: recovery varswap failed!\n");
                    return SYLVAN_VARSWAP_P2_REHASH_AND_CREATE_FAIL;
                }
            }else{
                return SYLVAN_VARSWAP_P1_REHASH_FAIL_MARKED;
            }

            LOG_INFO("Recovery good.\n");
            return SYLVAN_VARSWAP_ROLLBACK;
        }
    }

    mtbdd_levels_varswap(var);

    // do some kind of clear-and-mark ???
    sylvan_clear_and_mark();
    sylvan_rehash_all();

    return result;
}

/**
 * Implementation of the zero phase of variable swapping.
 * For all <var+1> nodes, make <var> and rehash.
 *
 * Removes exactly the nodes that will be changed from the hash table.
 */
VOID_TASK_IMPL_4(sylvan_varswap_p0,
                 uint32_t, var, /** variable label **/
                 size_t, first, /** index in the unique table **/
                 size_t, count, /** index in the unique table **/
                 volatile sylvan_varswap_res_t*, result
){
    // go recursive if count above BLOCKSIZE
    if (count > BLOCKSIZE) {
        SPAWN(sylvan_varswap_p0, var, first, count / 2, result);
        CALL(sylvan_varswap_p0, var, first + count / 2, count - count / 2, result);
        SYNC(sylvan_varswap_p0);
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
        if (nvar == var || nvar == (var+1)) {
            if (llmsset_clear_one(nodes, first) != 0){
                LOG_ERROR("sylvan_varswap_p0: llmsset_clear_one(nodes, %u) failed!\n", nvar);
//                *result = SYLVAN_VARSWAP_P0_CLEAR_FAIL;
            }
        }
    }
}


/**
 * Implementation of the first phase of variable swapping.
 * For all <var+1> nodes, make <var> and rehash.
 * For all <var> nodes not depending on <var+1>, make <var+1> and rehash.
 * For all <var> nodes depending on <var+1>, stay <var> and mark. (no rehash)
 * Returns number of marked nodes left.
 *
 * This algorithm is also used for the recovery phase 1. This is an identical
 * phase, except marked <var> nodes are unmarked. If the recovery flag is set, then only <var+1>
 * nodes are rehashed.
 */
TASK_IMPL_4(uint64_t, sylvan_varswap_p1,
            uint32_t, var, /** variable label **/
            size_t, first,  /** starting node index in the unique table **/
            size_t, count, /** number of nodes to visit form the starting index **/
            volatile sylvan_varswap_res_t*, result
){
    // Divide-and-conquer if count above BLOCKSIZE
    if (count > BLOCKSIZE) {
        SPAWN(sylvan_varswap_p1, var, first, count / 2, result);
        uint64_t res1 = CALL(sylvan_varswap_p1, var, first + count / 2, count - count / 2, result);
        uint64_t res2 = SYNC(sylvan_varswap_p1);
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

        if (nvar == (var+1)) {
            // if <var+1>, then replace with <var> and rehash
            mtbddnode_setvariable(node, var);
            if (llmsset_rehash_bucket(nodes, first) != 0){
                LOG_ERROR("sylvan_varswap_p1: llmsset_clear_one(nodes, %zu) failed!\n", first);
//                *result = SYLVAN_VARSWAP_P1_REHASH_FAIL;
            }
            continue;
        } else if (nvar != var) {
            continue; // not <var> or <var+1>
        }

        // nvar == <var>
        if (mtbddnode_getmark(node)) {
            // marked node, remove mark and rehash (we are apparently recovering)
            mtbddnode_setmark(node, 0);
            llmsset_rehash_bucket(nodes, first);
            if (llmsset_rehash_bucket(nodes, first) != 0){
                LOG_ERROR("sylvan_varswap_p1:recovery: llmsset_clear_one(nodes, %zu) failed!\n", first);
//                *result = SYLVAN_VARSWAP_P1_REHASH_FAIL;
            }
            continue;
        }

        if (mtbddnode_ismapnode(node)) {
            MTBDD f0 = mtbddnode_getlow(node);
            if (f0 == mtbdd_false) {
                // we are at the end of a chain
                mtbddnode_setvariable(node, var+1);
                llmsset_rehash_bucket(nodes, first);
            } else {
                // not the end of a chain, so f0 is the next in chain
                uint32_t vf0 = mtbdd_getvar(f0);
                if (vf0 > var+1) {
                    // next in chain wasn't <var+1>...
                    mtbddnode_setvariable(node, var+1);
                    if (llmsset_rehash_bucket(nodes, first) != 0){
                        LOG_ERROR("sylvan_varswap_p1: llmsset_clear_one(nodes, %zu) failed!\n", first);
//                        *result = SYLVAN_VARSWAP_P1_REHASH_FAIL;
                    }
                } else {
                    // mark for phase 2
                    mtbddnode_setmark(node, 1);
                    marked++;
                }
            }
        } else {
            int p2 = 0;
            MTBDD f0 = mtbddnode_getlow(node);
            if (!mtbdd_isleaf(f0)) {
                uint32_t vf0 = mtbdd_getvar(f0);
                if (vf0 == var || vf0 == var+1) p2 = 1;
            }
            if (!p2) {
                MTBDD f1 = mtbddnode_gethigh(node);
                if (!mtbdd_isleaf(f1)) {
                    uint32_t vf1 = mtbdd_getvar(f1);
                    if (vf1 == var || vf1 == var+1) p2 = 1;
                }
            }
            if (p2) {
                // mark for phase 2
                mtbddnode_setmark(node, 1);
                marked++;
            } else {
                mtbddnode_setvariable(node, var+1);
                if (llmsset_rehash_bucket(nodes, first) != 0){
                    LOG_ERROR("sylvan_varswap_p1: llmsset_clear_one(nodes, %zu) failed!\n", first);
//                    *result = SYLVAN_VARSWAP_P1_REHASH_FAIL;
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
                 volatile sylvan_varswap_res_t*, result
){
    /* divide and conquer (if count above BLOCKSIZE) */
    if (count > BLOCKSIZE) {
        SPAWN(sylvan_varswap_p2, var, first, count / 2, result);
        CALL(sylvan_varswap_p2, var, first + count / 2, count - count / 2, result);
        SYNC(sylvan_varswap_p2);
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
        if (!mtbddnode_getmark(node)) continue; // an unmarked node

        if (mtbddnode_ismapnode(node)) {
            // it is a map node, swap places with next in chain
            MTBDD f0 = mtbddnode_getlow(node);
            MTBDD f1 = mtbddnode_gethigh(node);
            mtbddnode_t n0 = MTBDD_GETNODE(f0);
            MTBDD f00 = node_getlow(f0, n0);
            MTBDD f01 = node_gethigh(f0, n0);
            f0 = mtbdd_varswap_makemapnode(var+1, f00, f1);
            if (f0 == mtbdd_invalid) {
                LOG_ERROR("sylvan_varswap_p2: SYLVAN_VARSWAP_P2_CREATE_FAIL\n");
                *result = SYLVAN_VARSWAP_P2_CREATE_FAIL;
                return;
            } else {
                mtbddnode_makemapnode(node, var, f0, f01);
                if (llmsset_rehash_bucket(nodes, first) != 0){
                    LOG_ERROR("sylvan_varswap_p2: llmsset_clear_one(nodes, %zu) failed!\n", first);
//                    *result = SYLVAN_VARSWAP_P2_REHASH_FAIL;
                }
            }
        } else {
            // obtain cofactors
            MTBDD f0 = mtbddnode_getlow(node);
            MTBDD f1 = mtbddnode_gethigh(node);
            MTBDD f00, f01, f10, f11;
            f00 = f01 = f0;
            if (!mtbdd_isleaf(f0)) {
                mtbddnode_t n0 = MTBDD_GETNODE(f0);
                if (mtbddnode_getvariable(n0) == var) {
                    f00 = node_getlow(f0, n0);
                    f01 = node_gethigh(f0, n0);
                }
            }
            f10 = f11 = f1;
            if (!mtbdd_isleaf(f1)) {
                mtbddnode_t n1 = MTBDD_GETNODE(f1);
                if (mtbddnode_getvariable(n1) == var) {
                    f10 = node_getlow(f1, n1);
                    f11 = node_gethigh(f1, n1);
                }
            }

            // compute new f0 and f1
            f0 = mtbdd_varswap_makenode(var+1, f00, f10);
            f1 = mtbdd_varswap_makenode(var+1, f01, f11);
            if (f0 == mtbdd_invalid || f1 == mtbdd_invalid) {
                *result = SYLVAN_VARSWAP_P2_CREATE_FAIL;
                LOG_ERROR("sylvan_varswap_p2: SYLVAN_VARSWAP_P2_CREATE_FAIL\n");
                return;
            } else {
                // update node, which also removes the mark
                mtbddnode_makenode(node, var, f0, f1);
                if (llmsset_rehash_bucket(nodes, first) != 0){
                    LOG_ERROR("sylvan_varswap_p2: llmsset_clear_one(nodes, %zu) failed!\n", first);
//                    *result = SYLVAN_VARSWAP_P2_REHASH_FAIL;
                }
            }
        }
    }
}
