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


/*
 * TODO
 *
 * check if variables are "interacting" to make it a biiiit faster...
 */


static int mtbdd_reorder_initialized = 0;

/**
 * Block size tunes the granularity of the parallel distribution
 */
#define BLOCKSIZE 128

#define ENABLE_ERROR_LOGS   0
#define ENABLE_INFO_LOGS    1
#define ENABLE_DEBUG_LOGS   1

/* Obtain current wallclock time */
static double wctime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}
static double t_start = 0.0;

#define LOG_ERROR(s, ...)   { if (ENABLE_ERROR_LOGS) fprintf(stderr, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); }
#define LOG_DEBUG(s, ...)   { if (ENABLE_DEBUG_LOGS) fprintf(stdout, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); }
#define LOG_INFO(s, ...)    { if (ENABLE_INFO_LOGS)  fprintf(stdout, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); }

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

void print_levels_ordering(void)
{
#if ENABLE_DEBUG_LOGS
    printf("levels var: ");
    for (size_t i = 0; i < mtbdd_levels_size(); ++i){
        BDD f = mtbdd_ithlevel(i);
        printf("v%d ", mtbdd_getvar(f));
    }
    printf("\n");
    printf("real   var: ");
    for (size_t i = 0; i < mtbdd_levels_size(); ++i){
        BDD f = mtbdd_ithvar(i);
        printf("v%d ", mtbdd_getvar(f));
    }
    printf("\n\n");
#endif
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

    sylvan_register_quit(reorder_quit);
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
    mtbdd_levels_count_nodes(level_counts)

    LOG_INFO("mtbdd_levels_count_nodes()  took %f seconds\n", (((double)(clock() - t))/CLOCKS_PER_SEC));

    // set levels below the threshold to -1
    for (int i = 0; i < (int)mtbdd_levels_size(); i++) {
        if (level_counts[mtbdd_levels_level_to_var(i)] < threshold) {
            level[i] = -1;
        }
        else {
            level[i] = i;
        }
    }

    t = clock();
    gnome_sort(level, level_counts);
    LOG_INFO("gnome_sort()                took %f seconds\n", (((double)(clock() - t))/CLOCKS_PER_SEC));
}

VOID_TASK_IMPL_6(sift_up,
        size_t,  pos,
        size_t,  high,
        float,   maxGrowth,
        size_t*, curSize,
        size_t*, bestSize,
        size_t*, bestPos
){
    LOG_DEBUG("sift_up: v%zu to level %zu\n", pos, high);
    for (; pos < high; pos++){
        varswap_res_t res = sylvan_simple_varswap(pos);
        if (res != SYLVAN_VARSWAP_SUCCESS){
            sylvan_print_varswap_res("sylvan_simple_varswap failed due to: \n", res);
            exit(-1);
        }
        *curSize = llmsset_count_marked(nodes);
        if (*curSize < *bestSize){
            LOG_INFO("Improved size from %zu to %zu\n", *bestSize, *curSize);
            *bestSize = *curSize;
            *bestPos = pos;
        }
        if ((float)(*curSize) >= maxGrowth * (float)(*bestSize)) break;
    }
    //TODO: return varswap_res_t
}

VOID_TASK_IMPL_6(sift_down,
        size_t,  pos,
        size_t,  low,
        float,   maxGrowth,
        size_t*, curSize,
        size_t*, bestSize,
        size_t*, bestPos
){
    LOG_DEBUG("sift_down: v%zu to level %zu\n", pos, low);
    for (; pos > low; pos--) {
        varswap_res_t res = sylvan_simple_varswap(pos-1);
        if (res != SYLVAN_VARSWAP_SUCCESS){
            sylvan_print_varswap_res("sylvan_simple_varswap failed due to: \n", res);
            exit(-1);
        }
        *curSize = llmsset_count_marked(nodes);
        if (*curSize < *bestSize){
            *bestSize = *curSize;
            *bestPos = pos;
        }
        if ((float)(*curSize) >= maxGrowth * (float)(*bestSize)) break;
    }
    //TODO: return varswap_res_t
}

VOID_TASK_IMPL_2(sift_to_pos, size_t, pos, size_t, targetPos)
{
    for (; pos < targetPos; pos++){
        varswap_res_t res = sylvan_simple_varswap(pos);
        if (res != SYLVAN_VARSWAP_SUCCESS){
            LOG_ERROR("varswap failed due to: %d\n", res);
            exit(-1);
        }
    }
    for (; pos > targetPos; pos--){
        varswap_res_t res = sylvan_simple_varswap(pos-1);
        if (res != SYLVAN_VARSWAP_SUCCESS){
            LOG_ERROR("varswap failed due to: %d\n", res);
            exit(-1);
        }
    }
}

