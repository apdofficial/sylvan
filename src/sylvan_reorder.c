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
#include "sylvan_interact.h"


#define STATS 1 // useful information w.r.t. dynamic reordering

static int reorder_initialized = 0;
static int reorder_is_running = 0;

/**
 * This variable is used for a cas flag so only
 * one reordering runs at one time
 */
static _Atomic (int) re;

struct sifting_config
{
    double t_start_sifting;                     // start time of the sifting
    uint32_t threshold;                         // threshold for number of nodes per level
    float max_growth;                           // coefficient used to calculate maximum growth
    uint32_t max_swap;                          // maximum number of swaps per sifting
    uint32_t total_num_swap;                    // number of swaps completed
    uint32_t max_var;                           // maximum number of vars swapped per sifting
    uint32_t total_num_var;                     // number of vars sifted
    double time_limit_ms;                       // time limit in milliseconds
};

/// reordering configurations
static struct sifting_config configs = {
        .t_start_sifting = 0,
        .threshold = 128,
        .max_growth = 1.2f,
        .max_swap = 10000,
        .total_num_swap = 0,
        .max_var = 250,
        .total_num_var = 0,
        .time_limit_ms = 1 * 60 * 1000 // 1 minute
};

typedef struct re_term_entry
{
    struct re_term_entry *next;
    re_term_cb cb;
} *re_term_entry_t;

static re_term_entry_t termre_list;

typedef struct re_hook_entry
{
    struct re_hook_entry *next;
    re_hook_cb cb;
} *re_hook_entry_t;

static re_hook_entry_t prere_list;
static re_hook_entry_t postre_list;
static re_hook_entry_t progre_list;

void sylvan_re_hook_prere(re_hook_cb callback)
{
    re_hook_entry_t e = (re_hook_entry_t) malloc(sizeof(struct re_hook_entry));
    e->cb = callback;
    e->next = prere_list;
    prere_list = e;
}

void sylvan_re_hook_postre(re_hook_cb callback)
{
    re_hook_entry_t e = (re_hook_entry_t) malloc(sizeof(struct re_hook_entry));
    e->cb = callback;
    e->next = postre_list;
    postre_list = e;
}

void sylvan_re_hook_progre(re_hook_cb callback)
{
    re_hook_entry_t e = (re_hook_entry_t) malloc(sizeof(struct re_hook_entry));
    e->cb = callback;
    e->next = progre_list;
    progre_list = e;
}

void sylvan_re_hook_termre(re_term_cb callback)
{
    re_term_entry_t e = (re_term_entry_t) malloc(sizeof(struct re_term_entry));
    e->cb = callback;
    e->next = termre_list;
    termre_list = e;
}

static int should_terminate_reordering(const struct sifting_config *reorder_config);

static double wctime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

static inline double wctime_sec_elapsed(double t_start)
{
    return wctime() - t_start;
}

static inline double wctime_ms_elapsed(double start)
{
    return wctime_sec_elapsed(start) * 1000;
}

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
    reorder_initialized = 0;
}

