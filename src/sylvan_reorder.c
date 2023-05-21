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
#include <stdatomic.h>

#include "sylvan_varswap.h"
#include "sylvan_levels.h"
#include "sylvan_reorder.h"
#include "sylvan_interact.h"

#define STATS 1 // useful information w.r.t. dynamic reordering
#define DEBUG_STATS 0 // useful debugging information w.r.t. dynamic reordering


static int reorder_initialized = 0;
static int print_reordering_stat = 1;

struct sifting_config
{
    double t_start_sifting;                     // start time of the sifting
    uint32_t threshold;                         // threshold for number of nodes per level
    double max_growth;                          // coefficient used to calculate maximum growth
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

TASK_IMPL_1(reorder_result_t, sylvan_siftdown, sifting_state_t*, s_state)
{
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;

    reorder_result_t res;
    int R;  // upper bound on node decrease
    int isolated;
    int limitSize;
    BDDVAR xIndex;
    BDDVAR yIndex;

    s_state->size = CALL(llmsset_count_marked, nodes);
    xIndex = levels->level_to_order[s_state->pos];

    limitSize = s_state->size - levels->isolated_count;
    R = 0;

    // Let <x> be the variable at level <pos>. (s_state->pos)
    // Let <y> be the variable at level <pos+1>. (s_state->pos+1)
    // Let <Ni> be the number of nodes at level i.
    // Let <n> be the number of levels. (levels->count)

    // Then the size of DD can not be reduced below:
    // LB(DN) = Nj + ∑ Ni | 0<i<pos

    // The part of the DD above <x> will not change.
    // The part of the DD below <x> that does not interact with <x> will not change.
    // The rest may vanish in the best case, except for
    // the nodes at level <high>, which will not vanish, regardless.

    // x = pos
    // y = pos+1

    // Initialize the upper bound
    for (BDDVAR y = s_state->high; y > s_state->pos; y--) {
        yIndex = levels->level_to_order[y];
        if (interact_test(levels, xIndex, yIndex)) {
            isolated = atomic_load_explicit(&levels->ref_count[yIndex], memory_order_relaxed) <= 1;
            R += (int) atomic_load_explicit(&levels->var_count[yIndex], memory_order_relaxed) - isolated;
        }
    }

    for (; s_state->pos < s_state->high && s_state->size - R < limitSize; ++s_state->pos) {
        //  Update the upper bound on node decrease
        yIndex = levels->level_to_order[s_state->pos + 1];
        if (interact_test(levels, xIndex, yIndex)) {
            isolated = atomic_load_explicit(&levels->ref_count[yIndex], memory_order_relaxed) <= 1;
            R -= (int) atomic_load_explicit(&levels->var_count[yIndex], memory_order_relaxed) - isolated;
        }
        res = CALL(sylvan_varswap, s_state->pos);
        s_state->size = CALL(llmsset_count_marked, nodes);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        if (should_terminate_sifting(&configs)) break;
        // check the max allowed size growth
        if ((double) (s_state->size) > (double) s_state->best_size * configs.max_growth) {
            ++s_state->pos;
            break;
        }
        // update best position
        if (s_state->size < s_state->best_size) {
            s_state->best_size = s_state->size;
            s_state->best_pos = s_state->pos;
        }
        if (s_state->size < limitSize) limitSize = s_state->size;
    }
    return SYLVAN_REORDER_SUCCESS;
}

TASK_IMPL_1(reorder_result_t, sylvan_siftup, sifting_state_t*, s_state)
{
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;

    reorder_result_t res;
    int L;  // lower bound on DD size
    int isolated;
    int limitSize;
    BDDVAR xIndex;
    BDDVAR yIndex;

    s_state->size = CALL(llmsset_count_marked, nodes);
    yIndex = levels->level_to_order[s_state->pos];

    // Let <x> be the variable at level <pos-1>. (s_state->pos-1)
    // Let <y> be the variable at level <pos>. (s_state->pos)
    // Let <Ni> be the number of nodes at level i.
    // Let <n> be the number of levels. (levels->count)

    // Then the size of DD can not be reduced below:
    // LB(UP) = N0 + ∑ Ni | i<pos<n

    // The part of the DD below <y> will not change.
    // The part of the DD above <y> that does not interact with <x> will not change.
    // The rest may vanish in the best case, except for
    // the nodes at level <low>, which will not vanish, regardless.

    limitSize = L = (int) (s_state->size - levels->isolated_count);
    for (BDDVAR x = s_state->low + 1; x < s_state->pos; x++) {
        xIndex = levels->level_to_order[x];
        if (interact_test(levels, xIndex, yIndex)) {
            isolated = atomic_load_explicit(&levels->ref_count[xIndex], memory_order_relaxed) <= 1;
            L -= (int) atomic_load_explicit(&levels->var_count[xIndex], memory_order_relaxed) - isolated;
        }
    }

    for (; s_state->pos > s_state->low && L <= limitSize; --s_state->pos) {
        xIndex = levels->level_to_order[s_state->pos-1];
        res = CALL(sylvan_varswap, s_state->pos - 1);
        s_state->size = CALL(llmsset_count_marked, nodes);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
        if (should_terminate_sifting(&configs)) break;
        // check the max allowed size growth
        if ((double) (s_state->size) > (double) s_state->best_size * configs.max_growth) {
            --s_state->pos;
            break;
        }
        // update the best position
        if (s_state->size < s_state->best_size) {
            s_state->best_size = s_state->size;
            s_state->best_pos = s_state->pos;
        }
        // Update the lower bound on DD size
        if (interact_test(levels, xIndex, yIndex)) {
            isolated = atomic_load_explicit(&levels->ref_count[xIndex], memory_order_relaxed) <= 1;
            L += (int) atomic_load_explicit(&levels->var_count[yIndex], memory_order_relaxed) - isolated;
        }
        if ((int) s_state->size < limitSize) limitSize = (int) s_state->size;
    }
    return SYLVAN_REORDER_SUCCESS;
}

TASK_IMPL_2(reorder_result_t, sylvan_siftpos, uint32_t, pos, uint32_t, target)
{
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;
    for (; pos < target; pos++) {
        reorder_result_t res = CALL(sylvan_varswap, pos);
        if (!sylvan_varswap_issuccess(res)) return res;
        configs.total_num_swap++;
    }
    for (; pos > target; pos--) {
        reorder_result_t res = CALL(sylvan_varswap, pos - 1);
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
        if (permutation[level] != levels->level_to_order[level]) {
            identity = 0;
            break;
        }
    }
    if (identity) return res;

    for (size_t level = 0; level < levels->count; ++level) {
        uint32_t var = permutation[level];
        uint32_t pos = mtbdd_order_to_level(var);
        res = CALL(sylvan_siftpos, pos, level);
        if (!sylvan_varswap_issuccess(res)) break;
    }

    return res;
}

void sylvan_test_reduce_heap()
{
    if (llmsset_count_marked(nodes) >= levels->reorder_size_threshold && levels->reorder_count < SYLVAN_REORDER_LIMIT) {
        RUNEX(sylvan_reorder_stop_world);
//        RUNEX(sylvan_reorder_impl, 0, 0);
    }
}

void sylvan_reduce_heap()
{
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
        NEWFRAME(sylvan_reorder_impl, 0, 0);
        re = 0;
    } else {
        /* wait for new frame to appear */
        while (atomic_load_explicit(&lace_newframe.t, memory_order_relaxed) == 0) {}
        lace_yield(__lace_worker, __lace_dq_head);
    }
}

