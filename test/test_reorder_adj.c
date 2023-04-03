#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "test_assert.h"
#include "sylvan_int.h"
#include "sylvan_varswap_adj.h"
#include "sylvan_reorder_adj.h"
#include "sylvan_levels.h"
#include "common.h"


TASK_0(int, test_varswap_down)
{
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_newlevels(4);

    size_t initial_order[4] = {0, 1, 2, 3 };
    for (unsigned int i = 0; i < sizeof(initial_order) / sizeof(initial_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == initial_order[i]);
    }

    size_t var = 0;
    test_assert(sylvan_varswap_adj(var, mtbdd_nexthigh(var)) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap_adj(var, mtbdd_nexthigh(var)) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap_adj(var, mtbdd_nexthigh(var)) == SYLVAN_VARSWAP_SUCCESS);

    size_t desired_order[4] = {1, 2, 3, 0 };
    for (unsigned int i = 0; i < sizeof(desired_order) / sizeof(desired_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == desired_order[i]);
    }

    sylvan_levels_destroy();
    return 0;
}

TASK_0(int, test_varswap_up)
{
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_newlevels(4);

    size_t initial_order[4] = {0, 1, 2, 3 };
    for (unsigned int i = 0; i < sizeof(initial_order) / sizeof(initial_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == initial_order[i]);
    }

    size_t var = 3;
    test_assert(sylvan_varswap_adj(var, mtbdd_nextlow(var)) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap_adj(var, mtbdd_nextlow(var)) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap_adj(var, mtbdd_nextlow(var)) == SYLVAN_VARSWAP_SUCCESS);

    size_t desired_order[4] = {3, 0, 1,2 };
    for (unsigned int i = 0; i < sizeof(desired_order) / sizeof(desired_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == desired_order[i]);
    }

    sylvan_levels_destroy();
    return 0;
}

TASK_0(int, test_sift_down)
{
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_newlevels(4);

    size_t initial_order[4] = {0, 1, 2, 3 };
    for (unsigned int i = 0; i < sizeof(initial_order) / sizeof(initial_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == initial_order[i]);
    }

    size_t cursize = llmsset_count_marked(nodes);
    size_t bestsize = cursize;
    size_t bestPos = 0;

    size_t var = 0;
    varswap_res_t res = sift_down_adj(var, 3, &cursize, &bestsize, &bestPos);
    test_assert(res == SYLVAN_VARSWAP_SUCCESS);

    size_t desired_order[4] = {1, 2, 3, 0 };
    for (unsigned int i = 0; i < sizeof(desired_order) / sizeof(desired_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == desired_order[i]);
    }

    sylvan_levels_destroy();
    return 0;
}

TASK_0(int, test_sift_up)
{
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_newlevels(4);

    size_t initial_order[4] = {0, 1, 2, 3 };
    for (unsigned int i = 0; i < sizeof(initial_order) / sizeof(initial_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == initial_order[i]);
    }

    size_t cur_size = llmsset_count_marked(nodes);
    size_t best_size = cur_size;
    size_t best_pos = 0;

    size_t var = 3;
    varswap_res_t res = sift_up_adj(var, 0, &cur_size, &best_size, &best_pos);
    test_assert(res == SYLVAN_VARSWAP_SUCCESS);

    size_t desired_order[4] = {3, 0, 1,2 };
    for (unsigned int i = 0; i < sizeof(desired_order) / sizeof(desired_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == desired_order[i]);
    }

    sylvan_levels_destroy();
    return 0;
}

TASK_0(int, test_sift_to_lvl)
{
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_newlevels(4);

    size_t initial_order[4] = {0, 1, 2, 3 };
    for (unsigned int i = 0; i < sizeof(initial_order) / sizeof(initial_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == initial_order[i]);
    }

    varswap_res_t res = sift_to_lvl(3, 1);
    test_assert(res == SYLVAN_VARSWAP_SUCCESS);

    size_t desired_order[4] = {0, 3, 1,2 };
    for (unsigned int i = 0; i < sizeof(desired_order) / sizeof(desired_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == desired_order[i]);
    }

    sylvan_levels_destroy();
    return 0;
}

TASK_0(int, test_reorder)
{
    sylvan_gc();

    mtbdd_levels_reset();
    mtbdd_newlevels(6);

    BDD n0 = mtbdd_ithlevel(0);
    BDD n1 = mtbdd_ithlevel(1);
    BDD n2 = mtbdd_ithlevel(2);
    BDD n3 = mtbdd_ithlevel(3);
    BDD n4 = mtbdd_ithlevel(4);
    BDD n5 = mtbdd_ithlevel(5);

#if 1
    // not optimal order
    MTBDD f = sylvan_or(sylvan_and(n0, n3), sylvan_or(sylvan_and(n1, n4), sylvan_and(n2, n5)));
    mtbdd_protect(&f);
#else
    // optimal order
    MTBDD f = sylvan_or(sylvan_and(n0, n1), sylvan_or(sylvan_and(n2, n3), sylvan_and(n4, n5)));
    mtbdd_protect(&f);
#endif

    size_t size_before = llmsset_count_marked(nodes);
    sylvan_reorder_adj(0, 0);
    size_t size_after = llmsset_count_marked(nodes);

    test_assert(size_after < size_before);

    return 0;
}

TASK_0(int, runtests)
{
    int num_test_runs = 1;
    printf("Testing test_varswap_down.\n");
    for (int j=0;j<num_test_runs;j++) if (RUN(test_varswap_down)) return 1;
    printf("Testing test_varswap_up.\n");
    for (int j=0;j<num_test_runs;j++) if (RUN(test_varswap_up)) return 1;
    printf("Testing test_sift_down.\n");
    for (int j=0;j<num_test_runs;j++) if (RUN(test_sift_down)) return 1;
    printf("Testing test_sift_up.\n");
    for (int j=0;j<num_test_runs;j++) if (RUN(test_sift_up)) return 1;
    printf("Testing test_sift_to_lvl.\n");
    for (int j=0;j<num_test_runs;j++) if (RUN(test_sift_to_lvl)) return 1;
    printf("Testing test_reorder.\n");
    for (int j=0;j<num_test_runs;j++) if (RUN(test_reorder)) return 1;
    return 0;
}

int main()
{
    // Init Lace
    lace_start(2, 1000000); // 2 workers, use a 1,000,000 size task queue

    // Simple Sylvan initialization, also initialize BDD, MTBDD and LDD support
    sylvan_set_sizes(1LL<<20, 1LL<<20, 1LL<<16, 1LL<<16);
    sylvan_init_package();
    sylvan_init_bdd();
    sylvan_init_mtbdd();
    sylvan_init_ldd();
    sylvan_init_reorder();

    int res = RUN(runtests);

    sylvan_quit();
    lace_stop();

    return res;
}
