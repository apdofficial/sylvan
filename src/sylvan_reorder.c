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

#define LOG_ERROR(s, ...)   { if (ENABLE_ERROR_LOGS) fprintf(stderr, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); }
#define LOG_DEBUG(s, ...)   { if (ENABLE_DEBUG_LOGS) fprintf(stdout, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); }
#define LOG_INFO(s, ...)    { if (ENABLE_INFO_LOGS)  fprintf(stdout, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); }


static int mtbdd_reorder_initialized = 0;
static double t_start = 0.0;

struct sifting_config
{
    varswap_termination_cb      termination_cb;         // termination callback
    double                      t_start_sifting;        // start time of the sifting
    size_t                      level_count_threshold;  // threshold for number of nodes per level
    float                       max_growth;             // coefficient used to calculate maximum growth
    size_t                      max_swap;               // maximum number of swaps per sifting
    size_t                      total_num_swap;         // number of swaps completed
    size_t                      max_var;                // maximum number of vars sifted
    size_t                      total_num_var;          // maximum number of vars sifted
    unsigned long               time_limit;	            // time limit in milliseconds
};

/// reordering configuration default values
// TODO: determine optimal config defaults
static sifting_config_t configs =  {
        .termination_cb = NULL,
        .t_start_sifting = 0,
        .level_count_threshold = 32,
        .max_growth = 1.2f,
        .max_swap = 5000,
        .total_num_swap = 0,
        .max_var = 200,
        .total_num_var = 0,
        .time_limit = 2000
};

VOID_TASK_DECL_2(get_sorted_level_counts, int*, size_t);
/**
 * @brief Count and sort all variable levels (parallel...)
 *
 * \details Order all the variables using gnome sort according to the number of entries in each level.
 *
 * \param level_counts - array of size mtbdd_levels_size()
 * \param threshold - only count levels which have at least threshold number of variables.
 * If level is skipped assign it -1.
 *
 */
#define get_sorted_level_counts(level_counts, threshold) RUN(get_sorted_level_counts, level_counts, threshold)

/* Obtain current wallclock time */
static double wctime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

void sylvan_set_reordering_termination_cb(varswap_termination_cb callback)
{
    configs.termination_cb = callback;
}

void sylvan_set_reordering_threshold(size_t threshold)
{
    configs.level_count_threshold = threshold;
}

void sylvan_set_reordering_max_growth(float max_growth)
{
    configs.max_growth = max_growth;
}

void sylvan_set_reordering_max_swap(size_t max_swap)
{
    configs.max_swap = max_swap;
}

void sylvan_set_reordering_max_var(size_t max_var)
{
    configs.max_var = max_var;
}

void sylvan_set_reordering_time_limit(size_t time_limit)
{
    configs.time_limit = time_limit;
}


static void reorder_quit()
{
    sylvan_levels_destroy();
    mtbdd_reorder_initialized = 0;
    sylvan_varswap_quit();
    t_start = 0.0;
}

void sylvan_init_reorder()
{
    t_start = wctime();
    sylvan_init_mtbdd();
    sylvan_varswap_init();

    if (mtbdd_reorder_initialized) return;
    mtbdd_reorder_initialized = 1;

    sylvan_register_quit(&reorder_quit);
    mtbdd_levels_gc_add_mark_managed_refs();
}

static void gnome_sort(int *level, const size_t *level_counts)
{
    unsigned int i = 1;
    unsigned int j = 2;
    while (i < mtbdd_levels_size()) {
        long p = level[i - 1] == -1 ? -1 : (long) level_counts[mtbdd_levels_level_to_var(level[i - 1])];
        long q = level[i] == -1 ? -1 : (long) level_counts[mtbdd_levels_level_to_var(level[i])];
        if (p < q) {
            int t = level[i];
            level[i] = level[i - 1];
            level[i - 1] = t;
            if (--i) continue;
        }
        i = j++;
    }
}

