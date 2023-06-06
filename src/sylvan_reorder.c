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
#include "sylvan_align.h"

#define STATS 0 // useful information w.r.t. dynamic reordering

static int reorder_initialized = 0;
static int print_reordering_stat = 1;

struct sifting_config
{
    double t_start_sifting;                     // start time of the sifting
    uint32_t threshold;                         // threshold for number of nodes per level
    double max_growth;                          // coefficient used to calculate maximum growth
    uint32_t max_swap;                          // maximum number of swaps per sifting
    uint32_t varswap_count;                    // number of swaps completed
    uint32_t max_var;                           // maximum number of vars swapped per sifting
    uint32_t total_num_var;                     // number of vars sifted
    double time_limit_ms;                       // time limit in milliseconds
    reordering_type_t type;                     // type of reordering algorithm
};

/// reordering configurations
static struct sifting_config configs = {
        .t_start_sifting = 0,
        .threshold = SYLVAN_REORDER_NODES_THRESHOLD,
        .max_growth = SYLVAN_REORDER_GROWTH,
        .max_swap = SYLVAN_REORDER_MAX_SWAPS,
        .varswap_count = 0,
        .max_var = SYLVAN_REORDER_MAX_VAR,
        .total_num_var = 0,
        .time_limit_ms = SYLVAN_REORDER_TIME_LIMIT_MS,
        .type = SYLVAN_REORDER_TYPE_DEFAULT
};

VOID_TASK_DECL_1(sylvan_reorder_stop_world, reordering_type_t)

TASK_DECL_2(reorder_result_t, sylvan_sift, uint32_t, uint32_t)

TASK_DECL_2(reorder_result_t, sylvan_bounded_sift, uint32_t, uint32_t)

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

static inline uint64_t get_nodes_count()
{
#if STATS
    assert(levels_nodes_count_load(levels) == (llmsset_count_marked(nodes) + 2));
#endif
#if SYLVAN_USE_LINEAR_PROBING
    return llmsset_count_marked(nodes) + 2;
#else
    return levels_nodes_count_load(levels);
#endif
}


void sylvan_reorder_type_description(reordering_type_t type, char *buf, size_t buf_len)
{
    (void) buf_len;
    assert(buf_len >= 100);
    switch (type) {
        case SYLVAN_REORDER_BOUNDED_SIFT:
            sprintf(buf, "bounded sifting");
            break;
        case SYLVAN_REORDER_SIFT:
            sprintf(buf, "sifting");

    }
}


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

void sylvan_set_reorder_nodes_threshold(uint32_t threshold)
{
    assert(threshold > 1);
    configs.threshold = threshold;
}

void sylvan_set_reorder_maxgrowth(float max_growth)
{
    assert(max_growth > 1.0f);
    configs.max_growth = max_growth;
}

void sylvan_set_reorder_maxswap(uint32_t max_swap)
{
    assert(max_swap > 1);
    configs.max_swap = max_swap;
}

void sylvan_set_reorder_maxvar(uint32_t max_var)
{
    assert(max_var > 1);
    configs.max_var = max_var;
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
    configs.time_limit_ms = time_limit;
}

void sylvan_set_reorder_verbose(int is_verbose)
{
    assert(is_verbose >= 0);
    print_reordering_stat = is_verbose;
}

void sylvan_set_reorder_type(reordering_type_t type)
{
    configs.type = type;
}