reorder_config_t sylvan_get_reorder_config()
{
    return (reorder_config_t) &configs;
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

void sylvan_set_reorder_timelimit(double time_limit)
{
    configs.time_limit_ms = time_limit;
}

static inline void update_best_pos(sifting_state_t *state)
{
    // update even if the size is the same
    // because this becomes the best closest position to our current position
    if (state->size <= state->best_size) {
        state->best_size = state->size;
        state->best_pos = state->pos;
    }
}

static inline int is_max_growth_reached(const sifting_state_t *state)
{
    return ((float) (state->size) >= configs.max_growth * (float) (state->best_size));
}

TASK_IMPL_1(varswap_t, sylvan_siftdown, sifting_state_t*, sifting_state)
{
    if (!reorder_initialized) return SYLVAN_VARSWAP_NOT_INITIALISED;

    bounds_state_t upper_bound = {
            .bound = 0,
            .limit = 0,
            .isolated = 0,
    };
    init_upper_bound(levels, sifting_state->pos, sifting_state->low, &upper_bound, sifting_state);

    for (; sifting_state->pos < sifting_state->high &&
                   (int)sifting_state->size - upper_bound.bound < upper_bound.limit; ++sifting_state->pos) {

        // Update the upper bound on node decrease
        if (interact_test(levels, sifting_state->pos, sifting_state->pos + 1)) {
            update_upper_bound(levels, sifting_state->pos, &upper_bound);
        }

        varswap_t res = sylvan_varswap(sifting_state->pos);
        sifting_state->size = llmsset_count_marked(nodes);

        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        update_best_pos(sifting_state);
        if (is_max_growth_reached(sifting_state)) {
            ++sifting_state->pos;
            break;
        }
        if (should_terminate_reordering(&configs)) break;
        if ((int)sifting_state->size < upper_bound.limit) upper_bound.limit = sifting_state->size;
    }
    return SYLVAN_VARSWAP_SUCCESS;
}

TASK_IMPL_1(varswap_t, sylvan_siftup, sifting_state_t*, sifting_state)
{
    if (!reorder_initialized) return SYLVAN_VARSWAP_NOT_INITIALISED;

    bounds_state_t lower_bound = {
            .bound = 0,
            .limit = 0,
            .isolated = 0,
    };
    init_lower_bound(levels, sifting_state->pos, sifting_state->low, &lower_bound, sifting_state);

    for (; sifting_state->pos > sifting_state->low && lower_bound.bound < lower_bound.limit; --sifting_state->pos) {
        varswap_t res = sylvan_varswap(sifting_state->pos - 1);
        sifting_state->size = llmsset_count_marked(nodes);

        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        update_best_pos(sifting_state);
        if (is_max_growth_reached(sifting_state)) {
            --sifting_state->pos;
            break;
        }
        if (should_terminate_reordering(&configs)) break;

        update_lower_bound(levels, sifting_state->pos, &lower_bound);
        if ((int)sifting_state->size < lower_bound.limit) lower_bound.limit = sifting_state->size;
    }
    return SYLVAN_VARSWAP_SUCCESS;
}

TASK_IMPL_2(varswap_t, sylvan_siftpos, uint32_t, pos, uint32_t, target)
{
    if (!reorder_initialized) return SYLVAN_VARSWAP_NOT_INITIALISED;
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

TASK_IMPL_1(varswap_t, sylvan_reorder_perm, const uint32_t*, permutation)
{
    if (!reorder_initialized) return SYLVAN_VARSWAP_NOT_INITIALISED;
    varswap_t res = SYLVAN_VARSWAP_SUCCESS;
    int identity = 1;

    // check if permutation is identity
    for (size_t level = 0; level < levels->count; level++) {
        if (permutation[level] != mtbdd_level_to_order(level)) {
            identity = 0;
            break;
        }
    }
    if (identity) return res;

    for (size_t level = 0; level < levels->count; ++level) {
        uint32_t var = permutation[level];
        uint32_t pos = mtbdd_order_to_level(var);
        res = sylvan_siftpos(pos, level);
        if (!sylvan_varswap_issuccess(res)) break;
    }

    return res;
}

VOID_TASK_IMPL_0(sylvan_reduce_heap)
{
    RUNEX(sylvan_reorder_impl, 0, 0);
}

TASK_IMPL_2(varswap_t, sylvan_reorder_impl, uint32_t, low, uint32_t, high)
{
    if (!reorder_initialized) return SYLVAN_VARSWAP_NOT_INITIALISED;
    if (reorder_is_running) return SYLVAN_VARSWAP_ALREADY_RUNNING;
    if (levels->count < 1) return SYLVAN_VARSWAP_ERROR;

    reorder_is_running = 1;

    sylvan_stats_count(SYLVAN_RE_COUNT);
    sylvan_timer_start(SYLVAN_RE);

    for (re_hook_entry_t e = prere_list; e != NULL; e = e->next) {
        WRAP(e->cb);
    }

    configs.t_start_sifting = wctime();
    configs.total_num_swap = 0;
    configs.total_num_var = 0;

    // if high == 0, then we sift all variables
    if (high == 0) high = levels->count - 1;

    interact_var_ref_init(levels);

    // now count all variable levels (parallel...)
    _Atomic (size_t) level_counts[levels->count];
    for (size_t i = 0; i < levels->count; i++) level_counts[i] = 0;
    sylvan_count_levelnodes(level_counts);
    // mark and sort
    int sorted_levels_counts[levels->count];
    mtbdd_mark_threshold(sorted_levels_counts, level_counts, configs.threshold);
    gnome_sort(sorted_levels_counts, level_counts);

    varswap_t res;
    sifting_state_t sifting_state;
    sifting_state.low = low;
    sifting_state.high = high;
    sifting_state.size = llmsset_count_marked(nodes);
    sifting_state.best_size = sifting_state.size;

    for (size_t i = 0; i < levels->count; i++) {
        if (sorted_levels_counts[i] < 0) break; // marked level, done
        uint64_t lvl = sorted_levels_counts[i];

        sifting_state.pos = mtbdd_level_to_order(lvl);
        if (sifting_state.pos < low || sifting_state.pos > high) continue; // skip, not in range

        sifting_state.best_pos = sifting_state.pos;

        // search for the optimum variable position
        // first sift to the closest boundary, then sift in the other direction
        if (lvl > levels->count / 2) {
            res = sylvan_siftdown(&sifting_state);
            if (!sylvan_varswap_issuccess(res)) break;
            res = sylvan_siftup(&sifting_state);
            if (!sylvan_varswap_issuccess(res)) break;
        } else {
            res = sylvan_siftup(&sifting_state);
            if (!sylvan_varswap_issuccess(res)) break;
            res = sylvan_siftdown(&sifting_state);
            if (!sylvan_varswap_issuccess(res)) break;
        }

        // optimum variable position restoration
        res = sylvan_siftpos(sifting_state.pos, sifting_state.best_pos);
        if (!sylvan_varswap_issuccess(res)) break;

        configs.total_num_var++;

        // if we managed to reduce size call progress hooks
        if (sifting_state.best_size < sifting_state.size) {
            for (re_hook_entry_t e = progre_list; e != NULL; e = e->next) {
                WRAP(e->cb);
            }
        }

        if (should_terminate_reordering(&configs)) break;
    }

    for (re_hook_entry_t e = postre_list; e != NULL; e = e->next) {
        WRAP(e->cb);
    }

    sylvan_timer_stop(SYLVAN_RE);
    configs.t_start_sifting = 0;

    reorder_is_running = 0;

    return res;
}

static int should_terminate_reordering(const struct sifting_config *reorder_config)
{
    for (re_term_entry_t e = termre_list; e != NULL; e = e->next) {
        if (e->cb()) {
#if STATS
            printf("sifting exit: termination_cb\n");
#endif
            return 1;
        }
    }

    if (reorder_config->total_num_swap > reorder_config->max_swap) {
#if STATS
        printf("sifting exit: reached %u from the total_num_swap %u\n",
               reorder_config->total_num_swap,
               reorder_config->max_swap);
#endif
        return 1;
    }
    if (reorder_config->total_num_var > reorder_config->max_var) {
#if STATS
        printf("sifting exit: reached %u from the total_num_var %u\n",
               reorder_config->total_num_var,
               reorder_config->max_var);
#endif
        return 1;
    }
    double t_elapsed = wctime_ms_elapsed(reorder_config->t_start_sifting);
    if (t_elapsed > reorder_config->time_limit_ms && reorder_config->t_start_sifting != 0) {
#if STATS
        printf("sifting exit: reached %fms from the time_limit %.2fms\n",
               t_elapsed,
               reorder_config->time_limit_ms);
#endif
        return 1;
    }
    return 0;
}