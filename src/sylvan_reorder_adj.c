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

#include <sylvan_int.h>
#include <sys/time.h>
#include "sylvan_varswap_adj.h"
#include "sylvan_levels.h"
#include "sylvan_reorder_adj.h"

/**
 * Block size tunes the granularity of the parallel distribution
 */
#define BLOCKSIZE 128

#define ENABLE_ERROR_LOGS   1 // critical errors that cause sifting to fail
#define ENABLE_INFO_LOGS    1 // useful information w.r.t. dynamic reordering
#define ENABLE_DEBUG_LOGS   0 // useful only for development purposes

#define LOG_ERROR(s, ...)   { if (ENABLE_ERROR_LOGS) fprintf(stderr, s,  ##__VA_ARGS__); }
#define LOG_DEBUG(s, ...)   { if (ENABLE_DEBUG_LOGS) fprintf(stdout, s,  ##__VA_ARGS__); }
#define LOG_INFO(s, ...)    { if (ENABLE_INFO_LOGS)  fprintf(stdout, s,  ##__VA_ARGS__); }

static int reorder_initialized = 0;

typedef struct sifting_config
{
    reorder_termination_cb termination_cb;  // termination callback
    clock_t t_start_sifting;                // start time of the sifting
    size_t level_count_threshold;           // threshold for number of nodes per level
    float max_growth;                       // coefficient used to calculate maximum growth
    size_t max_swap;                        // maximum number of swaps per sifting
    size_t total_num_swap;                  // number of swaps completed
    size_t max_var;                         // maximum number of vars swapped per sifting
    size_t total_num_var;                   // number of vars sifted
    unsigned long time_limit_ms;            // time limit in milliseconds
} reorder_config_t;

/// reordering configurations
static reorder_config_t configs = {
        .termination_cb = NULL,
        .t_start_sifting = 0,
        .level_count_threshold = 1,
        .max_growth = 1.2f,
        .max_swap = 10000,
        .total_num_swap = 0,
        .max_var = 2000,
        .total_num_var = 0,
        .time_limit_ms = 50000
};

static int should_terminate_reordering(const reorder_config_t *reorder_config);


TASK_IMPL_5(varswap_res_t, sift_down_adj,
            size_t,  var,
            size_t,  high_lvl,
            size_t*, cur_size,
            size_t*, best_size,
            size_t*, best_lvl)
{
    while(mtbdd_var_to_level(var) < high_lvl) {
        varswap_res_t res = sylvan_varswap_adj(var, mtbdd_nexthigh(var));
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        size_t after = llmsset_count_marked(nodes);
        LOG_DEBUG("sift(DN): v%lu at level %u\n", var, mtbdd_var_to_level(var));
        *cur_size = after;
        if (*cur_size < *best_size) {
            LOG_DEBUG("sift(DN):   Improved size from %zu to %zu\n", *best_size, *cur_size);
            *best_size = *cur_size;
            *best_lvl = mtbdd_var_to_level(var);
        }
        if ((float) (*cur_size) >= configs.max_growth * (float) (*best_size)) {
            *best_lvl = mtbdd_var_to_level(var);
            break;
        }
    }

    return SYLVAN_VARSWAP_SUCCESS;
}


TASK_IMPL_5(varswap_res_t, sift_up_adj,
            size_t,  var,
            size_t,  low_lvl,
            size_t*, cur_size,
            size_t*, best_size,
            size_t*, best_lvl)
{

    while (mtbdd_var_to_level(var) > low_lvl) {
        varswap_res_t res = sylvan_varswap_adj(var, mtbdd_nextlow(var));
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        size_t after = llmsset_count_marked(nodes);
        LOG_DEBUG("sift(UP): v%lu at level %u\n", var, mtbdd_var_to_level(var));
        *cur_size = after;
        if (*cur_size < *best_size) {
            LOG_DEBUG("sift(UP):   Improved size from %zu to %zu\n", *best_size, *cur_size);
            *best_size = *cur_size;
            *best_lvl = mtbdd_var_to_level(var);
        }
        if ((float) (*cur_size) >= configs.max_growth * (float) (*best_size)) {
            *best_lvl = mtbdd_var_to_level(var);
            break;
        }
    }

    return SYLVAN_VARSWAP_SUCCESS;
}