TASK_IMPL_1(reorder_result_t, sylvan_siftdown, sifting_state_t *, s_state)
{
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;
#if STATS
    printf("\n");
#endif
    reorder_result_t res;
    int R;  // upper bound on node decrease
    int limitSize;
    BDDVAR xIndex;
    BDDVAR yIndex;
    BDDVAR x;
    BDDVAR y;

    s_state->size = (int) get_nodes_count();
    xIndex = levels->level_to_order[s_state->pos];

    limitSize = s_state->size = s_state->size - levels->isolated_count;
    R = 0;

    // Let <x> be the variable at level <pos>. (s_state->pos)
    // Let <y> be the variable at level <pos+1>. (s_state->pos+1)
    // Let Ni be the number of nodes at level i.
    // Let n be the number of levels. (levels->count)

    // Then the size of DD can not be reduced below:
    // LB(DN) = Nj + ∑ Ni | 0<i<pos

    // The part of the DD above <x> will not change.
    // The part of the DD below <x> that does not interact with <x> will not change.
    // The rest may vanish in the best case, except for
    // the nodes at level <high>, which will not vanish, regardless.
#if STATS
    printf("R: %d, limitSize: %d, all_nodes: %d, all_isolated: %d\n",
           R, limitSize, s_state->size, levels->isolated_count);
#endif
    // Initialize the upper bound
    for (y = s_state->high; y > s_state->pos; y--) {
        yIndex = levels->level_to_order[y];
        if (interact_test(levels, xIndex, yIndex)) {
            R += (int) levels_var_count_load(levels, y) - levels_is_isolated(levels, yIndex);
#if STATS
            printf("R: %d, xindex: %d, yindex: %d\n", R, xIndex, yIndex);
#endif
        } else {
#if STATS
            printf("R: %d, xindex: %d, yindex: %d (no interaction)\n", R, xIndex, yIndex);
#endif
        }
    }

    x = s_state->pos;
    y = s_state->pos + 1;
#if STATS
    printf("sift down pos: x: %d (R: %d, size: %d, limitSize: %d) (xNodes: %d, yNodes: %d)\n",
           x, R, s_state->size, limitSize,
           levels_var_count_load(levels, x),
           levels_var_count_load(levels, y)
    );
#endif
    for (; s_state->pos < s_state->high && s_state->size - R < limitSize; ++s_state->pos) {
        x = s_state->pos;
        y = s_state->pos + 1;
        //  Update the upper bound on node decrease
        yIndex = levels->level_to_order[y];
        if (interact_test(levels, xIndex, yIndex)) {
            R -= (int) levels_var_count_load(levels, y) - levels_is_isolated(levels, yIndex);
        }
        res = sylvan_varswap(x);
        s_state->size = (int) get_nodes_count();
        if (!sylvan_reorder_issuccess(res)) return res;
        configs.varswap_count++;

        // check the max allowed size growth
        if ((double) (s_state->size) > (double) s_state->best_size * configs.max_growth) {
            ++s_state->pos;
            break;
        }

        // update best position
        if (s_state->size <= s_state->best_size) {
            s_state->best_size = s_state->size;
            s_state->best_pos = s_state->pos;
        }

        if (s_state->size < limitSize) limitSize = s_state->size;
#if STATS
        printf("sift down pos: x: %d (R: %d, size: %d, limitSize: %d) (xNodes: %d, yNodes: %d)\n",
               x, R, s_state->size, limitSize,
               levels_var_count_load(levels, x),
               levels_var_count_load(levels, y)
        );
//        printf("\n");
//        for (size_t i = 0; i < levels->count; i++) {
//            printf("level %zu (%d) \t has %u nodes\n", i, levels->order_to_level[i], levels_var_count_load(levels, i));
//        }
//        printf("\n");
#endif
        if (should_terminate_sifting(&configs)) break;
    }

    if (s_state->size <= s_state->best_size) {
        s_state->best_size = s_state->size;
        s_state->best_pos = s_state->pos;
    }

#if STATS
    printf("\n");
    for (size_t i = 0; i < levels->count; i++) {
        printf("level %zu (%d) \t has %u nodes\n", i, levels->order_to_level[i], levels_var_count_load(levels, i));
    }
    printf("\n");
#endif
    return SYLVAN_REORDER_SUCCESS;
}