VOID_TASK_IMPL_2(sylvan_sifting_new, uint32_t, low, uint32_t, high)
{
    // TODO: implement maxGrowth limit tuning parameter (look at (Ebendt et al. in RT [12]) and CUDD)
    // TODO: implement maxSwap tuning parameter (look a CUDD)
    // TODO: implement timeLimit tuning parameter (look at CUDD)

    LOG_INFO("Started sifting...\n");

    size_t before_size = llmsset_count_marked(nodes);

    // if high == 0, then we sift all variables
    if (high == 0)  high = mtbdd_levels_size() - 1;

    // Count all the variables and order their levels according to the
    // number of entries in each level (parallel operation)
    int level[mtbdd_levels_size()];
    get_sorted_level_counts(level, 128);

    size_t cursize = llmsset_count_marked(nodes);

    // loop over all levels
    for (unsigned int i = 0; i < mtbdd_levels_size(); i++){
        int lvl = level[i];
        if (lvl == -1) break; // done
        size_t pos = mtbdd_levels_level_to_var(lvl);
        size_t bestsize = cursize, bestpos = pos;

        //TODO: check terminating conditions (maxSwap, timeLimit, etc.)
        //TODO: calculate dynamically lower/ upper bounds

        LOG_DEBUG("sifting variable %zu from %d to %d\n", pos, i, lvl);

        float maxGrowth = 1.2f;

        // search for the optimum variable position
        if(lvl > (int)(mtbdd_levels_size()/2)){
            sift_up(pos, high, maxGrowth, &cursize, &bestsize, &bestpos);
            sift_down(pos, low, maxGrowth, &cursize, &bestsize, &bestpos);
        }else {
            sift_down(pos, low, maxGrowth, &cursize, &bestsize, &bestpos);
            sift_up(pos, high, maxGrowth, &cursize, &bestsize, &bestpos);
        }
        // optimum variable position restoration
        sift_to_pos(pos, bestpos);
        LOG_DEBUG("bestpos: %zu  bestsize %zu\n", bestpos, bestsize);
    }

    size_t after_size = llmsset_count_marked(nodes);
    LOG_INFO("sifting finished:from %zu to %zu nodes\n", before_size, after_size);
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
TASK_IMPL_2(int, sylvan_sifting, uint32_t, low, uint32_t, high)
{
    // SHOULD run first gc

    // if high == 0, then we sift all variables
    if (high == 0)  high = mtbdd_levels_size() - 1;

    size_t before_size = llmsset_count_marked(nodes);

    int level[mtbdd_levels_size()];
    get_sorted_level_counts(level, 1);

//    printf("chosen order: ");
//    for (size_t i = 0; i < sylvan_levels_get_count(); i++) printf("%d ", level[i]);
//    printf("\n");

    size_t cursize = llmsset_count_marked(nodes);

    for (unsigned int i = 0; i < mtbdd_levels_size(); i++) {
        int lvl = level[i];
        if (lvl == -1) break; // done
        size_t pos = mtbdd_levels_level_to_var(lvl);

        printf("now moving level %u, currently at position %zu\n", lvl, pos);

        size_t bestsize = cursize, bestpos = pos;
        size_t oldsize = cursize, oldpos = pos;

        // optimum variable position search
        // sift up
        for (; pos < high; pos++){
            varswap_res_t res = sylvan_simple_varswap(pos);
            if (res != SYLVAN_VARSWAP_SUCCESS){
                fprintf(stderr, "varswap failed due to: %d\n", res);
                exit(-1);
            }
            size_t after = llmsset_count_marked(nodes);
//            printf("swap(UP): from %zu to %zu\n", cursize, after);
            cursize = after;
            if (cursize < bestsize){
                bestsize = cursize;
                bestpos = pos;
            }
            if (cursize >= 2 * bestsize) break;
        }
        // sift down
        for (; pos > low; pos--){
            varswap_res_t res = sylvan_simple_varswap(pos - 1);
            if (res != SYLVAN_VARSWAP_SUCCESS){
                fprintf(stderr, "varswap failed due to: %d\n", res);
                exit(-1);
            }
            size_t after = llmsset_count_marked(nodes);
//            printf("swap(DN): from %zu to %zu\n", cursize, after);
            cursize = after;
            if (cursize < bestsize){
                bestsize = cursize;
                bestpos = pos;
            }
            if (cursize >= 2 * bestsize) break;
        }

        printf("best pos : %zu (old pos %zu) bestsize %zu (old bestsize %zu)\n", bestpos, oldpos, bestsize, oldsize);

        // optimum variable position restoration
        // sift up
        for (; pos < bestpos; pos++){
            varswap_res_t res = sylvan_simple_varswap(pos);
            if (res != SYLVAN_VARSWAP_SUCCESS){
                fprintf(stderr, "varswap failed due to: %d\n", res);
                exit(-1);
            }
        }
        // sift down
        for (; pos > bestpos; pos--){
            varswap_res_t res = sylvan_simple_varswap(pos - 1);
            if (res != SYLVAN_VARSWAP_SUCCESS){
                fprintf(stderr, "varswap failed due to: %d\n", res);
                exit(-1);
            }
        }
    }

    size_t after_size = llmsset_count_marked(nodes);
    printf("Result of sifting: from %zu to %zu nodes.\n", before_size, after_size);

    return 0;
}



