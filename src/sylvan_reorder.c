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

#define STATS 0 // useful information w.r.t. dynamic reordering for debugging
#define INFO 1  // useful information w.r.t. dynamic reordering

VOID_TASK_DECL_1(sylvan_reorder_stop_world, reordering_type_t)

#define sylvan_reorder_stop_world(type) RUN(sylvan_reorder_stop_world, type)

TASK_DECL_2(reorder_result_t, sylvan_sift, uint32_t, uint32_t)

#define sylvan_sift(v, limit) CALL(sylvan_sift, v, limit)

TASK_DECL_2(reorder_result_t, sylvan_bounded_sift, uint32_t, uint32_t)

#define sylvan_bounded_sift(v, limit) CALL(sylvan_bounded_sift, v, limit)

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
    reorder_db = reorder_db_init();
}

void sylvan_quit_reorder()
{
    reorder_db_deinit(reorder_db);
}

void sylvan_set_reorder_nodes_threshold(uint32_t threshold)
{
    assert(threshold > 0);
    reorder_db->config.threshold = threshold;
}

void sylvan_set_reorder_maxgrowth(float max_growth)
{
    assert(max_growth > 1.0f);
    reorder_db->config.max_growth = max_growth;
}

void sylvan_set_reorder_maxswap(uint32_t max_swap)
{
    assert(max_swap > 1);
    reorder_db->config.max_swap = max_swap;
}

void sylvan_set_reorder_maxvar(uint32_t max_var)
{
    assert(max_var > 1);
    reorder_db->config.max_var = max_var;
}

void sylvan_set_reorder_timelimit_min(double time_limit)
{
    assert(time_limit > 0);
    sylvan_set_reorder_timelimit_sec(time_limit * 60);
}

void sylvan_set_reorder_timelimit_sec(double time_limit)
{
    assert(time_limit > 0);
    sylvan_set_reorder_timelimit_ms(time_limit * 1000);
}

void sylvan_set_reorder_timelimit_ms(double time_limit)
{
    assert(time_limit > 0);
    reorder_db->config.time_limit_ms = time_limit;
}

void sylvan_set_reorder_verbose(int is_verbose)
{
    assert(is_verbose >= 0);
    reorder_db->config.print_stat = is_verbose;
}

void sylvan_set_reorder_type(reordering_type_t type)
{
    reorder_db->config.type = type;
}

TASK_IMPL_1(reorder_result_t, sylvan_reorder_perm, const uint32_t*, permutation)
{
    if (!reorder_db->is_initialised) return SYLVAN_REORDER_NOT_INITIALISED;
    reorder_result_t res = SYLVAN_REORDER_SUCCESS;
    int is_identity = 1;

    // check if permutation is identity
    for (size_t level = 0; level < levels->count; level++) {
        if (permutation[level] != levels->level_to_order[level]) {
            is_identity = 0;
            break;
        }
    }
    if (is_identity) return res;

    for (size_t level = 0; level < levels->count; ++level) {
        uint32_t var = permutation[level];
        uint32_t pos = mtbdd_order_to_level(var);
        for (; pos < level; pos++) {
            res = sylvan_varswap(pos);
            if (!sylvan_reorder_issuccess(res)) return res;
        }
        for (; pos > level; pos--) {
            res = sylvan_varswap(pos - 1);
            if (!sylvan_reorder_issuccess(res)) return res;
        }
        if (!sylvan_reorder_issuccess(res)) break;
    }

    return res;
}

void sylvan_test_reduce_heap()
{
    if (llmsset_count_marked(nodes) >= reorder_db->config.size_threshold &&
        reorder_db->call_count < SYLVAN_REORDER_LIMIT) {
        sylvan_reduce_heap(reorder_db->config.type);
    }
}

void sylvan_reduce_heap(reordering_type_t type)
{
    sylvan_reorder_stop_world(type);
}

/**
 * This variable is used for a cas flag so only
 * one reordering runs at one time
 */
static _Atomic (int) re;


VOID_TASK_IMPL_1(sylvan_reorder_stop_world, reordering_type_t, type)
{
    reorder_result_t result = SYLVAN_REORDER_SUCCESS;
    if (!reorder_db->is_initialised) result = SYLVAN_REORDER_NOT_INITIALISED;
    if (levels->count < 1) result = SYLVAN_REORDER_NO_REGISTERED_VARS;
    if (sylvan_reorder_issuccess(result) == 0) {
        sylvan_print_reorder_res(result);
        return;
    }
    sylvan_pre_reorder(type);
    switch (type) {
        case SYLVAN_REORDER_SIFT:
            result = sylvan_sift(0, 0);
            break;
        case SYLVAN_REORDER_BOUNDED_SIFT:
            result = sylvan_bounded_sift(0, 0);
            break;
    }
    if (sylvan_reorder_issuccess(result) == 0) {
        sylvan_print_reorder_res(result);
    }
    re = 0;
    sylvan_post_reorder();
}