TASK_IMPL_1(reorder_result_t, sylvan_siftup, sifting_state_t *, s_state)
{
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;
#if STATS
    printf("\n");
#endif

    reorder_result_t res;
    int L;  // lower bound on DD size
    int limitSize;
    BDDVAR xIndex;
    BDDVAR yIndex;
    BDDVAR x;
    BDDVAR y;

    s_state->size = (int) get_nodes_count();
    yIndex = levels->level_to_order[s_state->pos];

    // Let <x> be the variable at level <pos-1>. (s_state->pos-1)
    // Let <y> be the variable at level <pos>. (s_state->pos)
    // Let Ni be the number of nodes at level i.
    // Let n be the number of levels. (levels->count)

    // Then the size of DD can not be reduced below:
    // LB(UP) = N0 + ∑ Ni | i<pos<n

    // The part of the DD below <y> will not change.
    // The part of the DD above <y> that does not interact with <y> will not change.
    // The rest may vanish in the best case, except for
    // the nodes at level <low>, which will not vanish, regardless.

    limitSize = L = (int) (s_state->size - levels->isolated_count);
#if STATS
    printf("L: %d, limitSize: %d, all_nodes: %d, all_isolated: %d\n",
           L, limitSize, s_state->size, levels->isolated_count);
#endif
    for (x = s_state->low + 1; x < s_state->pos; x++) {
        xIndex = levels->level_to_order[x];
        if (interact_test(levels, xIndex, yIndex)) {
            L -= (int) levels_var_count_load(levels, x) - levels_is_isolated(levels, xIndex);
#if STATS
            printf("L: %d, xindex: %d, yindex: %d\n", L, xIndex, yIndex);
#endif
        } else {
#if STATS
            printf("L: %d, xindex: %d, yindex: %d(no interaction)\n", L, xIndex, yIndex);
#endif
        }
    }
    x = s_state->pos - 1;
    y = s_state->pos;

    L -= (int) levels_var_count_load(levels, y) - levels_is_isolated(levels, yIndex);
#if STATS
    printf("sift up pos: x: %d (L: %d, size: %d, limitSize: %d) (xNodes: %d, yNodes: %d)\n",
           x, L, s_state->size, limitSize,
           levels_var_count_load(levels, x),
           levels_var_count_load(levels, y)
    );
#endif
    for (; s_state->pos > s_state->low && L <= limitSize; --s_state->pos) {
        x = s_state->pos - 1;
        y = s_state->pos;
        xIndex = levels->level_to_order[x];

        res = sylvan_varswap(x);
        if (!sylvan_reorder_issuccess(res)) return res;

        s_state->size = (int) get_nodes_count();
        configs.varswap_count++;

        // check the max allowed size growth
        if ((double) (s_state->size) > (double) s_state->best_size * configs.max_growth) {
            --s_state->pos;
            break;
        }

        // update the best position
        if (s_state->size <= s_state->best_size) {
            s_state->best_size = s_state->size;
            s_state->best_pos = s_state->pos;
        }

        // Update the lower bound on DD size
        if (interact_test(levels, xIndex, yIndex)) {
            L += (int) levels_var_count_load(levels, y) - levels_is_isolated(levels, xIndex);
        }

        if ((int) s_state->size < limitSize) limitSize = (int) s_state->size;
#if STATS
        printf("sift up pos: x: %d (L: %d, size: %d, limitSize: %d) (xNodes: %d, yNodes: %d)\n",
               x, L, s_state->size, limitSize,
               levels_var_count_load(levels, x),
               levels_var_count_load(levels, y)
        );
#endif
#if 0
        printf("\n");
        for (size_t i = 0; i < levels->count; i++) {
            printf("level %zu (%d) \t has %u nodes\n", i, levels->order_to_level[i], levels_var_count_load(levels, i));
        }
        printf("\n");
#endif
        if (should_terminate_sifting(&configs)) break;
    }

    if (s_state->size <= s_state->best_size) {
        s_state->best_size = s_state->size;
        s_state->best_pos = s_state->pos;
    }

#if STATS
    printf("\n");
    for (size_t i = 0; i < levels->count; i++) {
        printf("level %zu (%d) \t has %u nodes\n", i, levels->order_to_level[i], levels_var_count_load(levels, i));
    }
    printf("\n");
#endif
    return SYLVAN_REORDER_SUCCESS;
}

