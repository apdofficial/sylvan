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

const static size_t initial_order[4] = {0, 1, 2, 3 };

TASK_DECL_1(MTBDD, create_example_bdd, size_t);
#define create_example_bdd(is_optimal) CALL(create_example_bdd, is_optimal)

TASK_IMPL_1(MTBDD, create_example_bdd, size_t, is_optimal)
{
//    BDDs from the paper:
//    Randal E. Bryant Graph-Based Algorithms for Boolean Function Manipulation,
//    IEEE Transactions on Computers, 1986 http://www.cs.cmu.edu/~bryant/pubdir/ieeetc86.pdf
    MTBDD v0 = mtbdd_newlevel();
    MTBDD v1 = mtbdd_newlevel();
    MTBDD v2 = mtbdd_newlevel();
    MTBDD v3 = mtbdd_newlevel();
    MTBDD v4 = mtbdd_newlevel();
    MTBDD v5 = mtbdd_newlevel();

    MTBDD bdd;
    if (is_optimal) {
        // optimal order
        // ideally 8 nodes including 2 terminal nodes
        bdd = sylvan_or(sylvan_and(v0, v1), sylvan_or(sylvan_and(v2, v3), sylvan_and(v4, v5)));
    } else {
        // not optimal order
        // ideally 16 nodes including 2 terminal nodes
        bdd = sylvan_or(sylvan_and(v0, v3), sylvan_or(sylvan_and(v1, v4), sylvan_and(v2, v5)));
    }
    assert(mtbdd_levelscount() >= 6);
    return bdd;
}

TASK_0(int, test_varswap_down)
{
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    size_t var = 0;
    test_assert(sylvan_varswap_adj(var, mtbdd_nexthigh(var)) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap_adj(var, mtbdd_nexthigh(var)) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap_adj(var, mtbdd_nexthigh(var)) == SYLVAN_VARSWAP_SUCCESS);

    test_assert(mtbdd_getvar(zero) == 1);
    test_assert(mtbdd_getvar(one) == 2);
    test_assert(mtbdd_getvar(two) == 3);
    test_assert(mtbdd_getvar(three) == 0);

    size_t desired_order[4] = {1, 2, 3, 0 };
    for (unsigned int i = 0; i < sizeof(desired_order) / sizeof(desired_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == desired_order[i]);
    }
    return 0;
}

TASK_0(int, test_varswap_up)
{
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    for (unsigned int i = 0; i < sizeof(initial_order) / sizeof(initial_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == initial_order[i]);
    }

    size_t var = 3;
    test_assert(sylvan_varswap_adj(mtbdd_nextlow(var), var) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap_adj(mtbdd_nextlow(var), var) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap_adj(mtbdd_nextlow(var), var) == SYLVAN_VARSWAP_SUCCESS);

    test_assert(mtbdd_getvar(zero) == 3);
    test_assert(mtbdd_getvar(one) == 0);
    test_assert(mtbdd_getvar(two) == 1);
    test_assert(mtbdd_getvar(three) == 2);

    size_t desired_order[4] = {3, 0, 1,2 };
    for (unsigned int i = 0; i < sizeof(desired_order) / sizeof(desired_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == desired_order[i]);
    }
    return 0;
}

TASK_0(int, test_sift_down)
{
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    for (unsigned int i = 0; i < sizeof(initial_order) / sizeof(initial_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == initial_order[i]);
    }

    uint64_t cur_size = llmsset_count_marked(nodes);
    uint64_t best_size = cur_size;
    LEVEL best_lvl = 0;

    size_t var = 0;
    varswap_res_t res = sift_down_adj(var, 3, &cur_size, &best_size, &best_lvl);
    test_assert(res == SYLVAN_VARSWAP_SUCCESS);

    test_assert(mtbdd_getvar(zero) == 1);
    test_assert(mtbdd_getvar(one) == 2);
    test_assert(mtbdd_getvar(two) == 3);
    test_assert(mtbdd_getvar(three) == 0);

    size_t desired_order[4] = {1, 2, 3, 0 };
    for (unsigned int i = 0; i < sizeof(desired_order) / sizeof(desired_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == desired_order[i]);
    }
    return 0;
}