TASK_IMPL_2(varswap_res_t, sift_to_lvl, size_t, var, size_t, lvl)
{
    LOG_DEBUG("sift_to_lvl: v%lu at level %u to level %lu\n", var, mtbdd_var_to_level(var), lvl);
    while (mtbdd_var_to_level(var) < lvl) {
        varswap_res_t res = sylvan_varswap_adj(var, mtbdd_nexthigh(var));
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
    }
    while (mtbdd_var_to_level(var) > lvl) {
        varswap_res_t res = sylvan_varswap_adj(var, mtbdd_nextlow(var));
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
    }
    return SYLVAN_VARSWAP_SUCCESS;
}

VOID_TASK_IMPL_2(sylvan_reorder_adj, uint32_t, low_lvl, uint32_t, high_lvl)
{
    // TODO: implement variable interaction check (look at (Ebendt et al. in RT [12]) and CUDD)
    // TODO: implement variable order lock (its order will never be changed)

    if (mtbdd_levels_size() < 1) return;

    configs.t_start_sifting = clock();
    configs.total_num_swap = 0;
    configs.total_num_var = 0;

    size_t before_size = llmsset_count_marked(nodes);

    // if high == 0, then we sift all variables
    if (high_lvl == 0) high_lvl = mtbdd_levels_size() - 2;
    LOG_INFO("sifting started: between levels %d and %d\n", low_lvl, high_lvl);

    int levels[mtbdd_levels_size()];
    mtbdd_count_sort_levels(levels, configs.level_count_threshold);

    size_t cur_size = llmsset_count_marked(nodes);

    // loop over all levels and perform sifting on each variable
    for (size_t i = 0; i < mtbdd_levels_size(); i++) {
        int lvl = levels[i];
        if (lvl == -1) break; // done
        if (mtbdd_getorderlock(lvl)) continue; // skip, level is locked

        size_t var = mtbdd_level_to_var(lvl);

        size_t best_size = cur_size;
        size_t best_lvl = lvl;

        LOG_DEBUG("sifting level %d at position %zu\n", lvl, var);
        varswap_res_t res;

        // search for the optimum variable position
        if (lvl > (long long int) (mtbdd_levels_size() / 2)) {
            res = sift_up_adj(var, low_lvl, &cur_size, &best_size, &best_lvl);
            res = sift_down_adj(var, high_lvl, &cur_size, &best_size, &best_lvl);
        } else {
            res = sift_down_adj(var, high_lvl, &cur_size, &best_size, &best_lvl);
            res = sift_up_adj(var, low_lvl, &cur_size, &best_size, &best_lvl);
        }

        // optimum variable position restoration
        sift_to_lvl(var, best_lvl);

        configs.total_num_var++;

        if (sylvan_varswap_issuccess(res) == SYLVAN_FAIL) break;
        if (should_terminate_reordering(&configs)) break;

        LOG_DEBUG("var %zu has best level %zu with size %zu\n", var, best_lvl, best_size);
    }

    size_t after_size = llmsset_count_marked(nodes);
    LOG_INFO("sifting finished: from %zu to %zu nodes\n", before_size, after_size);
}

static int should_terminate_reordering(const reorder_config_t *reorder_config)
{
    if (reorder_config->termination_cb != NULL && reorder_config->termination_cb()) {
        LOG_INFO("sifting exit: termination_cb\n");
        return SYLVAN_SUCCESS;
    }
    if (reorder_config->total_num_swap > reorder_config->max_swap) {
        LOG_INFO("sifting exit: reached %zu from the total_num_swap %lu\n", reorder_config->total_num_swap,
                 reorder_config->max_swap);
        return SYLVAN_SUCCESS;
    }
    if (reorder_config->total_num_var > reorder_config->max_var) {
        LOG_INFO("sifting exit: reached %zu from the total_num_var %lu\n", reorder_config->total_num_var,
                 reorder_config->max_var);
        return SYLVAN_SUCCESS;
    }
    size_t t_elapsed = clock_ms_elapsed(reorder_config->t_start_sifting);
    if (t_elapsed > reorder_config->time_limit_ms) {
        LOG_INFO("sifting exit: reached %lums from the time_limit %.2zums\n", t_elapsed, reorder_config->time_limit_ms);
        return SYLVAN_SUCCESS;
    }
    return SYLVAN_FAIL;
}