TASK_IMPL_1(reorder_result_t, sylvan_siftback, sifting_state_t *, s_state)
{
    reorder_result_t res = SYLVAN_REORDER_SUCCESS;
#if STATS
    printf("\n");
#endif
    if (!reorder_initialized) return SYLVAN_REORDER_NOT_INITIALISED;
    if (s_state->pos == s_state->best_pos) return res;
    for (; s_state->pos <= s_state->best_pos; s_state->pos++) {
        if (s_state->size == s_state->best_size) return res;
        if (s_state->pos == UINT32_MAX) return res;
#if STATS
        printf("sift back: x: %d \t y: %d (size: %d)\n", s_state->pos, s_state->pos + 1, s_state->size);
#endif
        res = sylvan_varswap(s_state->pos);
        s_state->size = (int) get_nodes_count();
        if (!sylvan_reorder_issuccess(res)) return res;
        configs.varswap_count++;
    }
    for (; s_state->pos >= s_state->best_pos; s_state->pos--) {
        if (s_state->pos) break;
        if (s_state->size == s_state->best_size) return res;
#if STATS
        printf("sift back: x: %d \t y: %d (size: %d)\n", s_state->pos - 1, s_state->pos, s_state->size);
#endif
        res = sylvan_varswap(s_state->pos - 1);
        s_state->size = (int) get_nodes_count();
        if (!sylvan_reorder_issuccess(res)) return res;
        configs.varswap_count++;
    }
    return res;
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
    if (llmsset_count_marked(nodes) >= levels->reorder_size_threshold && levels->reorder_count < SYLVAN_REORDER_LIMIT) {
        sylvan_reduce_heap(configs.type);
    }
}

void sylvan_reduce_heap(reordering_type_t type)
{
    RUNEX(sylvan_reorder_stop_world, type);
}

/**
 * This variable is used for a cas flag so only
 * one reordering runs at one time
 */
static _Atomic (int) re;

VOID_TASK_IMPL_1(sylvan_pre_reorder, reordering_type_t, type)
{
    sylvan_gc_test();

    char buff[100];
    sylvan_reorder_type_description(type, buff, 100);
#if SYLVAN_USE_LINEAR_PROBING
    printf("BDD reordering with %s (probing): from %zu to ... ", buff, llmsset_count_marked(nodes));
#else
    printf("BDD reordering with %s (chaining): from %zu to ... ", buff, llmsset_count_marked(nodes));
#endif
    // alloc necessary memory dependent on table_size/ # of variables
    // let:
    // v - number of variables
    // n - number of nodes

    levels_var_count_malloc(levels->count);             // memory usage: # of variables * 16 bits                (16v)
    levels_ref_count_malloc(levels->count);             // memory usage: # of variables * 16 bits                (16v)
    levels_node_ref_count_malloc(nodes->table_size);    // memory usage: # of nodes * 16 bits                    (16n)
    levels_bitmap_p2_malloc(nodes->table_size);         // memory usage: # of nodes * 1 bit                      (n)
    levels_bitmap_p3_malloc(nodes->table_size);         // memory usage: # of nodes * 1 bit                      (n)
    levels_bitmap_ext_malloc(nodes->table_size);        // memory usage: # of nodes * 1 bit                      (n)
    // at the moment unfortunately, interaction matrix requires consecutive variables without **gaps**
    interact_malloc(levels);                            // memory usage: # of variables * # of variables * 1 bit (v^2)

//    Reordering memory composition in bits is: 32v + 19n + v^2
//    O(v^2 + n) quadratic space complexity

    mtbdd_re_mark_external_refs(levels->bitmap_ext);

    levels->reorder_count++;
    levels->isolated_count = 0;

    sylvan_stats_count(SYLVAN_RE_COUNT);
    sylvan_timer_start(SYLVAN_RE);

    for (re_hook_entry_t e = prere_list; e != NULL; e = e->next) {
        WRAP(e->cb);
    }

    configs.t_start_sifting = wctime();
    configs.total_num_var = 0;
}

VOID_TASK_IMPL_1(sylvan_post_reorder, size_t, leaf_count)
{
    size_t after_size = get_nodes_count();

    // new size threshold for next reordering is double the size of non-terminal nodes + the terminal nodes
    size_t new_size_threshold = (after_size - leaf_count + 1) * SYLVAN_REORDER_SIZE_RATIO + leaf_count;
    if (levels->reorder_count < SYLVAN_REORDER_LIMIT || new_size_threshold > levels->reorder_size_threshold) {
        levels->reorder_size_threshold = new_size_threshold;
    } else {
        levels->reorder_size_threshold += SYLVAN_REORDER_LIMIT;
    }

    levels_var_count_free();
    levels_ref_count_free();
    levels_node_ref_count_free();
    levels_bitmap_p2_free();
    levels_bitmap_p3_free();
    levels_bitmap_ext_free();

    double end = wctime() - configs.t_start_sifting;
    if (print_reordering_stat) {
        printf("%zu nodes in %f sec\n", after_size, end);
    }

    for (re_hook_entry_t e = postre_list; e != NULL; e = e->next) {
        WRAP(e->cb);
    }

    sylvan_timer_stop(SYLVAN_RE);
}