VOID_TASK_IMPL_2(get_sorted_level_counts, int*, level, size_t, threshold)
{
    size_t level_counts[mtbdd_levels_size()];
    for (size_t i = 0; i < mtbdd_levels_size(); i++) level_counts[i] = 0;

    clock_t t = clock();
    mtbdd_levels_count_nodes(level_counts);

    LOG_DEBUG("mtbdd_levels_count_nodes()  took %f seconds\n", (((double)(clock() - t))/CLOCKS_PER_SEC));

    // set levels below the threshold to -1
    for (int i = 0; i < (int)mtbdd_levels_size(); i++) {
        LOG_DEBUG("level %d contains %zu nodes\n", i, level_counts[i]);
        if (level_counts[mtbdd_levels_level_to_var(i)] < threshold) {
            level[i] = -1;
        }
        else {
            level[i] = i;
        }
    }

    t = clock();
    gnome_sort(level, level_counts);
    LOG_DEBUG("gnome_sort                  took %f seconds\n", (((double)(clock() - t))/CLOCKS_PER_SEC));
}

VOID_TASK_IMPL_5(sift_down,
                 size_t*, pos,
                 size_t, high,
                 size_t*, curSize,
                 size_t*, bestSize,
                 size_t*, bestPos
){
    for (; *pos < high; *pos = *pos + 1){
        sylvan_varswap_res_t res = sylvan_simple_varswap(*pos);
        configs.total_num_swap++;
        if (res != SYLVAN_VARSWAP_SUCCESS){
            LOG_ERROR("sift(UP): failed due to %d\n", res);
            break;
        }
        size_t after = llmsset_count_marked(nodes);
        LOG_DEBUG("sift(UP): from %zu to %zu\n", *curSize, after);
        *curSize = after;
        if (*curSize < *bestSize) {
            LOG_DEBUG("sift(UP):   Improved size from %zu to %zu\n", *bestSize, *curSize);
            *bestSize = *curSize;
            *bestPos = *pos;
        }
        if ((float)(*curSize) >= configs.max_growth * (float)(*bestSize)) {
            *pos = *pos + 1;
            break;
        }
    }
    //TODO: return varswap_res_t
}

VOID_TASK_IMPL_5(sift_up,
                 size_t*, pos,
                 size_t, low,
                 size_t*, curSize,
                 size_t*, bestSize,
                 size_t*, bestPos
){
    for (; *pos > low; *pos = *pos - 1) {
        if(configs.total_num_var > configs.max_var) break;
        sylvan_varswap_res_t res = sylvan_simple_varswap(*pos - 1);
        configs.total_num_swap++;
        if (res != SYLVAN_VARSWAP_SUCCESS){
            LOG_ERROR("sift(DN): failed due to %d\n", res);
            break;
        }
        size_t after = llmsset_count_marked(nodes);
        LOG_DEBUG("sift(DN): from %zu to %zu\n", *curSize, after);
        *curSize = after;
        if (*curSize < *bestSize){
            LOG_DEBUG("sift(DN): Improved size from %zu to %zu\n", *bestSize, *curSize);
            *bestSize = *curSize;
            *bestPos = *pos;
        }
        if ((float)(*curSize) >= configs.max_growth * (float)(*bestSize)) {
            *pos = *pos - 1;
            break;
        }
    }
    //TODO: return varswap_res_t
}

VOID_TASK_IMPL_2(sift_to_pos, size_t, pos, size_t, targetPos)
{
    for (; pos < targetPos; pos++){
        sylvan_varswap_res_t res = sylvan_simple_varswap(pos);
        configs.total_num_swap++;
        if (res != SYLVAN_VARSWAP_SUCCESS){
            LOG_ERROR("sift_to_pos: failed due to %d\n", res);
            break;
        }
    }
    for (; pos > targetPos; pos--){
        sylvan_varswap_res_t res = sylvan_simple_varswap(pos - 1);
        configs.total_num_swap++;
        if (res != SYLVAN_VARSWAP_SUCCESS){
            LOG_ERROR("sift_to_pos: failed due to %d\n", res);
            break;
        }
    }
}

