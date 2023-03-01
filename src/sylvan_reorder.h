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

VOID_TASK_DECL_5(sift_up, size_t, size_t, size_t, size_t*, size_t*);
/**
 * @brief Sift a variable up  from its current level to the target level.
 * \param var - variable to sift up
 * \param target_lvl - target level
 * \param cursize - current size of the bdd
 * \param bestsize - best size of the bdd (w.r.t. dynamic variable reordering)
 * \param bestlvl - best level of the variable (w.r.t. dynamic variable reordering)
 */
#define sift_up(var, target_lvl, cursize, bestsize, bestlvl) RUN(sift_up,var, target_lvl, cursize, bestsize, bestlvl)

VOID_TASK_DECL_5(sift_down, size_t, size_t, size_t, size_t*, size_t*);
/**
 * @brief Sift a variable down from its current level to the target level.
 * \param var - variable to sift down
 * \param target_lvl - target level
 * \param cursize - current size of the bdd
 * \param bestsize - best size of the bdd (w.r.t. dynamic variable reordering)
 * \param bestlvl - best level of the variable (w.r.t. dynamic variable reordering)
 */
#define sift_down(var, target_lvl, cursize, bestsize, bestlvl) RUN(sift_down,var, target_lvl, cursize, bestsize, bestlvl)

VOID_TASK_DECL_2(sift_to_lvl, size_t, size_t);
/**
 * @brief Sift a variable to its best level.
 * \param var - variable to sift
 * \param bestlvl - best level of the variable (w.r.t. dynamic variable reordering)
 */
#define sift_to_lvl(var, bestlvl) RUN(sift_to_lvl, var, bestlvl)

VOID_TASK_DECL_2(sylvan_sifting_new, uint32_t, uint32_t);
#define sylvan_sifting_new(low_lvl, high_lvl) CALL(sylvan_sifting_new, low_lvl, high_lvl)

TASK_DECL_2(int, sylvan_sifting, uint32_t, uint32_t);
#define sylvan_sifting(low, high) CALL(sylvan_sifting, low, high)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_VAR_REORDER_H

