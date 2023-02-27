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

/**
 * Block size tunes the granularity of the parallel distribution
 */
#define BLOCKSIZE 128

static int mtbdd_reorder_initialized = 0;

static void
reorder_quit() {
    sylvan_levels_destroy();
    mtbdd_reorder_initialized = 0;
}

void
sylvan_init_reorder() {
    sylvan_init_mtbdd();

    if (mtbdd_reorder_initialized) return;
    mtbdd_reorder_initialized = 1;

    sylvan_register_quit(reorder_quit);
    sylvan_gc_add_mark_managed_refs();
}

void sort_levels(size_t size, const size_t level_counts[size], int level[size]) {
    for (unsigned int i = 0; i < sylvan_get_levels_count(); i++) {
        if (level_counts[sylvan_get_real_var(i)] < 128) /* threshold */ level[i] = -1;
        else level[i] = i;
    }

    // just use gnome sort because meh
    unsigned int i = 1, j = 2;
    while (i < sylvan_get_levels_count()) {
        long p = level[i - 1] == -1 ? -1 : (long) level_counts[sylvan_get_real_var(level[i - 1])];
        long q = level[i] == -1 ? -1 : (long) level_counts[sylvan_get_real_var(level[i])];
        if (p < q) {
            int t = level[i];
            level[i] = level[i - 1];
            level[i - 1] = t;
            if (--i) continue;
        }
        i = j++;
    }
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

    if (high == 0) {
        high = sylvan_get_levels_count() - 1;
    }

    size_t before_size = llmsset_count_marked(nodes);

    // now count all variable levels (parallel...)
    size_t level_counts[sylvan_get_levels_count()];
    for (size_t i = 0; i < sylvan_get_levels_count(); i++) level_counts[i] = 0;
    sylvan_count_nodes(level_counts);

    for (size_t i = 0; i < sylvan_get_levels_count(); i++) printf("Level %zu has %zu nodes\n", i, level_counts[i]);

    // we want to sort it
    int level[sylvan_get_levels_count()];
    sort_levels(sylvan_get_levels_count(), level_counts, level);


    printf("chosen order: ");
    for (size_t i = 0; i < sylvan_get_levels_count(); i++) printf("%d ", level[i]);
    printf("\n");

    // sift a thing
//    int cur_var = level_to_var[lvl];
//    int best = lvl;

    size_t cursize = llmsset_count_marked(nodes);

    for (unsigned int i = 0; i < sylvan_get_levels_count(); i++) {
        int lvl = level[i];
        if (lvl == -1) break; // done
        size_t pos = sylvan_get_real_var(lvl);

        printf("now moving level %u, currently at position %zu\n", lvl, pos);

        size_t bestsize = cursize, bestpos = pos;
        size_t oldsize = cursize, oldpos = pos;

        // TODO: if pos < low or pos > high, bye
        if (pos < low || pos > high) continue; // nvm.

        for (; pos < high; pos++) {
            if (sylvan_simple_varswap(pos) != SYLVAN_VAR_SWAP_SUCCESS) {
                // failed, table full.
                // TODO garbage collect.
                break;
            }
            size_t after = llmsset_count_marked(nodes);
            printf("swap(DN): from %zu to %zu\n", cursize, after);
            cursize = after;
            if (cursize < bestsize) {
                bestsize = cursize;
                bestpos = pos;
            }
            if (cursize >= 2 * bestsize) {
                pos++;
                break;
            }
        }
        for (; pos > low; pos--) {
            if (sylvan_simple_varswap(pos - 1) != SYLVAN_VAR_SWAP_SUCCESS) break;

            size_t after = llmsset_count_marked(nodes);
            printf("swap(UP): from %zu to %zu\n", cursize, after);
            cursize = after;
            if (cursize < bestsize) {
                bestsize = cursize;
                bestpos = pos;
            }
            if (cursize >= 2 * bestsize) {
                pos--;
                break;
            }
        }
        printf("best: %zu (old %zu) at %zu (old %zu)\n", bestpos, oldpos, bestsize, oldsize);
        for (; pos < bestpos; pos++) {
            if (sylvan_simple_varswap(pos) != SYLVAN_VAR_SWAP_SUCCESS) {
                printf("UH OH\n");
                exit(-1);
            }
        }
        for (; pos > bestpos; pos--) {
            if (sylvan_simple_varswap(pos - 1) != SYLVAN_VAR_SWAP_SUCCESS) {
                printf("UH OH\n");
                exit(-1);
            }
        }
    }

    for (size_t i = 0; i < sylvan_get_levels_count(); i++) level_counts[i] = 0;
    CALL(sylvan_count_nodes, level_counts, 0, nodes->table_size);
    for (size_t i = 0; i < sylvan_get_levels_count(); i++) printf("Level %zu has %zu nodes\n", i, level_counts[i]);

    size_t after_size = llmsset_count_marked(nodes);
    printf("Result of sifting: from %zu to %zu nodes.\n", before_size, after_size);

    return 0;
}