TASK_0(int, test_sift_up)
{
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    for (unsigned int i = 0; i < sizeof(initial_order) / sizeof(initial_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == initial_order[i]);
    }

    uint64_t cur_size = llmsset_count_marked(nodes);
    uint64_t best_size = cur_size;
    LEVEL best_lvl = 0;

    size_t var = 3;
    varswap_res_t res = sift_up_adj(var, 0, &cur_size, &best_size, &best_lvl);

    test_assert(mtbdd_getvar(zero) == 3);
    test_assert(mtbdd_getvar(one) == 0);
    test_assert(mtbdd_getvar(two) == 1);
    test_assert(mtbdd_getvar(three) == 2);

    test_assert(res == SYLVAN_VARSWAP_SUCCESS);

    size_t desired_order[4] = {3, 0, 1,2 };
    for (unsigned int i = 0; i < sizeof(desired_order) / sizeof(desired_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == desired_order[i]);
    }
    return 0;
}

TASK_0(int, test_sift_to_lvl)
{
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    for (unsigned int i = 0; i < sizeof(initial_order) / sizeof(initial_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == initial_order[i]);
    }

    varswap_res_t res = sift_to_lvl(3, 1);
    test_assert(res == SYLVAN_VARSWAP_SUCCESS);

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 3);
    test_assert(mtbdd_getvar(two) == 1);
    test_assert(mtbdd_getvar(three) == 2);

    size_t desired_order[4] = {0, 3, 1,2 };
    for (unsigned int i = 0; i < sizeof(desired_order) / sizeof(desired_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == desired_order[i]);
    }

    res = sift_to_lvl(3, 3);
    test_assert(res == SYLVAN_VARSWAP_SUCCESS);

    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    for (unsigned int i = 0; i < sizeof(initial_order) / sizeof(initial_order[0]); ++i){
        test_assert(mtbdd_level_to_var(i) == initial_order[i]);
    }

    return 0;
}

TASK_0(int, test_reorder)
{
    sylvan_gc();
    mtbdd_resetlevels();

    sylvan_set_reorder_threshold(1);

    size_t is_optimally_ordered = 0;
    MTBDD bdd = create_example_bdd(is_optimally_ordered);
    mtbdd_protect(&bdd);

    size_t size_before = llmsset_count_marked(nodes);
    sylvan_reorder(0, 0);
    size_t size_after = llmsset_count_marked(nodes);

    test_assert(size_after < size_before);

    mtbdd_unprotect(&bdd);
    return 0;
}

TASK_0(int, test_reorder_adj)
{
    sylvan_gc();
    mtbdd_resetlevels();

    size_t is_optimally_ordered = 0;
    MTBDD bdd = create_example_bdd(is_optimally_ordered);
    mtbdd_protect(&bdd);

    sylvan_set_reorder_threshold_adj(1);

    size_t size_before = llmsset_count_marked(nodes);
    sylvan_reorder_adj(0, 0);
    size_t size_after = llmsset_count_marked(nodes);

    test_assert(size_after < size_before);

    mtbdd_unprotect(&bdd);
    return 0;
}

TASK_1(int, runtests, size_t, ntests)
{
    printf("test_varswap_down.\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_varswap_down)) return 1;
    printf("test_varswap_up.\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_varswap_up)) return 1;
    printf("test_sift_down.\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_sift_down)) return 1;
    printf("test_sift_up.\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_sift_up)) return 1;
    printf("test_sift_to_lvl.\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_sift_to_lvl)) return 1;
    printf("test_reorder.\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_reorder)) return 1;
    printf("test_reorder_adj.\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_reorder_adj)) return 1;
    return 0;
}

int main()
{
    // Init Lace
    lace_start(4, 1000000); // 4 workers, use a 1,000,000 size task queue

    sylvan_set_sizes(1LL<<20, 1LL<<20, 1LL<<16, 1LL<<16);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();

    size_t ntests = 1;

    int res = RUN(runtests, ntests);

    sylvan_quit();
    lace_stop();

    return res;
}
