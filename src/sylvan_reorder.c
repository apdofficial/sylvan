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

#define STATS 0 // useful information w.r.t. dynamic reordering
#define DEBUG_STATS 0 // useful debugging information w.r.t. dynamic reordering


static int reorder_initialized = 0;
static int reorder_is_running = 0;
static int print_reordering_stat = 1;

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
        .threshold = SYLVAN_REORDER_NODES_THRESHOLD,
        .max_growth = SYLVAN_REORDER_GROWTH,
        .max_swap = SYLVAN_REORDER_MAX_SWAPS,
        .total_num_swap = 0,
        .max_var = SYLVAN_REORDER_MAX_VAR,
        .total_num_var = 0,
        .time_limit_ms = SYLVAN_REORDER_TIME_LIMIT_MS
};

VOID_TASK_DECL_0(sylvan_reorder_stop_world);

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

static int should_terminate_sifting(const struct sifting_config *reorder_config);

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

//static inline int is_max_growth_reached(const sifting_state_t *state)
//{
//    return ((double) (state->size) > (double) configs.max_growth * (double) state->best_size);
//}

TASK_IMPL_1(reorder_result_t, sylvan_siftdown, sifting_state_t*, sifting_state)
{
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;

    size_t R;  /* upper bound on node decrease */
    int isolated;
    size_t limitSize;
    limitSize = sifting_state->size = sifting_state->size - levels_isolated_count_load(levels);
    R = 0;

    for (BDDVAR y = sifting_state->high; y > sifting_state->pos; --y) {
        if (interact_test(levels, sifting_state->pos, y)) {
            isolated = levels_is_isolated(levels, y);
            R += levels_var_count_load(levels, y) - isolated;
        }
    }

//    for (; sifting_state->pos <= sifting_state->high; ++sifting_state->pos) {
    for (; sifting_state->pos <= sifting_state->high && sifting_state->size - R < limitSize; ++sifting_state->pos) {

//         Update the upper bound on node decrease
        if (interact_test(levels, sifting_state->pos, sifting_state->pos + 1)) {
            isolated = levels_is_isolated(levels, sifting_state->pos + 1);
            R -= (int) levels_var_count_load(levels, sifting_state->pos + 1) - isolated;
        }

        reorder_result_t res = sylvan_varswap(sifting_state->pos);
        sifting_state->size = llmsset_count_marked(nodes);

        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;

        update_best_pos(sifting_state);

        if ((double) (sifting_state->size) > (double) sifting_state->size * (double) configs.max_growth) {
            ++sifting_state->pos;
            break;
        }
        if (should_terminate_sifting(&configs)) break;
        if (sifting_state->size < limitSize) limitSize = sifting_state->size;
    }
    return SYLVAN_REORDER_SUCCESS;
}

TASK_IMPL_1(reorder_result_t, sylvan_siftup, sifting_state_t*, sifting_state)
{
    (void)sifting_state;
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;

    int L = 0;  /* lower bound on DD size */
    int isolated;
    int limitSize;
    sifting_state->size = llmsset_count_marked(nodes);
    /* Initialize the lower bound.
    ** The part of the DD below <y> will not change.
    ** The part of the DD above <y> that does not interact with <y> will not
    ** change. The rest may vanish in the best case, except for
    ** the nodes at level <low>, which will not vanish, regardless.
    */
    limitSize = L = (int) (sifting_state->size - levels_isolated_count_load(levels));
    for (BDDVAR x = sifting_state->low + 1; x < sifting_state->pos; ++x) {
        if (interact_test(levels, x, sifting_state->pos)) {
            isolated = levels_is_isolated(levels, x);
            L -= levels_var_count_load(levels, x) - isolated;
        }
    }

    for (; sifting_state->pos > sifting_state->low && L <= limitSize; --sifting_state->pos) {
        reorder_result_t res = sylvan_varswap(sifting_state->pos - 1);
        sifting_state->size = llmsset_count_marked(nodes);

        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        update_best_pos(sifting_state);
        if (should_terminate_sifting(&configs)) break;

        /* Update the lower bound. */
        if (interact_test(levels, sifting_state->pos, sifting_state->pos + 1)) {
            isolated = levels_is_isolated(levels, sifting_state->pos);
            L -= levels_var_count_load(levels, sifting_state->pos + 1) - isolated;
        }

        if ((double) (sifting_state->size) > (double) limitSize * (double) configs.max_growth) {
            --sifting_state->pos;
            break;
        }
        if ((int) sifting_state->size < limitSize) limitSize = sifting_state->size;
    }
    return SYLVAN_REORDER_SUCCESS;
}

