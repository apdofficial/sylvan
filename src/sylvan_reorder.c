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
#include "sylvan_varswap.h"
#include "sylvan_levels.h"
#include "sylvan_reorder.h"

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
    reorder_termination_cb termination_cb;    // termination callback
    clock_t t_start_sifting;                  // start time of the sifting
    uint32_t level_count_threshold;           // threshold for number of nodes per level
    float max_growth;                         // coefficient used to calculate maximum growth
    uint32_t max_swap;                        // maximum number of swaps per sifting
    uint32_t total_num_swap;                  // number of swaps completed
    uint32_t max_var;                         // maximum number of vars swapped per sifting
    uint32_t total_num_var;                   // number of vars sifted
    uint64_t time_limit_ms;                   // time limit in milliseconds
} reorder_config_t;

/// reordering configurations
static reorder_config_t configs = {
        .termination_cb = NULL,
        .t_start_sifting = 0,
        .level_count_threshold = 8,
        .max_growth = 1.1f,
        .max_swap = 10000,
        .total_num_swap = 0,
        .max_var = 2000,
        .total_num_var = 0,
        .time_limit_ms = 10 * 60000 // 10 minute
};

static int should_terminate_reordering(const reorder_config_t *reorder_config);

void sylvan_init_reorder()
{
    if (reorder_initialized) return;
    reorder_initialized = 1;

    sylvan_init_mtbdd();

    sylvan_register_quit(&sylvan_quit_reorder);
    mtbdd_levels_gc_add_mark_managed_refs();
}

void sylvan_quit_reorder()
{
    mtbdd_resetlevels();
    reorder_initialized = 0;
}

void sylvan_set_reorder_terminationcb(reorder_termination_cb callback)
{
    configs.termination_cb = callback;
}

void sylvan_set_reorder_threshold(uint32_t threshold)
{
    configs.level_count_threshold = threshold;
}

void sylvan_set_reorder_maxgrowth(float max_growth)
{
    configs.max_growth = max_growth;
}

void sylvan_set_reorder_maxswap(uint32_t max_swap)
{
    configs.max_swap = max_swap;
}

void sylvan_set_reorder_maxvar(uint32_t max_var)
{
    configs.max_var = max_var;
}

void sylvan_set_reorder_timelimit(uint64_t time_limit)
{
    configs.time_limit_ms = time_limit;
}

static inline BDDVAR mtbdd_next_highvar(const BDDVAR* var)
{
    BDDVAR curr = *var;
    if (curr >= UINT32_MAX) return curr;
    return curr + 1;
}

static inline BDDVAR mtbdd_next_lowvar(const BDDVAR* var)
{
    BDDVAR curr = *var;
    if (curr <= 0) return curr;
    return curr - 1;
}

TASK_IMPL_5(varswap_res_t, sift_down,
            BDDVAR*, var,
            BDDVAR, high,
            uint64_t*, cur_size,
            uint64_t*, best_size,
            BDDVAR*, best_pos)
{
    LOG_INFO("sift(DN): from %u to %u\n", *var, high);
    for (; *var < high; *var = mtbdd_next_highvar(var)) {
        varswap_res_t res = sylvan_varswap(*var);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        *cur_size = llmsset_count_marked(nodes);
        if (*cur_size < *best_size) {
            LOG_INFO("sift(DN): Improved size from %llu to %llu\n", *best_size, *cur_size);
            *best_size = *cur_size;
            *best_pos = *var;
        }
        if ((float) (*cur_size) >= configs.max_growth * (float) (*best_size)) {
            *var = mtbdd_next_highvar(var);
            break;
        }
    }
    return SYLVAN_VARSWAP_SUCCESS;
}


TASK_IMPL_5(varswap_res_t, sift_up,
            BDDVAR*, var,
            BDDVAR, low,
            uint64_t*, cur_size,
            uint64_t*, best_size,
            BDDVAR*, best_pos)
{
    LOG_INFO("sift(UP): from %u to %u\n", *var, low);
    for (; *var > low; *var = mtbdd_next_lowvar(var)) {
        varswap_res_t res = sylvan_varswap(mtbdd_next_lowvar(var));
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        *cur_size = llmsset_count_marked(nodes);
        if (*cur_size < *best_size) {
            LOG_INFO("sift(UP): Improved size from %llu to %llu\n", *best_size, *cur_size);
            *best_size = *cur_size;
            *best_pos = *var;
        }
        if ((float) (*cur_size) >= configs.max_growth * (float) (*best_size)) {
            *var = mtbdd_next_lowvar(var);
            break;
        }
    }
    return SYLVAN_VARSWAP_SUCCESS;
}