VOID_TASK_IMPL_2(sylvan_sifting_new, uint32_t, low,uint32_t, high)
{
    // TODO: implement variable interaction check (look at (Ebendt et al. in RT [12]) and CUDD)
    // TODO: implement variable order lock (its order will never be changed)

     if (mtbdd_levels_size() < 1) return;

    configs.t_start_sifting = wctime();
    configs.total_num_swap = 0;
    configs.total_num_var = 0;

    size_t before_size = llmsset_count_marked(nodes);

    // if high == 0, then we sift all variables
    if (high == 0)  high = mtbdd_levels_size() - 1;
    LOG_INFO("sifting between %d and %d\n", low, high);

    // Count all the variables and order their levels according to the
    // number of entries in each level (parallel operation)
    int level[mtbdd_levels_size()];
    get_sorted_level_counts(level, configs.level_count_threshold);

#if ENABLE_DEBUG_LOGS
    LOG_DEBUG("chosen order: ")
    for (size_t i = 0; i < mtbdd_levels_size(); i++) printf("%d ", level[i]);
    printf("\n");
#endif

    size_t cur_size = llmsset_count_marked(nodes);

    // loop over all levels
    for (unsigned int i = 0; i < mtbdd_levels_size(); i++){
        int lvl = level[i];
        if (lvl == -1) break; // done
        size_t pos = mtbdd_levels_level_to_var(lvl);

        size_t best_size = cur_size;
        size_t best_pos = pos;
        size_t old_size = cur_size;
        size_t old_pos = pos;

        LOG_DEBUG("sifting level %d at position %zu\n", lvl, pos);

        // search for the optimum variable position
        if(lvl > (long long int)(mtbdd_levels_size()/2)){
            sift_up(&pos, low, &cur_size, &best_size, &best_pos);
            sift_down(&pos, high, &cur_size, &best_size, &best_pos);
        }else {
            sift_down(&pos, high, &cur_size, &best_size, &best_pos);
            sift_up(&pos, low, &cur_size, &best_size, &best_pos);
        }

        // optimum variable position restoration
        sift_to_pos(pos, best_pos);

        configs.total_num_var++;

        if(configs.termination_cb != NULL && configs.termination_cb()) {
            LOG_INFO("sifting exit: termination_cb\n");
            break;
        }
        if(configs.total_num_swap > configs.max_swap) {
            LOG_INFO("sifting exit: reached %zu from the total_num_swap %lu\n", configs.total_num_swap, configs.max_swap);
            break;
        }
        if(configs.total_num_var > configs.max_var) {
            LOG_INFO("sifting exit: reached %zu from the total_num_var %lu\n", configs.total_num_var, configs.max_var);
            break;
        }

        double elapsed = (wctime()-configs.t_start_sifting)*1000;

        if(elapsed > (double)configs.time_limit) {
            LOG_INFO("sifting exit: reached %lums from the time_limit %.2fms\n", configs.time_limit, elapsed);
            break;
        }

        LOG_DEBUG("level %d has best position %zu with size %zu\n", lvl, best_pos, best_size);
    }

    size_t after_size = llmsset_count_marked(nodes);
    LOG_INFO("sifting finished: from %zu to %zu nodes\n", before_size, after_size);
}

/**
 * Sifting in CUDD:
 * First: obtain number of nodes per variable, then sort.
 * Then perform sifting until max var, or termination callback, or time limit
 * Only variables between "lower" and "upper" that are not "bound"
 *
 * Sifting a variable between "low" and "high:
 * go to the closest end first.
 * siftingUp/siftingDown --> siftingBackward
 *
 * Parameters
 * - siftMaxVar - maximum number of vars sifted
 *     default: 1000
 * - siftMaxSwap - maximum number of swaps (total)
 *     default: 2000000
 * - double maxGrowth - some maximum % growth (from the start of a sift of a part. variable)
 *     default: 1.2
 * - timeLimit - [[util_cpu_time]] table->timeLimit (actually turns off dyn reord)
 * if a lower size is found, the limitSize is updated...
 */
