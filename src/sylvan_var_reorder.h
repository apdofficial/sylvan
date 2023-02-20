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

#ifndef SYLVAN_REORDER_H
#define SYLVAN_REORDER_H

#include <sylvan.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Sylvan dynamic variable reordering
 */
void sylvan_init_reorder(void);

TASK_DECL_2(int, sylvan_sifting, uint32_t, uint32_t);
#define sylvan_sifting(low, high) CALL(sylvan_sifting, low, high)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
