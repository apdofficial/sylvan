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
 * Sylvan dynamic variable reordering
 */
void sylvan_init_reorder(void);

void print_levels_ordering(void);


VOID_TASK_DECL_6(sift_up, size_t, size_t, float, size_t*, size_t*, size_t*);
/**
 * \brief Sift given variable up from its current level to the target level.
 *
 * \param var - variable to sift up
 * \param targetLvl - target level
 * \param maxGrowth - some maximum % growth (from the start of a sift of a part. variable)
 * \param curSize - pointer to current size of the bdd
 * \param bestSize - pointer to best size of the bdd (w.r.t. dynamic variable reordering)
 * \param bestLvl - pointer to best level of the variable (w.r.t. dynamic variable reordering)
 *
 * \sideeffect order of variables is changed
 */
#define sift_up(var, targetLvl, maxGrowth, curSize, bestSize, bestLvl) \
                    RUN(sift_up, var, targetLvl, maxGrowth, curSize, bestSize, bestLvl)

VOID_TASK_DECL_6(sift_down, size_t, size_t, float, size_t*, size_t*, size_t*);
/**
 * \brief Sift given variable down from its current level to the target level.
 *
 * \param var - variable to sift down
 * \param targetLvl - target level
 * \param maxGrowth - some maximum % growth (from the start of a sift of a part. variable)
 * \param curSize - pointer to current size of the bdd
 * \param bestSize - pointer to best size of the bdd (w.r.t. dynamic variable reordering)
 * \param bestLvl - pointer to best level of the variable (w.r.t. dynamic variable reordering)
 *
 * \sideeffect order of variables is changed
 */
#define sift_down(var, targetLvl, maxGrowth, curSize, bestSize, bestLvl) \
                    RUN(sift_down, var, targetLvl, maxGrowth, curSize, bestSize, bestLvl)

VOID_TASK_DECL_2(sift_to_pos, size_t, size_t);
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

  \param low_lvl - the lowest level to sift
  \param high_lvl - the highest level to sift

  \sideeffect order and number of variables is changed

*/
#define sylvan_sifting_new(low, high) CALL(sylvan_sifting_new, low, high)

TASK_DECL_2(int, sylvan_sifting, uint32_t, uint32_t);
#define __sylvan_sifting(low, high) CALL(sylvan_sifting, low, high)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_VAR_REORDER_H