VOID_TASK_IMPL_2(sylvan_sifting, uint32_t, low, uint32_t, high)
{
    // SHOULD run first gc

    // if high == 0, then we sift all variables
    if (high == 0)  high = mtbdd_levels_size() - 1;
    LOG_INFO("sifting between %d and %d\n", low, high);

    size_t before_size = llmsset_count_marked(nodes);

    // Count all the variables and order their levels according to the
    // number of entries in each level (parallel operation)
    int level[mtbdd_levels_size()];
    get_sorted_level_counts(level, configs.level_count_threshold);

#if ENABLE_DEBUG_LOGS
    LOG_DEBUG("chosen order: ")
    for (size_t i = 0; i < mtbdd_levels_size(); i++) printf("%d ", level[i]);
    printf("\n");
#endif
    size_t curSize = llmsset_count_marked(nodes);

    for (size_t i=0; i<mtbdd_levels_size(); i++) {
        int lvl = level[i];
        if (lvl == -1) break; // done
        size_t pos = mtbdd_levels_level_to_var(lvl);

        LOG_DEBUG("now moving level %u, currently at position %zu\n", lvl, pos);

        size_t bestSize = curSize;
        size_t bestPos = pos;
        size_t oldSize = curSize;
        size_t oldPos = pos;

        for (; pos<high; pos++) {
            sylvan_varswap_res_t res = sylvan_simple_varswap(pos);
            if (res != SYLVAN_VARSWAP_SUCCESS) {
                LOG_ERROR("sift(DN): failed due to %d\n", res);
                break;
            }
            size_t after = llmsset_count_marked(nodes);
            LOG_DEBUG("sift(DN): from %zu to %zu\n", curSize, after);
            curSize = after;
            if (curSize < bestSize) {
                LOG_DEBUG("sift(DN): improved size\n");
                bestSize = curSize;
                bestPos = pos;
            }

            if ((float)curSize >= configs.max_growth * (float)bestSize) {
                pos++;
                break;
            }
        }
        for (; pos>low; pos--) {
            sylvan_varswap_res_t res = sylvan_simple_varswap(pos - 1);
            if (res != SYLVAN_VARSWAP_SUCCESS) {
                LOG_ERROR("sift(UP): failed due to %d\n", res);
                break;
            }
            size_t after = llmsset_count_marked(nodes);
            LOG_DEBUG("sift(UP): from %zu to %zu\n", curSize, after);
            curSize = after;
            if (curSize < bestSize) {
                LOG_DEBUG("sift(UP): improved size\n");
                bestSize = curSize;
                bestPos = pos;
            }
            if ((float)curSize >= configs.max_growth * (float)bestSize) {
                pos--;
                break;
            }
        }
        LOG_DEBUG("bestPos: %zu (old %zu) bestSize %zu (old %zu)\n", bestPos, oldPos, bestSize, oldSize);
        for (; pos < bestPos; pos++) {
            sylvan_varswap_res_t res = sylvan_simple_varswap(pos);
            if (res != SYLVAN_VARSWAP_SUCCESS) {
                LOG_ERROR("swap(bestPos): failed due to %d\n", res);
                break;
            }
        }
        for (; pos > bestPos; pos--) {
            sylvan_varswap_res_t res = sylvan_simple_varswap(pos - 1);
            if (res != SYLVAN_VARSWAP_SUCCESS) {
                LOG_ERROR("swap(bestPos): failed due to %d\n", res);
                break;
            }
        }
    }

    size_t after_size = llmsset_count_marked(nodes);
    LOG_INFO("Result of sifting: from %zu to %zu nodes.\n", before_size, after_size);
}