VOID_TASK_0(sylvan_test_sift)
{
    interaction_matrix_init(levels);
    var_ref_init(levels);

    int perm_add4[208] = {
            5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 3, 2, 1, 0,
            0, 1, 2, 3, 4, 5, 6, 7, 7,
            6, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 7, 6, 5, 4, 7, 8,
            8, 7, 6, 5, 4, 3, 2, 1, 0, 0,
            1, 2, 3, 4, 5, 6, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 7, 6, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 2,
            0, 0, 1, 2, 3, 4, 5, 6, 7, 8,
            8, 7, 6, 5, 0, 1, 2, 3, 4, 5, 6, 6, 5, 4, 3, 2, 9, 10, 10, 9, 8, 7, 6, 5, 5, 6, 7, 8, 9, 12, 12, 11, 10, 9,
            8, 7, 6, 5, 4, 3, 2, 1, 0, 10,
            10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
    };
    int perm_add8[745] = {
            14, 15, 16, 17, 18, 19, 20, 21, 22, 22, 21, 20, 19, 18, 17, 16, 15, 14, 14, 15, 16, 17, 18, 19, 20, 15, 16,
            16, 15, 14, 13, 12, 11, 10, 9,
            8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 14, 15, 16, 17, 18, 19, 20, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 9,
            10, 11, 12, 13, 14, 15, 16, 17,
            18, 13, 14, 15, 16, 17, 18, 19, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 9, 10, 11, 12, 13, 14, 15, 16,
            11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
            0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 18, 17, 16, 11, 10, 9, 8, 7, 6, 5, 4,
            3, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
            13, 14, 15, 16, 17, 18, 19, 20, 20, 19, 18, 17, 16, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6,
            7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
            16, 15, 14, 13, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 12, 11, 10,
            9, 8, 7, 6, 5, 4, 9, 8, 7, 6, 5, 4,
            3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 2, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 1, 2, 3, 4, 5, 6, 7, 8,
            9, 10, 10, 9, 8, 7, 6, 11, 10, 9,
            8, 7, 6, 5, 4, 3, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 14, 13, 12, 11, 10, 9, 8, 10, 9, 8,
            7, 6, 5, 4, 3, 2, 1, 1, 2, 3, 4, 5,
            6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 20, 19, 18, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1,
            2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
            14, 15, 16, 17, 18, 19, 19, 18, 17, 16, 15, 14, 11, 10, 9, 8, 7, 6, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
            16, 15, 14, 13, 9, 8, 7, 6, 5, 4, 3, 2,
            1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 20, 19, 18, 17, 8, 7, 6, 5, 4, 3,
            2, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
            12, 13, 14, 15, 16, 17, 18, 18, 17, 16, 15, 14, 16, 17, 18, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 9, 10,
            11, 12, 13, 14, 6, 5, 4, 3, 2, 1, 1, 2, 3,
            4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 17, 16, 15, 14, 13, 12, 11, 10, 9, 3, 2, 1, 1, 2, 3, 4, 5,
            6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
            16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 14, 13,
            12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 20, 20,
            19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 10, 11, 12, 13, 14, 15, 16, 17, 18, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
            10, 11, 12, 13, 14, 14, 13, 12, 11, 10,
            9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 22, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 8, 9, 10, 11,
            12, 13, 14, 15, 16, 17, 18, 19, 20, 24,
            24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 23, 22, 21, 20,
            19, 18, 17, 16, 15, 14, 13, 12, 11, 10,
            9, 8, 7, 6, 5, 4, 3, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15,
            14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4,
            3, 2, 1, 0
    };
    (void) perm_add4;
    (void) perm_add8;

    int size_add8 = 745;
    int size_add4 = 208;
    (void) size_add8;
    (void) size_add4;

    for (int i = 0; i < size_add8; i++) {
        int pos = perm_add8[i];
        CALL(sylvan_varswap, pos);
    }
}

