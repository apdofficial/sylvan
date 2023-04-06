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

#define ENABLE_ERROR   1 // critical errors that cause sifting to fail
#define ENABLE_INFO    1 // useful information w.r.t. dynamic reordering
#define ENABLE_DEBUG   1 // useful only for development purposes

#define LOG_ERROR(s, ...)   { if (ENABLE_ERROR) fprintf(stderr, s,  ##__VA_ARGS__); }
#define LOG_DEBUG(s, ...)   { if (ENABLE_DEBUG) fprintf(stdout, s,  ##__VA_ARGS__); }
#define LOG_INFO(s, ...)    { if (ENABLE_INFO)  fprintf(stdout, s,  ##__VA_ARGS__); }

static int reorder_initialized = 0;

typedef struct sifting_config
{
    reorder_termination_cb termination_cb;    // termination callback
    clock_t t_start_sifting;                  // start time of the sifting
    uint32_t threshold;           // threshold for number of nodes per level
    float max_growth;                         // coefficient used to calculate maximum growth
    uint32_t max_swap;                        // maximum number of swaps per sifting
    uint32_t total_num_swap;                  // number of swaps completed
    uint32_t max_var;                         // maximum number of vars swapped per sifting
    uint32_t total_num_var;                   // number of vars sifted
    uint64_t time_limit_ms;                   // time limit in milliseconds
} reorder_config_t;

// TODO: update clock_t t_start_sifting to cpu time instead of cpu clock (atm not compatible with multithreading)

/// reordering configurations
static reorder_config_t configs = {
        .termination_cb = NULL,
        .t_start_sifting = 0,
        .threshold = 64,
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
    configs.threshold = threshold;
}

void sylvan_set_reorder_maxgrowth(float max_growth)
{
    assert(max_growth > 1.0f);
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

static inline void update_best_pos(sifting_state_t* state)
{
    state->size = llmsset_count_marked(nodes);
    if (state->size < state->best_size) {
        state->best_size = state->size;
        state->best_pos = state->pos;
    }
}

static inline int is_max_growth_reached(const sifting_state_t* state)
{
    return ((float) (state->size) >= configs.max_growth * (float) (state->best_size));
}

static inline void move_pos_dn(sifting_state_t* state)
{
    ++state->pos;
}

static inline void move_pos_up(sifting_state_t* state)
{
    --state->pos;
}

TASK_IMPL_1(varswap_t, sylvan_siftdown, sifting_state_t*, state)
{
    for (; state->pos < state->high; move_pos_dn(state)) {
        varswap_t res = sylvan_varswap(state->pos);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        update_best_pos(state);
        if (is_max_growth_reached(state)) {
            move_pos_dn(state);
            break;
        }
    }
    return SYLVAN_VARSWAP_SUCCESS;
}


TASK_IMPL_1(varswap_t, sylvan_siftup, sifting_state_t*, state)
{
    for (; state->pos > state->low; move_pos_up(state)) {
        varswap_t res = sylvan_varswap(state->pos - 1);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        update_best_pos(state);
        if (is_max_growth_reached(state)) {
            move_pos_up(state);
            break;
        }
    }
    return SYLVAN_VARSWAP_SUCCESS;
}

TASK_IMPL_2(varswap_t, sylvan_siftpos, BDDLABEL, pos, BDDLABEL, target)
{
    for (; pos < target; pos++) {
        varswap_t res = sylvan_varswap(pos);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
    }
    for (; pos > target; pos--) {
        varswap_t res = sylvan_varswap(pos - 1);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
    }
    return SYLVAN_VARSWAP_SUCCESS;
}

TASK_IMPL_2(varswap_t, sylvan_reorder, BDDLABEL, low, BDDLABEL, high)
{
    if (mtbdd_levelscount() < 1) return SYLVAN_VARSWAP_ERROR;

    configs.t_start_sifting = clock();
    configs.total_num_swap = 0;
    configs.total_num_var = 0;

    // if high == 0, then we sift all variables
    if (high == 0) high = mtbdd_levelscount() - 1;
    LOG_INFO("sifting start: between %d and %d\n", low, high);

    // now count all variable levels (parallel...)
    size_t level_counts[mtbdd_levelscount()];
    for (size_t i = 0; i < mtbdd_levelscount(); i++) {
        level_counts[i] = 0;
    }
    sylvan_count_nodes(level_counts);

    // mark and sort
    int levels[mtbdd_levelscount()];
    mtbdd_mark_threshold(levels, level_counts, configs.threshold);
    gnome_sort(levels, level_counts);

    varswap_t res;
    sifting_state_t state;
    state.low = low;
    state.high = high;
    state.size = llmsset_count_marked(nodes);

    for (size_t i = 0; i < mtbdd_levelscount(); i++) {
        if (levels[i] < 0) break; // marked level, done
        uint64_t lvl = levels[i];

        state.pos = mtbdd_level_to_label(lvl);
        if (state.pos < low || state.pos > high) continue; // skip, not in range

        state.best_pos = state.pos;
        state.best_size = state.size;

        // search for the optimum variable position
        // first sift to the closest boundary, then sift in the other direction
        if (lvl > mtbdd_levelscount() / 2) {
            res = sylvan_siftdown(&state);
            if (!sylvan_varswap_issuccess(res)) break;
            res = sylvan_siftup(&state);
            if (!sylvan_varswap_issuccess(res)) break;
        } else {
            res = sylvan_siftup(&state);
            if (!sylvan_varswap_issuccess(res)) break;
            res = sylvan_siftdown(&state);
            if (!sylvan_varswap_issuccess(res)) break;
        }

        // optimum variable position restoration
        res = sylvan_siftpos(state.pos, state.best_pos);
        if (!sylvan_varswap_issuccess(res)) break;

        configs.total_num_var++;
        if (should_terminate_reordering(&configs)) break;
    }

    return res;
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
        LOG_INFO("sifting exit: reached %lums from the time_limit %.2llums\n", t_elapsed,
                 reorder_config->time_limit_ms);
        return 1;
    }
    return 0;
}