TASK_IMPL_2(reorder_result_t, sylvan_sift, uint32_t, low, uint32_t, high)
{
    // if high == 0, then we sift all variables
    if (high == 0) high = levels->count - 1;

    // count all variable levels (parallel...)
    _Atomic (size_t) level_counts[levels->count];
    for (size_t i = 0; i < levels->count; i++) {
        level_counts[i] = mrc_var_nnodes_get(&reorder_db->mrc, levels->level_to_order[i]);
    }
    // mark and sort variable levels based on the threshold
    int ordered_levels[levels->count];
    mtbdd_mark_threshold(ordered_levels, level_counts, 0);
    gnome_sort(ordered_levels, level_counts);

    reorder_result_t res = SYLVAN_REORDER_SUCCESS;

    size_t cursize = get_nodes_count();

    for (int i = 0; i < (int) levels->count; i++) {
        int lvl = ordered_levels[i];
        if (lvl < 0) break; // done
        size_t pos = levels->level_to_order[lvl];

        size_t bestpos = pos;
        size_t bestsize = cursize;

        if (pos < low || pos > high) continue;

        reorder_db->config.varswap_count = 0;

        if ((pos - low) > (high - pos)) {
            // we are in the lower half of the levels, so sift down first and then up
            // sifting down
            for (; pos < high; pos++) {
                res = sylvan_varswap(pos);
                if (sylvan_reorder_issuccess(res) == 0) break;
                cursize = get_nodes_count();
                reorder_db->config.varswap_count++;
                if (should_terminate_sifting(&reorder_db->config)) break;
                if ((double) cursize > (double) bestsize * reorder_db->config.max_growth) {
                    pos++;
                    break;
                }
                if (cursize < bestsize) {
                    bestsize = cursize;
                    bestpos = pos;
                }
            }
            if (sylvan_reorder_issuccess(res)) {
                // sifting up
                for (; pos > low; pos--) {
                    res = sylvan_varswap(pos - 1);
                    if (sylvan_reorder_issuccess(res) == 0) break;
                    cursize = get_nodes_count();
                    reorder_db->config.varswap_count++;
                    if (should_terminate_sifting(&reorder_db->config)) break;
                    if ((double) cursize > (double) bestsize * reorder_db->config.max_growth) {
                        pos--;
                        break;
                    }
                    if (cursize < bestsize) {
                        bestsize = cursize;
                        bestpos = pos;
                    }
                }
            }
        } else {
            // we are in the upper half of the levels, so sift up first and then down
            // sifting up
            for (; pos > low; pos--) {
                res = sylvan_varswap(pos - 1);
                if (sylvan_reorder_issuccess(res) == 0) break;
                cursize = get_nodes_count();
                reorder_db->config.varswap_count++;
                if (should_terminate_sifting(&reorder_db->config)) break;
                if ((double) cursize > (double) bestsize * reorder_db->config.max_growth) {
                    pos--;
                    break;
                }
                if (cursize < bestsize) {
                    bestsize = cursize;
                    bestpos = pos;
                }

            }
            if (sylvan_reorder_issuccess(res)) {
                // sifting down
                for (; pos < high; pos++) {
                    res = sylvan_varswap(pos);
                    if (sylvan_reorder_issuccess(res) == 0) break;
                    cursize = get_nodes_count();
                    reorder_db->config.varswap_count++;
                    if (should_terminate_sifting(&reorder_db->config)) break;
                    if ((double) cursize > (double) bestsize * reorder_db->config.max_growth) {
                        pos++;
                        break;
                    }
                    if (cursize < bestsize) {
                        bestsize = cursize;
                        bestpos = pos;
                    }
                }
            }
        }
        reorder_result_t old_res = res;

        // optimum variable position restoration
        for (; pos < bestpos; pos++) {
            res = sylvan_varswap(pos);
            if (sylvan_reorder_issuccess(res) == 0) break;
            reorder_db->config.varswap_count++;
        }
        for (; pos > bestpos; pos--) {
            res = sylvan_varswap(pos - 1);
            if (sylvan_reorder_issuccess(res) == 0) break;
            reorder_db->config.varswap_count++;
        }

        cursize = get_nodes_count();

        if (!sylvan_reorder_issuccess(res) || !sylvan_reorder_issuccess(old_res)) break;
        reorder_db->config.total_num_var++;

        // if we managed to reduce size call progress hooks
        if (bestsize < cursize) {
            reorder_db_call_progress_hooks();
        }

        if (should_terminate_reordering(&reorder_db->config)) break;
    }

    return res;
}