VOID_TASK_IMPL_1(sylvan_reorder_stop_world, reordering_type_t, type)
{
    reorder_result_t result = SYLVAN_REORDER_SUCCESS;
    if (!reorder_initialized) result = SYLVAN_REORDER_NOT_INITIALISED;
    if (levels->count < 1) result = SYLVAN_REORDER_NO_REGISTERED_VARS;
    if (sylvan_reorder_issuccess(result) == 0) {
        sylvan_print_reorder_res(result);
        return;
    }
    int zero = 0;
    if (atomic_compare_exchange_strong(&re, &zero, 1)) {
        size_t leaf_count = 0;
        sylvan_pre_reorder(type);
        switch (type) {
            case SYLVAN_REORDER_SIFT:
                result = NEWFRAME(sylvan_sift, 0, 0);
                break;
            case SYLVAN_REORDER_BOUNDED_SIFT:
                result = NEWFRAME(sylvan_bounded_sift, 0, 0);
                break;
        }
        if (sylvan_reorder_issuccess(result) == 0) {
            sylvan_print_reorder_res(result);
        }
        re = 0;
        sylvan_post_reorder(leaf_count);
    } else {
        /* wait for new frame to appear */
        while (atomic_load_explicit(&lace_newframe.t, memory_order_relaxed) == 0) {}
        lace_yield(__lace_worker, __lace_dq_head);
    }
}