//TASK_IMPL_2(int, sylvan_simple_reorder_impl, uint32_t, low, uint32_t, high)
//{
//    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;
//    if (levels->count < 1) return SYLVAN_REORDER_ERROR;
//
//    levels->reorder_count++;
//
//#if DEBUG_STATS
//    double t_start = wctime();
//#endif
//
//    sylvan_stats_count(SYLVAN_RE_COUNT);
//    sylvan_timer_start(SYLVAN_RE);
//
//    for (re_hook_entry_t e = prere_list; e != NULL; e = e->next) {
//        WRAP(e->cb);
//    }
//
//    configs.t_start_sifting = wctime();
//    configs.total_num_var = 0;
//
//    // if high == 0, then we sift all variables
//    if (high == 0) high = levels->count - 1;
//
//    CALL(interact_var_ref_init, levels);
//    double t_start_sifting = wctime();
//    size_t before_size = llmsset_count_marked(nodes);
//
//    // count all variable levels (parallel...)
//    _Atomic (size_t) level_counts[levels->count];
//    _Atomic (size_t) leaf_count = 2;
//    for (size_t i = 0; i < levels->count; i++) level_counts[i] = 0;
//    CALL(sylvan_count_levelnodes, level_counts, &leaf_count, 0, nodes->table_size);
//
//    // mark and sort variable levels based on the threshold
//    int sorted_levels_counts[levels->count];
//    mtbdd_mark_threshold(sorted_levels_counts, level_counts, configs.threshold);
//    gnome_sort(sorted_levels_counts, level_counts);
//
//    size_t cursize = llmsset_count_marked(nodes);
//
//    for (int i = 0; i < (int) levels->count; i++) {
//        int lvl = sorted_levels_counts[i];
//        if (lvl < 0) break; // done
//        size_t pos = levels->level_to_order[lvl];
//        if (pos < low || pos > high) continue;
//
//        size_t bestsize = cursize, bestpos = pos;
//
//        if ((double) lvl > (double) levels->count / 2) {
//            // we are in the lower half of the levels, so sift down first and then up
//            // sifting down
//            for (; pos < high; pos++) {
//                if (CALL(sylvan_varswap, pos) != 0) {
//                    printf("swap failed, table full. TODO garbage collect.\n");
//                    break;
//                }
//                size_t after = llmsset_count_marked(nodes);
//                cursize = after;
//                if (cursize < bestsize) {
//                    bestsize = cursize;
//                    bestpos = pos;
//                }
//                if ((double) cursize >= configs.max_growth * (double) bestsize) {
//                    pos++;
//                    break;
//                }
//            }
//            // sifting up
//            for (; pos > low; pos--) {
//                if (CALL(sylvan_varswap, pos - 1) != 0) {
//                    printf("swap failed, table full. TODO garbage collect.\n");
//                    break;
//                }
//                size_t after = llmsset_count_marked(nodes);
//                cursize = after;
//                if (cursize < bestsize) {
//                    bestsize = cursize;
//                    bestpos = pos;
//                }
//                if ((double) cursize >= configs.max_growth * (double) bestsize) {
//                    pos--;
//                    break;
//                }
//            }
//        } else {
//            // we are in the upper half of the levels, so sift up first and then down
//            // sifting up
//            for (; pos > low; pos--) {
//                if (CALL(sylvan_varswap, pos - 1) != 0) {
//                    printf("swap failed, table full. TODO garbage collect.\n");
//                    break;
//                }
//                size_t after = llmsset_count_marked(nodes);
//                cursize = after;
//                if (cursize < bestsize) {
//                    bestsize = cursize;
//                    bestpos = pos;
//                }
//                if ((double) cursize >= configs.max_growth * (double) bestsize) {
//                    pos--;
//                    break;
//                }
//            }
//            // sifting down
//            for (; pos < high; pos++) {
//                if (CALL(sylvan_varswap, pos) != 0) {
//                    printf("swap failed, table full. TODO garbage collect.\n");
//                    break;
//                }
//                size_t after = llmsset_count_marked(nodes);
//                cursize = after;
//                if (cursize < bestsize) {
//                    bestsize = cursize;
//                    bestpos = pos;
//                }
//                if ((double) cursize >= configs.max_growth * (double) bestsize) {
//                    pos++;
//                    break;
//                }
//            }
//        }
//        // optimum variable position restoration
//        for (; pos < bestpos; pos++) {
//            CALL(sylvan_varswap, pos);
//        }
//        for (; pos > bestpos; pos--) {
//            CALL(sylvan_varswap, pos - 1);
//        }
//    }
//
//    size_t after_size = llmsset_count_marked(nodes);
//
//    // new size threshold for next reordering is double the size of non-terminal nodes + the terminal nodes
//    size_t new_size_threshold = (after_size - leaf_count + 1) * SYLVAN_REORDER_SIZE_RATIO + leaf_count;
//    if (levels->reorder_count < SYLVAN_REORDER_LIMIT || new_size_threshold > levels->reorder_size_threshold) {
//        levels->reorder_size_threshold = new_size_threshold;
//    } else {
//        levels->reorder_size_threshold += SYLVAN_REORDER_LIMIT;
//    }
//
//    if (print_reordering_stat) {
//        printf("BDD reordering with simple sifting: from %zu to ... %zu nodes in %f sec\n",
//               before_size, after_size, wctime() - t_start_sifting);
//    }
//
//    return SYLVAN_REORDER_SUCCESS;
//}

