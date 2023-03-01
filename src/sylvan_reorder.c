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
#include "sylvan_varswap.h"
#include "sylvan_levels.h"

/*
 * TODO
 *
 * check if variables are "interacting" to make it a biiiit faster...
 */

// TODO: separate declaration fomr implementation and add also sift down and restore best position


/**
 * Block size tunes the granularity of the parallel distribution
 */
#define BLOCKSIZE 128

VOID_TASK_DECL_2(get_level_counts, int*, size_t);
/**
 * @brief Count all variable levels (parallel...)
 * \param level_counts - array of size mtbdd_levels_size()
 * \param threshold - only count nodes from levels threshold
 */
#define get_level_counts(level_counts, threshold) RUN(get_level_counts, level_counts, threshold)



static int mtbdd_reorder_initialized = 0;

static void reorder_quit() {
    sylvan_levels_destroy();
    mtbdd_reorder_initialized = 0;
}

void sylvan_init_reorder(){
    sylvan_init_mtbdd();

    if (mtbdd_reorder_initialized) return;
    mtbdd_reorder_initialized = 1;

    sylvan_register_quit(reorder_quit);
    mtbdd_levels_gc_add_mark_managed_refs();
}


/**
 * Count all variable levels (parallel...)
 */
VOID_TASK_IMPL_2(get_level_counts, int*, level, size_t, threshold) {
    // we want to sort it
    size_t level_counts[mtbdd_levels_size()];
    for (size_t i = 0; i < mtbdd_levels_size(); i++) level_counts[i] = 0;
    mtbdd_levels_count_nodes(level_counts);

    // we want to sort it
    for (size_t i = 0; i < mtbdd_levels_size(); i++) {
        if (level_counts[mtbdd_levels_level_to_var(i)] < threshold) level[i] = -1;
        else level[i] = i;
    }

    // just use gnome sort because meh
    unsigned int i = 1, j = 2;
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


VOID_TASK_IMPL_5(sift_up, size_t, var, size_t, target_lvl, size_t, cursize, size_t*, bestsize, size_t*, bestlvl) {
    size_t cur_lvl = mtbdd_levels_var_to_level(var);

    for (; cur_lvl < target_lvl; cur_lvl++) {
        varswap_res_t res = sylvan_simple_varswap(var);
        if (res != SYLVAN_VAR_SWAP_SUCCESS) {
            fprintf(stderr, "varswap failed due to: %d\n", res);
            exit(-1);
        }
        size_t after = llmsset_count_marked(nodes);
        cursize = after;
        if (cursize < *bestsize) {
            *bestsize = cursize;
            *bestlvl = cur_lvl;
        }
        if (cursize >= 2 * (*bestsize)) break;
    }
}

VOID_TASK_IMPL_5(sift_down, size_t, var, size_t, target_lvl, size_t, cursize, size_t*, bestsize, size_t*, bestlvl) {
    size_t cur_lvl = mtbdd_levels_var_to_level(var);

    for (; cur_lvl > target_lvl; cur_lvl--) {
        size_t prev_var = mtbdd_levels_level_to_var(cur_lvl-1);
        varswap_res_t res = sylvan_simple_varswap(prev_var);
        if (res != SYLVAN_VAR_SWAP_SUCCESS) {
            fprintf(stderr, "varswap failed due to: %d\n", res);
            exit(-1);
        }
        size_t after = llmsset_count_marked(nodes);
        cursize = after;
        if (cursize < *bestsize) {
            *bestsize = cursize;
            *bestlvl = cur_lvl;
        }
        if (cursize >= 2 * (*bestsize)) break;
    }
}

VOID_TASK_IMPL_2(sift_to_lvl, size_t, var, size_t, bestlvl) {
    size_t cur_lvl = mtbdd_levels_var_to_level(var);
    // sift up
    for (; cur_lvl < bestlvl; cur_lvl++) {
        varswap_res_t res = sylvan_simple_varswap(var);
        if (res != SYLVAN_VAR_SWAP_SUCCESS) {
            fprintf(stderr, "varswap failed due to: %d\n", res);
            exit(-1);
        }
    }
    // sift down
    for (; cur_lvl > bestlvl; cur_lvl--) {
        size_t prev_var = mtbdd_levels_level_to_var(cur_lvl-1);
        varswap_res_t res = sylvan_simple_varswap(prev_var);
        if (res != SYLVAN_VAR_SWAP_SUCCESS) {
            fprintf(stderr, "varswap failed due to: %d\n", res);
            exit(-1);
        }
    }
}

VOID_TASK_IMPL_2(sylvan_sifting_new, uint32_t, low_lvl, uint32_t, high_lvl) {
    printf("DVO: Started dynamic variable ordering...\n");

    // if high == 0, then we sift all variables
    if (high_lvl == 0)  high_lvl = mtbdd_levels_size() - 1;

    size_t before_size = llmsset_count_marked(nodes);

    int level[mtbdd_levels_size()];
    get_level_counts(level, 1);

    size_t cursize = llmsset_count_marked(nodes);

    for (unsigned int i = 0; i < mtbdd_levels_size(); i++) {
        int cur_lvl = level[i];
        if (cur_lvl == -1) break; // done
        if (cur_lvl < (int)low_lvl || cur_lvl > (int)high_lvl) continue; // skip levels that are not in range

        size_t var = mtbdd_levels_level_to_var(cur_lvl);
        size_t bestsize = cursize;
        size_t bestlvl = mtbdd_levels_var_to_level(var);

        if(cur_lvl > (int)(mtbdd_levels_size()/2)){
            // sifting up first, then down if current level
            // is un the upper half of the variable order
            sift_up(var, high_lvl, cursize, &bestsize, &bestlvl);
            sift_down(var, low_lvl, cursize, &bestsize, &bestlvl);
        }else{
            // otherwise, sifting down first, then up
            sift_down(var, low_lvl, cursize, &bestsize, &bestlvl);
            sift_up(var, high_lvl, cursize, &bestsize, &bestlvl);
        }

        sift_to_lvl(var, bestlvl);
    }

    size_t after_size = llmsset_count_marked(nodes);
    printf("Result of sifting: from %zu to %zu nodes.\n", before_size, after_size);
}

/**
 * Sifting in CUDD:
 * First: obtain number of nodes per variable, then sort.
 * Then perform sifting until max var, or termination callback, or time limit
 * Only variables between "lower" and "upper" that are not "bound"
 *
 * Sifting a variable between "low" and "high:
 * go to closest end first.
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
TASK_IMPL_2(int, sylvan_sifting, uint32_t, low, uint32_t, high) {
    // SHOULD run first gc

    // if high == 0, then we sift all variables
    if (high == 0)  high = mtbdd_levels_size() - 1;

    size_t before_size = llmsset_count_marked(nodes);

    int level[mtbdd_levels_size()];
    get_level_counts(level, 1);

//    printf("chosen order: ");
//    for (size_t i = 0; i < sylvan_levels_get_count(); i++) printf("%d ", level[i]);
//    printf("\n");

    size_t cursize = llmsset_count_marked(nodes);

    for (unsigned int i = 0; i < mtbdd_levels_size(); i++) {
        int lvl = level[i];
        if (lvl == -1) break; // done
        size_t pos = mtbdd_levels_level_to_var(lvl);

//        printf("now moving level %u, currently at position %zu\n", lvl, pos);

        size_t bestsize = cursize, bestpos = pos;
//        size_t oldsize = cursize, oldpos = pos;

        // optimum variable position search
        // sift up
        for (; pos < high; pos++) {
            varswap_res_t res = sylvan_simple_varswap(pos);
            if (res != SYLVAN_VAR_SWAP_SUCCESS) {
                fprintf(stderr, "varswap failed due to: %d\n", res);
                exit(-1);
            }
            size_t after = llmsset_count_marked(nodes);
//            printf("swap(DN): from %zu to %zu\n", cursize, after);
            cursize = after;
            if (cursize < bestsize) {
                bestsize = cursize;
                bestpos = pos;
            }
            if (cursize >= 2 * bestsize) break;
        }
        // sift down
        for (; pos > low; pos--) {
            varswap_res_t res = sylvan_simple_varswap(pos - 1);
            if (res != SYLVAN_VAR_SWAP_SUCCESS) {
                fprintf(stderr, "varswap failed due to: %d\n", res);
                exit(-1);
            }
            size_t after = llmsset_count_marked(nodes);
//            printf("swap(UP): from %zu to %zu\n", cursize, after);
            cursize = after;
            if (cursize < bestsize) {
                bestsize = cursize;
                bestpos = pos;
            }
            if (cursize >= 2 * bestsize) break;
        }

//        printf("best pos : %zu (old pos %zu) bestsize %zu (old bestsize %zu)\n", bestpos, oldpos, bestsize, oldsize);

        // optimum variable position restoration
        // sift up
        for (; pos < bestpos; pos++) {
            varswap_res_t res = sylvan_simple_varswap(pos);
            if (res != SYLVAN_VAR_SWAP_SUCCESS) {
                fprintf(stderr, "varswap failed due to: %d\n", res);
                exit(-1);
            }
        }
        // sift down
        for (; pos > bestpos; pos--) {
            varswap_res_t res = sylvan_simple_varswap(pos - 1);
            if (res != SYLVAN_VAR_SWAP_SUCCESS) {
                fprintf(stderr, "varswap failed due to: %d\n", res);
                exit(-1);
            }
        }
    }

    size_t after_size = llmsset_count_marked(nodes);
    printf("Result of sifting: from %zu to %zu nodes.\n", before_size, after_size);

    return 0;
}