TASK_IMPL_2(varswap_res_t, sift_to_target, BDDVAR, var, BDDVAR, target_var)
{
    for (; var < target_var; var++) {
        varswap_res_t res = sylvan_varswap(var);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
    }
    for (; var > target_var; var--) {
        varswap_res_t res = sylvan_varswap(var--);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
    }
    return SYLVAN_VARSWAP_SUCCESS;
}

VOID_TASK_IMPL_2(sylvan_reorder, BDDVAR, low, BDDVAR, high)
{
    // TODO: implement variable interaction check (look at (Ebendt et al. in RT [12]) and CUDD)
    // TODO: implement variable order lock (its order will never be changed)
    sylvan_gc();

    if (mtbdd_levelscount() < 1) return;

    configs.t_start_sifting = clock();
    configs.total_num_swap = 0;
    configs.total_num_var = 0;

    uint64_t before_size = llmsset_count_marked(nodes);

    // if high == 0, then we sift all variables
    if (high == 0) high = mtbdd_levelscount() - 2; // do not swap the last node, thus -2 instead of -1
    LOG_INFO("sifting start: between %d and %d\n", low, high);

    int levels[mtbdd_levelscount()];
    mtbdd_count_sort_levels(levels, configs.level_count_threshold);

    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
        LOG_DEBUG("%d ", levels[i]);
    }
    LOG_DEBUG("\n");

    uint64_t cur_size = llmsset_count_marked(nodes);

    // loop over all levels and perform sifting on each variable
    for (size_t i = 0; i < mtbdd_levelscount(); i++) {
        int lvl = levels[i];
        if (lvl < 0) break; // done
        if (mtbdd_getorderlock(lvl)) continue; // skip, level is locked

        BDDVAR pos = mtbdd_level_to_var(lvl);

        BDDVAR best_pos = pos;
        uint64_t best_size = cur_size;

        LOG_INFO("sifting var %u at level %d\n", pos, lvl);
        varswap_res_t res;

        // search for the optimum variable position
        if (lvl > (long long int) (mtbdd_levelscount() / 2)) {
            res = sift_down(&pos, high, &cur_size, &best_size, &best_pos);
            res = sift_up(&pos, low, &cur_size, &best_size, &best_pos);
        } else {
            res = sift_up(&pos, low, &cur_size, &best_size, &best_pos);
            res = sift_down(&pos, high, &cur_size, &best_size, &best_pos);
        }

        // optimum variable position restoration
        sift_to_target(pos, best_pos);

        configs.total_num_var++;

        if (!sylvan_varswap_issuccess(res)) break;
        if (should_terminate_reordering(&configs)) break;

        LOG_DEBUG("level %d has best position %u with size %llu\n", lvl, best_pos, best_size);
    }

    uint64_t after_size = llmsset_count_marked(nodes);
    LOG_INFO("sifting finished: from %llu to %llu nodes\n", before_size, after_size);
}

static int should_terminate_reordering(const reorder_config_t *reorder_config)
{
    if (reorder_config->termination_cb != NULL && reorder_config->termination_cb()) {
        LOG_INFO("sifting exit: termination_cb\n");
        return 1;
    }
    if (reorder_config->total_num_swap > reorder_config->max_swap) {
        LOG_INFO("sifting exit: reached %u from the total_num_swap %u\n", reorder_config->total_num_swap,
                 reorder_config->max_swap);
        return 1;
    }
    if (reorder_config->total_num_var > reorder_config->max_var) {
        LOG_INFO("sifting exit: reached %u from the total_num_var %u\n", reorder_config->total_num_var,
                 reorder_config->max_var);
        return 1;
    }
    size_t t_elapsed = clock_ms_elapsed(reorder_config->t_start_sifting);
    if (t_elapsed > reorder_config->time_limit_ms) {
        LOG_INFO("sifting exit: reached %lums from the time_limit %.2llums\n", t_elapsed, reorder_config->time_limit_ms);
        return 1;
    }
    return 0;
}