TASK_IMPL_2(reorder_result_t, sylvan_reorder_impl, uint32_t, low, uint32_t, high)
{
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;
    if (levels->count < 1) return SYLVAN_REORDER_ERROR;

    levels->reorder_count++;

    sylvan_stats_count(SYLVAN_RE_COUNT);
    sylvan_timer_start(SYLVAN_RE);

    for (re_hook_entry_t e = prere_list; e != NULL; e = e->next) {
        WRAP(e->cb);
    }

    configs.t_start_sifting = wctime();
    configs.total_num_var = 0;

    // if high == 0, then we sift all variables
    if (high == 0) high = levels->count - 1;

    // parallel
    CALL(interact_var_ref_init, levels);
    double t_start_sifting = wctime();
    size_t before_size = CALL(llmsset_count_marked, nodes);

    // count all variable levels (parallel...)
    _Atomic (size_t) level_counts[levels->count];
    _Atomic (size_t) leaf_count = 2;
    for (size_t i = 0; i < levels->count; i++) level_counts[i] = 0;
    // parallel
    CALL(sylvan_count_levelnodes, level_counts, &leaf_count, 0, nodes->table_size);

    // mark and sort variable levels based on the threshold
    int sorted_levels_counts[levels->count];
    mtbdd_mark_threshold(sorted_levels_counts, level_counts, configs.threshold);
    gnome_sort(sorted_levels_counts, level_counts);

    reorder_result_t res;
    sifting_state_t s_state;

    s_state.pos = 0;
    s_state.best_pos = 0;
    s_state.size = CALL(llmsset_count_marked, nodes);
    s_state.best_size = s_state.size;
    s_state.low = low;
    s_state.high = high;

    for (int i = 0; i < (int) levels->count; i++) {
        int lvl = sorted_levels_counts[i];
        if (lvl < 0) break; // done
        s_state.pos = levels->level_to_order[lvl];
        if (s_state.pos < low || s_state.pos > high) continue;

        configs.total_num_swap = 0;
        s_state.best_pos = s_state.pos;

        if ((s_state.pos - s_state.low) > (s_state.high - s_state.pos)) {
            // we are in the lower half, so sift down first and then up
            res = CALL(sylvan_siftdown, &s_state);
            if (sylvan_varswap_issuccess(res)) {
                res = CALL(sylvan_siftup, &s_state);
            }
        } else {
            // we are in the upper half, so sift up first and then down
            res = CALL(sylvan_siftup, &s_state);
            if (sylvan_varswap_issuccess(res)) {
                res = CALL(sylvan_siftdown, &s_state);
            }
        }

        reorder_result_t old_res = res;

        // optimum variable position restoration
        res = CALL(sylvan_siftpos, s_state.pos, s_state.best_pos);
        s_state.best_size = CALL(llmsset_count_marked, nodes);

        if (!sylvan_varswap_issuccess(res) || !sylvan_varswap_issuccess(old_res)) break;
        configs.total_num_var++;

        // if we managed to reduce size call progress hooks
        if (s_state.best_size < s_state.size) {
            for (re_hook_entry_t e = progre_list; e != NULL; e = e->next) {
                WRAP(e->cb);
            }
        }

        if (should_terminate_reordering(&configs)) break;
    }

    s_state.best_size = CALL(llmsset_count_marked, nodes);

    sylvan_timer_stop(SYLVAN_RE);
    configs.t_start_sifting = 0;

    // new size threshold for next reordering is SYLVAN_REORDER_SIZE_RATIO * the size of non-terminal nodes + the terminal nodes
    size_t new_size_threshold = (s_state.best_size - leaf_count + 1) * SYLVAN_REORDER_SIZE_RATIO + leaf_count;
    if (levels->reorder_count < SYLVAN_REORDER_LIMIT || new_size_threshold > levels->reorder_size_threshold) {
        levels->reorder_size_threshold = new_size_threshold;
    } else {
        levels->reorder_size_threshold += SYLVAN_REORDER_LIMIT;
    }

    if (print_reordering_stat) {
        printf("BDD reordering with simple sifting: from %zu to ... %d nodes in %f sec\n",
               before_size, s_state.best_size, wctime() - t_start_sifting);
    }

    for (re_hook_entry_t e = postre_list; e != NULL; e = e->next) {
        WRAP(e->cb);
    }

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