TASK_IMPL_2(reorder_result_t, sylvan_siftpos, uint32_t, pos, uint32_t, target)
{
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;
    for (; pos < target; pos++) {
        reorder_result_t res = sylvan_varswap(pos);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
    }
    for (; pos > target; pos--) {
        reorder_result_t res = sylvan_varswap(pos - 1);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
    }
    return SYLVAN_REORDER_SUCCESS;
}

TASK_IMPL_1(reorder_result_t, sylvan_reorder_perm, const uint32_t*, permutation)
{
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;
    reorder_result_t res = SYLVAN_REORDER_SUCCESS;
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

void sylvan_test_reduce_heap()
{
    size_t size = llmsset_count_marked(nodes);
    if (size >= levels->reorder_size_threshold && levels->reorder_count < SYLVAN_REORDER_LIMIT) {
        sylvan_reduce_heap();
    }
}

void sylvan_reduce_heap()
{
    if (!reorder_initialized) return;
    if (reorder_is_running) {
        // avoid running multiple threads
        // if we are running RE and this function is invoked nevertheless
        // we must be in a different thread
        // since no operations are allowed while reordering
        // hang here until reordering is done and then exit
        while (reorder_is_running) {}
        return;
    }

    RUNEX(sylvan_reorder_stop_world);
}

/**
 * This variable is used for a cas flag so only
 * one reordering runs at one time
 */
static _Atomic (int) re;

VOID_TASK_IMPL_0(sylvan_reorder_stop_world)
{
    int zero = 0;
    if (atomic_compare_exchange_strong(&re, &zero, 1)) {
        TOGETHER(sylvan_reorder_impl, 0, 0);
        re = 0;
    } else {
        /* wait for new frame to appear */
        while (atomic_load_explicit(&lace_newframe.t, memory_order_relaxed) == 0) {}
        lace_yield(__lace_worker, __lace_dq_head);
    }
}

TASK_IMPL_2(reorder_result_t, sylvan_reorder_impl, uint32_t, low, uint32_t, high)
{
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;
    if (reorder_is_running) return SYLVAN_REORDER_ALREADY_RUNNING;
    if (levels->count < 1) return SYLVAN_REORDER_ERROR;

    reorder_is_running = 1;
    levels->reorder_count++;

#if DEBUG_STATS
    double t_start = wctime();
#endif
    sylvan_gc();

    size_t size_before = llmsset_count_marked(nodes);

    sylvan_stats_count(SYLVAN_RE_COUNT);
    sylvan_timer_start(SYLVAN_RE);

    for (re_hook_entry_t e = prere_list; e != NULL; e = e->next) {
        WRAP(e->cb);
    }

    configs.t_start_sifting = wctime();
    configs.total_num_var = 0;

    // if high == 0, then we sift all variables
    if (high == 0) high = levels->count - 1;
    SPAWN(interact_var_ref_init, levels);

    // now count all variable levels (parallel...)
    _Atomic (size_t) level_counts[levels->count];
    _Atomic (size_t) leaf_count = 0;
    for (size_t i = 0; i < levels->count; i++) level_counts[i] = 0;

    sylvan_count_levelnodes(level_counts, &leaf_count);

    // mark and sort
    int sorted_levels_counts[levels->count];
    mtbdd_mark_threshold(sorted_levels_counts, level_counts, configs.threshold);
    gnome_sort(sorted_levels_counts, level_counts);

    reorder_result_t res;
    sifting_state_t sifting_state;

    sifting_state.low = low;
    sifting_state.high = high;
    sifting_state.size = size_before;
    sifting_state.best_size = sifting_state.size;

    SYNC(interact_var_ref_init);

    for (size_t i = 0; i < levels->count; i++) {
        if (sorted_levels_counts[i] < 0) break; // marked level, done
        uint64_t lvl = sorted_levels_counts[i];

        sifting_state.pos = mtbdd_level_to_order(lvl);
        if (sifting_state.pos < low || sifting_state.pos > high) continue; // skip, not in range

        sifting_state.best_pos = sifting_state.pos;

        configs.total_num_swap = 0;

        // search for the optimum variable position
        // first sift to the closest boundary, then sift in the other direction
        if (lvl > levels->count / 2) {
#if DEBUG_STATS
            t_start = wctime();
#endif
            res = sylvan_siftdown(&sifting_state);
#if DEBUG_STATS
            printf("sylvan_siftdown time:  %f (%zu)\n", wctime() - t_start, llmsset_count_marked(nodes));
#endif

            if (sylvan_varswap_issuccess(res)) {
#if DEBUG_STATS
                t_start = wctime();
#endif
                sylvan_siftup(&sifting_state);
#if DEBUG_STATS
                printf("sylvan_siftup time:    %f (%zu)\n", wctime() - t_start, llmsset_count_marked(nodes));
#endif
            }
        } else {
#if DEBUG_STATS
            t_start = wctime();
#endif
            sylvan_siftup(&sifting_state);
#if DEBUG_STATS
            printf("sylvan_siftup time:    %f (%zu)\n", wctime() - t_start, llmsset_count_marked(nodes));
#endif
            res = sylvan_siftup(&sifting_state);
            if (sylvan_varswap_issuccess(res)) {
#if DEBUG_STATS
                t_start = wctime();
#endif
                sylvan_siftdown(&sifting_state);
#if DEBUG_STATS
                printf("sylvan_siftdown time:  %f (%zu)\n", wctime() - t_start, llmsset_count_marked(nodes));
#endif
            }
        }
        reorder_result_t old_res = res;
#if DEBUG_STATS
        t_start = wctime();
#endif
        // optimum variable position restoration
        res = sylvan_siftpos(sifting_state.pos, sifting_state.best_pos);
        sifting_state.best_size = llmsset_count_marked(nodes);
#if DEBUG_STATS
        printf("sylvan_siftpos time:   %f (%zu)\n", wctime() - t_start, llmsset_count_marked(nodes));
#endif
        if (!sylvan_varswap_issuccess(res) || !sylvan_varswap_issuccess(old_res)) break;

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

    if (print_reordering_stat) {
        printf("BDD reordering with sifting: from %zu to ... %zu nodes in %f sec\n", size_before,
               sifting_state.best_size, wctime() - configs.t_start_sifting);
    }

    sylvan_timer_stop(SYLVAN_RE);
    configs.t_start_sifting = 0;

    // new size threshold for next reordering is double the size of non-terminal nodes + the terminal nodes
    size_t new_size_threshold = (sifting_state.best_size - leaf_count + 1) * SYLVAN_REORDER_SIZE_RATIO + leaf_count;
    if (levels->reorder_count < SYLVAN_REORDER_LIMIT && new_size_threshold > levels->reorder_size_threshold) {
        levels->reorder_size_threshold = new_size_threshold;
    } else {
        levels->reorder_size_threshold += SYLVAN_REORDER_LIMIT;
    }

    reorder_is_running = 0;

    return res;
}

static int should_terminate_sifting(const struct sifting_config *reorder_config)
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


static int should_terminate_reordering(const struct sifting_config *reorder_config)
{
    for (re_term_entry_t e = termre_list; e != NULL; e = e->next) {
        if (e->cb()) {
#if STATS
            printf("reordering exit: termination_cb\n");
#endif
            return 1;
        }
    }

    if (reorder_config->total_num_var > reorder_config->max_var) {
#if STATS
        printf("reordering exit: reached %u from the total_num_var %u\n",
               reorder_config->total_num_var,
               reorder_config->max_var);
#endif
        return 1;
    }
    double t_elapsed = wctime_ms_elapsed(reorder_config->t_start_sifting);
    if (t_elapsed > reorder_config->time_limit_ms && reorder_config->t_start_sifting != 0) {
#if STATS
        printf("reordering exit: reached %fms from the time_limit %.2fms\n",
               t_elapsed,
               reorder_config->time_limit_ms);
#endif
        return 1;
    }
    return 0;
}