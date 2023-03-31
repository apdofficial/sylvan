#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "sylvan_int.h"
#include "utest_lace.h"
#include "sylvan_levels.h"
#include "common.h"

UTEST_STATE();

UTEST_TASK_0(test_sifting, basic_sift_up)
{
    // manually trigger sylvan garbage collection
    sylvan_gc();

    mtbdd_levels_reset();
    mtbdd_levels_new(3);

    BDD n0 = mtbdd_ithlevel(0);

    size_t cursize = llmsset_count_marked(nodes);
    size_t bestsize = cursize;
    size_t bestPos = 0;

    ASSERT_EQ(mtbdd_getvar(n0), 0u);

    size_t pos = mtbdd_getvar(n0);

    varswap_res_t res = sift_down(&pos, 3, &cursize, &bestsize, &bestPos);
    ASSERT_EQ(res, SYLVAN_VARSWAP_SUCCESS);

    ASSERT_EQ(mtbdd_getvar(n0), 3u);

    sylvan_levels_destroy();
}

UTEST_TASK_0(test_sifting, basic_sift_down)
{
    // manually trigger sylvan garbage collection
    sylvan_gc();

    mtbdd_levels_reset();
    mtbdd_levels_new(6);

    BDD n5 = mtbdd_ithlevel(5);

    size_t cursize = llmsset_count_marked(nodes);
    size_t bestsize = cursize;
    size_t bestPos = 0;

    ASSERT_EQ(mtbdd_getvar(n5), 5u);

    size_t pos = mtbdd_getvar(n5);

    varswap_res_t res = sift_up(&pos, 0, &cursize, &bestsize, &bestPos);
    ASSERT_EQ(res, SYLVAN_VARSWAP_SUCCESS);

    ASSERT_EQ(mtbdd_getvar(n5), 0u);

    sylvan_levels_destroy();
}

UTEST_TASK_0(test_sifting, basic_sift_to_best_level)
{
    // manually trigger sylvan garbage collection
    sylvan_gc();

    mtbdd_levels_reset();
    mtbdd_levels_new(6);

    BDD n5 = mtbdd_ithlevel(5);

    size_t cursize = llmsset_count_marked(nodes);
    size_t bestsize = cursize;
    size_t bestlvl = mtbdd_var_to_level(0);

    ASSERT_EQ(mtbdd_getvar(n5), 5u);

    size_t pos = mtbdd_getvar(n5);

    varswap_res_t res = sift_up(&pos, 0, &cursize, &bestsize, &bestlvl);
    ASSERT_EQ(res, SYLVAN_VARSWAP_SUCCESS);

    ASSERT_EQ(mtbdd_getvar(n5), 0u);

    pos = mtbdd_getvar(n5);
    sift_to_pos(pos, 5u);

    ASSERT_EQ(mtbdd_getvar(n5), 5u);
}

UTEST_TASK_0(test_sifting, sifting) {
    // manually trigger sylvan garbage collection
    sylvan_gc();

    mtbdd_levels_reset();
    mtbdd_levels_new(6);

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
    sylvan_set_reorder_threshold(0);

    size_t size_before = llmsset_count_marked(nodes);
    sylvan_reorder(0, 0);
    size_t size_after = llmsset_count_marked(nodes);
    ASSERT_LT(size_after, size_before);

    // restore the order
    sift_to_pos(0, mtbdd_level_to_var(0));
    sift_to_pos(1, mtbdd_level_to_var(1));
    sift_to_pos(2, mtbdd_level_to_var(2));
    sift_to_pos(3, mtbdd_level_to_var(3));
    sift_to_pos(4, mtbdd_level_to_var(4));
    sift_to_pos(5, mtbdd_level_to_var(5));

    ASSERT_EQ(mtbdd_getvar(n0), 0U);
    ASSERT_EQ(mtbdd_getvar(n1), 1U);
    ASSERT_EQ(mtbdd_getvar(n2), 2U);
    ASSERT_EQ(mtbdd_getvar(n3), 3U);
    ASSERT_EQ(mtbdd_getvar(n4), 4U);
    ASSERT_EQ(mtbdd_getvar(n5), 5U);

    size_t size_restored = llmsset_count_marked(nodes);
    ASSERT_NE(size_after, size_restored);

    mtbdd_setorderlock(2, 1);
    mtbdd_setorderlock(3, 1);

    size_before = llmsset_count_marked(nodes);
    sylvan_reorder(0, 0);
    size_after = llmsset_count_marked(nodes);

    ASSERT_EQ(size_after, size_before);
}

int main(int argc, const char *const argv[])
{
    // Init Lace
    lace_start(4, 1000000); // 2 workers, use a 1,000,000 size task queue

    // Init Sylvan
    // Give 2 GB memory
    sylvan_set_limits(2LL*1LL<<30, 1, 10);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();

    return utest_lace_main(argc, argv); // we handle Sylvan and Lace terminations here.
}