TASK_IMPL_2(reorder_result_t, sylvan_bounded_sift, uint32_t, low, uint32_t, high)
{
    // if high == 0, then we sift all variables
    if (high == 0) high = levels->count - 1;

    interact_init(&reorder_db->matrix, levels, levels->count, nodes->table_size);

    // count all variable levels
    _Atomic (size_t) level_counts[levels->count];
    for (size_t i = 0; i < levels->count; i++) {
        level_counts[i] = mrc_var_nnodes_get(&reorder_db->mrc, levels->level_to_order[i]);
    }
    // mark and sort variable levels based on the threshold
    int ordered_levels[levels->count];
    mtbdd_mark_threshold(ordered_levels, level_counts, reorder_db->config.threshold);
    gnome_sort(ordered_levels, level_counts);

    _Atomic (uint32_t) level_to_order[levels->count];
    for (size_t i = 0; i < levels->count; i++) {
        level_to_order[i] = levels->level_to_order[i];
    }

    reorder_result_t res = SYLVAN_REORDER_SUCCESS;
    sifting_state_t s_state;

    s_state.pos = 0;
    s_state.best_pos = 0;
    s_state.size = (int) get_nodes_count();
    s_state.best_size = s_state.size;
    s_state.low = low;
    s_state.high = high;

#if STATS
    printf("\n");
    interact_print_state(levels);

    for (size_t i = 0; i < levels->count; i++) {
        int lvl = ordered_levels[i];
        printf("level %d \t has %zu nodes\n", lvl, level_counts[lvl]);
    }
    printf("\n");
#endif

    for (int i = 0; i < (int) levels->count; i++) {
        int lvl = ordered_levels[i];
        s_state.pos = levels->order_to_level[level_to_order[lvl]];
        if (s_state.pos < s_state.low || s_state.pos > s_state.high) continue;

        reorder_db->config.varswap_count = 0;

        s_state.best_pos = s_state.pos;
        s_state.best_size = s_state.size;
#if STATS
        printf("sifting level %d with pos %d\n", s_state.pos, lvl);
#endif
        if (s_state.pos == s_state.low) {
            res = sylvan_siftdown(&s_state);
            if (!sylvan_reorder_issuccess(res)) goto siftingFailed;
            // at this point pos --> high unless bounding occurred.
            // move backward and stop at best position.
            res = sylvan_siftback(&s_state);
            if (!sylvan_reorder_issuccess(res)) goto siftingFailed;
        } else if (s_state.pos == s_state.high) {
            res = sylvan_siftup(&s_state);
            if (!sylvan_reorder_issuccess(res)) goto siftingFailed;
            // at this point pos --> low unless bounding occurred.
            // move backward and stop at best position.
            res = sylvan_siftback(&s_state);
            if (!sylvan_reorder_issuccess(res)) goto siftingFailed;
        } else if ((s_state.pos - s_state.low) > (s_state.high - s_state.pos)) {
            // we are in the lower half, so sift down first and then up
            res = sylvan_siftdown(&s_state);
            if (!sylvan_reorder_issuccess(res)) goto siftingFailed;
            res = sylvan_siftup(&s_state);
            if (!sylvan_reorder_issuccess(res)) goto siftingFailed;
            res = sylvan_siftback(&s_state);
            if (!sylvan_reorder_issuccess(res)) goto siftingFailed;
        } else {
            // we are in the upper half, so sift up first and then down
            res = sylvan_siftup(&s_state);
            if (!sylvan_reorder_issuccess(res)) goto siftingFailed;
            res = sylvan_siftdown(&s_state);
            if (!sylvan_reorder_issuccess(res)) goto siftingFailed;
            res = sylvan_siftback(&s_state);
            if (!sylvan_reorder_issuccess(res)) goto siftingFailed;
        }

        if (should_terminate_reordering(&reorder_db->config)) break;

        // if we managed to reduce size call progress hooks
        if (s_state.best_size < s_state.size) {
            reorder_db_call_progress_hooks();
        }

        reorder_db->config.total_num_var++;
#if STATS
        if (i > 1) exit(1);
#endif
        continue;

        siftingFailed:
        if (res == SYLVAN_REORDER_P2_CREATE_FAIL || res == SYLVAN_REORDER_P3_CLEAR_FAIL) {
#if INFO
            printf("\nRunning out of memory. (Running GC and table resizing.)\n");
#endif
            mrc_deinit(&reorder_db->mrc);
            interact_deinit(&reorder_db->matrix);

            sylvan_gc();

            mrc_init(&reorder_db->mrc, levels->count, nodes->table_size, reorder_db->node_ids);
            interact_init(&reorder_db->matrix, levels, levels->count, nodes->table_size);

            return CALL(sylvan_bounded_sift, low, high);
        } else {
#if INFO
            sylvan_print_reorder_res(res);
#endif
            return res;
        }
    }

    return res;
}