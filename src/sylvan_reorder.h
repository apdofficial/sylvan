#ifndef SYLVAN_VAR_REORDER_H
#define SYLVAN_VAR_REORDER_H

#include "sylvan_varswap.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
  @brief Type of reordering algorithm.
*/
typedef enum {
    SYLVAN_REORDER_SIFT,
    SYLVAN_REORDER_BOUNDED_SIFT,
} reordering_type_t;

typedef struct sifting_state
{
    uint32_t    pos;
    int         size;
    uint32_t    best_pos;
    int         best_size;
    uint32_t    low;
    uint32_t    high;
    int         use_bounds;
} sifting_state_t;

typedef int (*re_term_cb)();

/**
 * @brief Callback type
 */
LACE_TYPEDEF_CB(void, re_hook_cb);

/**
 * @brief Add a hook that is called before dynamic variable reordering begins.
 */
void sylvan_re_hook_prere(re_hook_cb callback);

/**
 * @brief Add a hook that is called after dynamic variable reordering is finished.
 */
void sylvan_re_hook_postre(re_hook_cb callback);

/**
 * @brief Add a hook that is called after dynamic variable reordering managed to reduce number of nodes.
 */
void sylvan_re_hook_progre(re_hook_cb callback);

/**
 * @brief Add a hook that is called regularly to see whether sifting should terminate.
 */
void sylvan_re_hook_termre(re_term_cb callback);

// opaque type
typedef struct reorder_config *reorder_config_t;

/**
 * @brief Initialize the dynamic variable reordering.
 */
void sylvan_init_reorder(void);

/**
 * @brief Quit the dynamic variable reordering.
 */
void sylvan_quit_reorder(void);

/**
 * @brief Get the reorder configuration.
 */
reorder_config_t sylvan_get_reorder_config();

/**
 * @brief Set threshold for the number of nodes per level to consider during the reordering.
 * @details If the number of nodes per level is less than the threshold, the level is skipped during the reordering.
 * @param threshold The threshold for the number of nodes per level.
*/
void sylvan_set_reorder_nodes_threshold(uint32_t threshold);

/**
 * @brief Set the maximum growth coefficient.
 * @details The maximum growth coefficient is used to calculate the maximum growth of the number of nodes during the reordering.
 *        If the number of nodes grows more than the maximum growth coefficient , sift up/down is terminated.
 * @param max_growth The maximum growth coefficient.
*/
void sylvan_set_reorder_maxgrowth(float max_growth);

/**
 * @brief Set the maximum number of swaps per sifting.
 * @param max_swap The maximum number of swaps per sifting.
*/
void sylvan_set_reorder_maxswap(uint32_t max_swap);

/**
 * @brief Set the maximum number of vars swapped per sifting.
 * @param max_var The maximum number of vars swapped per sifting.
 */
void sylvan_set_reorder_maxvar(uint32_t max_var);

/**
 * @brief Set the time limit in minutes for the reordering.
 * @param time_limit The time limit for the reordering.
 */
void sylvan_set_reorder_timelimit_min(double time_limit);

/**
 * @brief Set the time limit in seconds for the reordering.
 * @param time_limit The time limit for the reordering.
 */
void sylvan_set_reorder_timelimit_sec(double time_limit);

/**
 * @brief Set the time limit in milliseconds for the reordering.
 * @param time_limit The time limit for the reordering.
 */
void sylvan_set_reorder_timelimit_ms(double time_limit);

/**
 * @brief Set the the flag to print the progress of the reordering.
 * @param verbose The flag to print the progress of the reordering.
 */
void sylvan_set_reorder_verbose(int is_verbose);

/**
 * @brief Set the the flag to print the progress of the reordering.
 * @param verbose The flag to print the progress of the reordering.
 */
void sylvan_set_reorder_type(reordering_type_t type);

/**
 * @brief Sift given variable up from its current level to the target level.
 * @sideeffect order of variables is changed
 */
TASK_DECL_1(reorder_result_t, sylvan_siftdown, sifting_state_t*);
#define sylvan_siftdown(state) CALL(sylvan_siftdown, state)

/**
 * @brief Sift given variable down from its current level to the target level.
 * @sideeffect order of variables is changed
 */
TASK_DECL_1(reorder_result_t, sylvan_siftup, sifting_state_t*);
#define sylvan_siftup(state) CALL(sylvan_siftup, state)

/**
 * @brief Sift a variable to its best level.
 * @param pos - variable to sift
 * @param target_pos - target position (w.r.t. dynamic variable reordering)
 */
TASK_DECL_1(reorder_result_t, sylvan_siftback, sifting_state_t*);
#define sylvan_siftback(state) CALL(sylvan_siftback, state)

/**
 * @brief Reduce the heap size in the entire forest.
 *
 * @details Implementation of Rudell's sifting algorithm.
 * This function performs stop-the-world operation similar to garbage collection.
 * It proceeds as follows:
 * 1. Order all the variables according to the number of entries in each unique table.
 * 2. Sift the variable up and down, remembering each time the total size of the bdd size.
 * 3. Select the best permutation.
 * 4. Repeat 2 and 3 for all variables in given range.
 *
 * @sideeffect order of variables is changed, mappings level -> order and order -> level are updated
 */
void sylvan_reduce_heap(reordering_type_t type);

/**
 * @brief Maybe reduce the heap size in the entire forest.
 */
void sylvan_test_reduce_heap();

TASK_DECL_1(reorder_result_t, sylvan_reorder_perm, const uint32_t*);
/**
  @brief Reorder the variables in the BDDs according to the given permutation.

  @details The permutation is an array of BDD labels, where the i-th element is the label
  of the variable that should be moved to position i. The size
  of the array should be equal or greater to the number of variables
  currently in use.
 */
#define sylvan_reorder_perm(permutation)  RUNEX(sylvan_reorder_perm, permutation)

#define sylvan_pre_reorder() RUN(sylvan_pre_reorder)
VOID_TASK_DECL_0(sylvan_pre_reorder)

#define sylvan_post_reorder(before_size, leaf_count, type) RUN(sylvan_post_reorder, before_size, leaf_count, type)
VOID_TASK_DECL_3(sylvan_post_reorder, size_t, size_t, reordering_type_t)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_VAR_REORDER_H

