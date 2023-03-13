/*
 * Copyright 2016 Tom van Dijk, Johannes Kepler University Linz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SYLVAN_VAR_REORDER_H
#define SYLVAN_VAR_REORDER_H

#include "sylvan_varswap.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
   \brief Type of termination handler.
*/
typedef int (*reorder_termination_cb)();

void sylvan_init_reorder(void);

void sylvan_quit_reorder(void);

__attribute__((unused)) __attribute__((unused))
void sylvan_set_reorder_terminationcb(reorder_termination_cb callback);


__attribute__((unused)) 
/**
 * @brief Set threshold for the number of nodes per level to consider during the reordering.
 * @details If the number of nodes per level is less than the threshold, the level is skipped during the reordering.
 *         The default value is 32.
 * @param threshold The threshold for the number of nodes per level.
*/
void sylvan_set_reorder_threshold(size_t threshold);

__attribute__((unused)) 
/**
 * @brief Set the maximum growth coefficient.
 * @details The maximum growth coefficient is used to calculate the maximum growth of the number of nodes during the reordering.
 *        The default value is 1.2. If the number of nodes grows more than the maximum growth coefficient , sift up/down is terminated.
 * @param max_growth The maximum growth coefficient.
*/
void sylvan_set_reorder_maxgrowth(float max_growth);

__attribute__((unused))
/**
 * @brief Set the maximum number of swaps per sifting.
 * @details The default value is 10000.
 * @param max_swap The maximum number of swaps per sifting.
*/
void sylvan_set_reorder_maxswap(size_t max_swap);

__attribute__((unused)) 
/**
 * @brief Set the maximum number of vars swapped per sifting.
 * @details The default value is 2000.
 * @param max_var The maximum number of vars swapped per sifting.
*/
void sylvan_set_reorder_maxvar(size_t max_var);

__attribute__((unused))
/**
 * @brief Set the time limit for the reordering.
 * @details The default value is 50000 milliseconds.
 * @param time_limit The time limit for the reordering.
*/
void sylvan_set_reorder_timelimit(size_t time_limit);

TASK_DECL_5(varswap_res_t, sift_up, size_t*, size_t, size_t*, size_t*, size_t*);
/**
 * \brief Sift given variable up from its current level to the target level.
 *
 * \param var - variable to sift up
 * \param high - target position
 * \param curSize - pointer to current size of the bdd
 * \param bestSize - pointer to best size of the bdd (w.r.t. dynamic variable reordering)
 * \param bestPos - pointer to best position of the variable (w.r.t. dynamic variable reordering)
 *
 * \sideeffect order of variables is changed
 */
#define sift_down(var, high, curSize, bestSize, bestPos) RUN(sift_down, var, high, curSize, bestSize, bestPos)

TASK_DECL_5(varswap_res_t, sift_down, size_t*, size_t, size_t*, size_t*, size_t*);
/**
 * \brief Sift given variable down from its current level to the target level.
 *
 * \param var - variable to sift down
 * \param low - target level
 * \param curSize - pointer to current size of the bdd
 * \param bestSize - pointer to best size of the bdd (w.r.t. dynamic variable reordering)
 * \param bestPos - pointer to best position of the variable (w.r.t. dynamic variable reordering)
 *
 * \sideeffect order of variables is changed
 */
#define sift_up(var, low, curSize, bestSize, bestPos) RUN(sift_up, var, low, curSize, bestSize, bestPos)

TASK_DECL_2(varswap_res_t, sift_to_pos, size_t*, size_t);
/**
 * \brief Sift a variable to its best level.
 * \param var - variable to sift
 * \param targetPos - target position (w.r.t. dynamic variable reordering)
 */
#define sift_to_pos(var, targetPos) RUN(sift_to_pos, var, targetPos)

VOID_TASK_DECL_2(sylvan_reorder, uint32_t, uint32_t);
/**
  \brief Implementation of Rudell's sifting algorithm.

  \details
    <ol>
    <li> Order all the variables according to the number of entries
    in each unique table.
    <li> Sift the variable up and down, remembering each time the
    total size of the bdd size.
    <li> Select the best permutation.
    <li> Repeat 2 and 3 for all variables in given range.
    </ol>
  \param low - the lowest position to sift
  \param high - the highest position to sift

  \sideeffect order and number of variables might change
*/
#define sylvan_reorder(low, high)  CALL(sylvan_reorder, low, high)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_VAR_REORDER_H

