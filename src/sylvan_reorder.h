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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
   \brief Type of termination handler.
*/
typedef int (*varswap_termination_cb)();

/**
   @brief Sifting configuration.
*/
typedef struct sifting_config sifting_config_t;

/**
 * Sylvan dynamic variable reordering
 */
void sylvan_init_reorder(void);

void sylvan_set_reordering_termination_cb(varswap_termination_cb callback);

void sylvan_set_reordering_threshold(size_t threshold);

void sylvan_set_reordering_max_growth(float max_growth);

void sylvan_set_reordering_max_swap(size_t max_swap);

void sylvan_set_reordering_max_var(size_t max_var);

void sylvan_set_reordering_time_limit(size_t time_limit);


TASK_DECL_5(sylvan_varswap_res_t, sift_up, size_t*, size_t, size_t*, size_t*, size_t*);
/**
 * \brief Sift given variable up from its current level to the target level.
 *
 * \param var - variable to sift up
 * \param high - target position
 * \param maxGrowth - some maximum % growth (from the start of a sift of a part. variable)
 * \param curSize - pointer to current size of the bdd
 * \param bestSize - pointer to best size of the bdd (w.r.t. dynamic variable reordering)
 * \param bestPos - pointer to best position of the variable (w.r.t. dynamic variable reordering)
 *
 * \sideeffect order of variables is changed
 */
#define sift_down(var, high, curSize, bestSize, bestPos) RUN(sift_down, var, high, curSize, bestSize, bestPos)

TASK_DECL_5(sylvan_varswap_res_t, sift_down, size_t*, size_t, size_t*, size_t*, size_t*);
/**
 * \brief Sift given variable down from its current level to the target level.
 *
 * \param var - variable to sift down
 * \param low - target level
 * \param maxGrowth - some maximum % growth (from the start of a sift of a part. variable)
 * \param curSize - pointer to current size of the bdd
 * \param bestSize - pointer to best size of the bdd (w.r.t. dynamic variable reordering)
 * \param bestPos - pointer to best position of the variable (w.r.t. dynamic variable reordering)
 *
 * \sideeffect order of variables is changed
 */
#define sift_up(var, low, curSize, bestSize, bestPos) RUN(sift_up, var, low, curSize, bestSize, bestPos)

TASK_DECL_2(sylvan_varswap_res_t, sift_to_pos, size_t, size_t);
/**
 * \brief Sift a variable to its best level.
 * \param var - variable to sift
 * \param targetPos - target position (w.r.t. dynamic variable reordering)
 */
#define sift_to_pos(var, targetPos) RUN(sift_to_pos, var, targetPos)

VOID_TASK_DECL_2(sylvan_sifting_new, uint32_t, uint32_t);
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

  \sideeffect order and number of variables is changed

*/
#define sylvan_sifting_new(low, high)  CALL(sylvan_sifting_new, low, high)

VOID_TASK_DECL_2(sylvan_sifting, uint32_t, uint32_t);
#define sylvan_sifting(low, high) CALL(sylvan_sifting, low, high)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_VAR_REORDER_H