TASK_IMPL_2(reorder_result_t, sylvan_sift, uint32_t, low, uint32_t, high)
{
    // if high == 0, then we sift all variables
    if (high == 0) high = levels->count - 1;

    interaction_matrix_init(levels);
    var_ref_init(levels);

    // count all variable levels (parallel...)
    _Atomic (size_t) level_counts[levels->count];
    for (size_t i = 0; i < levels->count; i++) {
        level_counts[i] = levels_var_count_load(levels, levels->level_to_order[i]);
    }
    // mark and sort variable levels based on the threshold
    int ordered_levels[levels->count];
    mtbdd_mark_threshold(ordered_levels, level_counts, 0);
    gnome_sort(ordered_levels, level_counts);

    reorder_result_t res;

    size_t cursize = llmsset_count_marked(nodes) + 2;

    for (int i = 0; i < (int) levels->count; i++) {
        int lvl = ordered_levels[i];
        if (lvl < 0) break; // done
        size_t pos = levels->level_to_order[lvl];

        size_t bestpos = pos;
        size_t bestsize = cursize;

        if (pos < low || pos > high) continue;

        configs.varswap_count = 0;

        if ((pos - low) > (high - pos)) {
            // we are in the lower half of the levels, so sift down first and then up
            // sifting down
            for (; pos < high; pos++) {
                res = CALL(sylvan_varswap, pos);
                if (sylvan_reorder_issuccess(res) == 0) break;
                cursize = llmsset_count_marked(nodes) + 2;
                configs.varswap_count++;
                if (should_terminate_sifting(&configs)) break;
                if ((double) cursize > (double) bestsize * configs.max_growth) {
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
                    res = CALL(sylvan_varswap, pos - 1);
                    if (sylvan_reorder_issuccess(res) == 0) break;
                    cursize = llmsset_count_marked(nodes) + 2;
                    configs.varswap_count++;
                    if (should_terminate_sifting(&configs)) break;
                    if ((double) cursize > (double) bestsize * configs.max_growth) {
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
                res = CALL(sylvan_varswap, pos - 1);
                if (sylvan_reorder_issuccess(res) == 0) break;
                cursize = llmsset_count_marked(nodes) + 2;
                configs.varswap_count++;
                if (should_terminate_sifting(&configs)) break;
                if ((double) cursize > (double) bestsize * configs.max_growth) {
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
                    res = CALL(sylvan_varswap, pos);
                    if (sylvan_reorder_issuccess(res) == 0) break;
                    cursize = llmsset_count_marked(nodes) + 2;
                    configs.varswap_count++;
                    if (should_terminate_sifting(&configs)) break;
                    if ((double) cursize > (double) bestsize * configs.max_growth) {
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
            res = CALL(sylvan_varswap, pos);
            if (sylvan_reorder_issuccess(res) == 0) break;
            configs.varswap_count++;
        }
        for (; pos > bestpos; pos--) {
            res = CALL(sylvan_varswap, pos - 1);
            if (sylvan_reorder_issuccess(res) == 0) break;
            configs.varswap_count++;
        }

        cursize = llmsset_count_marked(nodes) + 2;

        if (!sylvan_reorder_issuccess(res) || !sylvan_reorder_issuccess(old_res)) break;
        configs.total_num_var++;

        // if we managed to reduce size call progress hooks
        if (bestsize < cursize) {
            for (re_hook_entry_t e = progre_list; e != NULL; e = e->next) {
                WRAP(e->cb);
            }
        }

        if (should_terminate_reordering(&configs)) break;
    }

    return res;
}

TASK_IMPL_2(reorder_result_t, sylvan_bounded_sift, uint32_t, low, uint32_t, high)
{
    // if high == 0, then we sift all variables
    if (high == 0) high = levels->count - 1;

    // parallel
    interaction_matrix_init(levels);
    var_ref_init(levels);

    // count all variable levels (parallel...)
    _Atomic (size_t) level_counts[levels->count];
    for (size_t i = 0; i < levels->count; i++) {
        level_counts[i] = levels_var_count_load(levels, levels->level_to_order[i]);
    }
    // mark and sort variable levels based on the threshold
    int ordered_levels[levels->count];
    mtbdd_mark_threshold(ordered_levels, level_counts, 0);
    gnome_sort(ordered_levels, level_counts);

    atomic_half_word_t level_to_order[levels->count];
    for (size_t i = 0; i < levels->count; i++) {
        level_to_order[i] = levels->level_to_order[i];
    }

#if 0
    for (size_t level = 0; level < sylvan_levelscount(); ++level) {
        size_t index = llmsset_first();
        while (index != llmsset_nindex) {
            mtbddnode_t node = MTBDD_GETNODE(index);
            BDDVAR var = mtbddnode_getvariable(node);
            if (var == level) {
                counter_t int_refs = levels_node_ref_count_load(levels, index);
                counter_t has_ext_ref = levels_ext_is_marked(index);
                printf("node %u \t has %hu refs (ext: %hu)\n", var, int_refs, has_ext_ref);
            }
            index = llmsset_next(index);
        }
    }
    printf("\n");
#endif

    reorder_result_t res = SYLVAN_REORDER_SUCCESS;
    sifting_state_t s_state;

    s_state.pos = 0;
    s_state.best_pos = 0;
    s_state.size = (int) get_nodes_count();
    s_state.best_size = s_state.size;
    s_state.low = low;
    s_state.high = high;

#if STATS
//    interact_print_state(levels);

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

        configs.varswap_count = 0;

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

        if (should_terminate_reordering(&configs)) break;

        // if we managed to reduce size call progress hooks
        if (s_state.best_size < s_state.size) {
            for (re_hook_entry_t e = progre_list; e != NULL; e = e->next) {
                WRAP(e->cb);
            }
        }

        configs.total_num_var++;
#if STATS
        if (configs.total_num_var > 3) exit(1);
#endif
        continue;

        siftingFailed:
        if (res == SYLVAN_REORDER_P2_CREATE_FAIL || res == SYLVAN_REORDER_P3_CLEAR_FAIL) {
            printf("\nRunning out of memory. (Running GC and table resizing.)\n");
            levels_var_count_free();
            levels_ref_count_free();
            levels_node_ref_count_free();
            levels_bitmap_p2_free();
            levels_bitmap_p3_free();
            levels_bitmap_ext_free();

            sylvan_gc();

            levels_var_count_malloc(levels->count);
            levels_ref_count_malloc(levels->count);
            levels_node_ref_count_malloc(nodes->table_size);
            levels_bitmap_p2_malloc(nodes->table_size);
            levels_bitmap_p3_malloc(nodes->table_size);
            levels_bitmap_ext_malloc(nodes->table_size);
            interact_malloc(levels);

            mtbdd_re_mark_external_refs(levels->bitmap_ext);

            return CALL(sylvan_bounded_sift, low, high);
        } else {
            return res;
        }

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
    if (reorder_config->varswap_count > reorder_config->max_swap) {
#if STATS
        printf("sifting exit: reached %u from the total_num_swap %u\n",
               reorder_config->varswap_count,
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
        printf("reordering exit: reached %fms from the time_limit %.2zums\n",
               t_elapsed,
               (size_t) reorder_config->time_limit_ms);
#endif
        return 1;
    }
    return 0;
}