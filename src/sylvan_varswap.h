#ifndef SYLVAN_SYLVAN_VAR_SWAP_H
#define SYLVAN_SYLVAN_VAR_SWAP_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum reorder_result {
    /// the operation was aborted and rolled back
    SYLVAN_REORDER_ROLLBACK = 1,
    /// success
    SYLVAN_REORDER_SUCCESS = 0,
    //// cannot clear in phase 0, no marked nodes remaining
    SYLVAN_REORDER_P0_CLEAR_FAIL = -1,
    //// cannot rehash in phase 1, no marked nodes remaining
    SYLVAN_REORDER_P1_REHASH_FAIL = -2,
    /// cannot rehash in phase 1, and marked nodes remaining
    SYLVAN_REORDER_P1_REHASH_FAIL_MARKED = -3,
    /// cannot rehash in phase 2, no marked nodes remaining
    SYLVAN_REORDER_P2_REHASH_FAIL = -4,
    /// cannot create node in phase 2 (ergo marked nodes remaining)
    SYLVAN_REORDER_P2_CREATE_FAIL = -5,
    /// cannot rehash and cannot create node in phase 2
    SYLVAN_REORDER_P2_REHASH_AND_CREATE_FAIL = -6,
    //// cannot rehash in phase 3, maybe there are marked nodes remaining
    SYLVAN_REORDER_P3_REHASH_FAIL = -7,
    //// cannot clear in phase 3, maybe there are marked nodes remaining
    SYLVAN_REORDER_P3_CLEAR_FAIL = -8,
    /// the operation failed fast because there are no registered variables
    SYLVAN_REORDER_NO_REGISTERED_VARS = -9,
    /// the operation failed fast because the varswap was not initialised
    SYLVAN_REORDER_NOT_INITIALISED = -10,
    /// the operation failed fast because the varswap was already running
    SYLVAN_REORDER_ALREADY_RUNNING = -11,
} reorder_result_t;

/**
 * @brief Provide description for given result.
 *
 * @details Requires buffer with length at least equal to 100
 *
 * @param tag
 * @param result based on which the description is determined
 * @param buf buffer into which the description will be copied
 * @param buf_len
 */
void sylvan_reorder_resdescription(reorder_result_t result, char *buf, size_t buf_len);

static inline int sylvan_reorder_issuccess(reorder_result_t result)
{
    return result == SYLVAN_REORDER_SUCCESS ||
    result == SYLVAN_REORDER_NOT_INITIALISED ||
    result == SYLVAN_REORDER_ROLLBACK;
}

void sylvan_print_reorder_res(reorder_result_t result);

 /**
  * @brief Swaps two consecutive variables in the entire forest.
  *
  * Variable swapping consists of two phases. The first phase performs
  * variable swapping on all simple cases. The cases that require node
  * lookups are left marked. The second phase then fixes the marked nodes.
  *
  * If the "recovery" parameter is set, then phase 1 only rehashes nodes that are <var+1>,
  * and phase 2 will not abort on the first error, but try to finish as many nodes as possible.
  *
  * We assume there are only BDD/MTBDD/MAP nodes in the forest.
  * The operation is not thread-safe, so make sure no other Sylvan operations are
  * done when performing variable swapping.
  *
  * It is recommended to clear the cache and perform clear-and-mark (the first part of garbage
  * collection, before resizing and rehashing) before running sylvan_varswap.
  *
  * If the parameter <recovery> is set, then phase 1 only rehashes nodes that have variable "var+1".
  * Phase 2 will not abort on the first error, but try to finish as many nodes as possible.
  *
  * See the implementation of sylvan_varswap for notes on recovery/rollback.
  *
  * @param var variable to swap
  * @return varswap_res_t
  *
  */
TASK_DECL_1(reorder_result_t, sylvan_varswap, uint32_t)
#define sylvan_varswap(pos) CALL(sylvan_varswap, pos)
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_VAR_SWAP_H
