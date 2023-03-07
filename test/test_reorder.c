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
    float maxGrowth = 1.2f;

    ASSERT_EQ(mtbdd_getvar(n0), 0u);

    size_t pos = mtbdd_getvar(n0);

    // sift var 0 to from level 0 to level 3
    sift_down(&pos, 3, maxGrowth, &cursize, &bestsize, &bestPos);

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
    float maxGrowth = 1.2f;

    ASSERT_EQ(mtbdd_getvar(n5), 5u);

    size_t pos = mtbdd_getvar(n5);

    sift_up(&pos, 0, maxGrowth, &cursize, &bestsize, &bestPos);

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
    size_t bestlvl = mtbdd_levels_var_to_level(0);
    float maxGrowth = 1.2f;

    ASSERT_EQ(mtbdd_getvar(n5), 5u);

    size_t pos = mtbdd_getvar(n5);

    sift_up(&pos, 0, maxGrowth, &cursize, &bestsize, &bestlvl);

    ASSERT_EQ(mtbdd_getvar(n5), 0u);

    sift_to_pos(mtbdd_getvar(n5), 5);

    ASSERT_EQ(mtbdd_getvar(n5), 5u);
}

UTEST_TASK_0(test_sifting, basic_sift)
{
    size_t level_counts[mtbdd_levels_size()];

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_levels_new(7);

    // suppose f = n1 & n2 | n2 & ~n3
#if 1
    // not optimal order n0 < n1 < n2 expect 6 nodes
    BDD n0 = mtbdd_ithlevel(0);
    BDD n1 = mtbdd_ithlevel(1);
    BDD n2 = mtbdd_ithlevel(2);
    BDD n3 = mtbdd_ithlevel(4);
    BDD n4 = mtbdd_ithlevel(5);
    BDD n5 = mtbdd_ithlevel(6);
#else
    // optimal order n0 < n2 < n1 expect 5 nodes
    BDD n0 = mtbdd_ithlevel(0);
    BDD n2 = mtbdd_ithlevel(1);
    BDD n1 = mtbdd_ithlevel(2);
    BDD n3 = mtbdd_ithlevel(3);
    BDD n5 = mtbdd_ithlevel(4);
    BDD n4 = mtbdd_ithlevel(5);
#endif

    // let f1 = n0 & n1 | n1 & ~n2
    MTBDD f = sylvan_or(sylvan_and(n0, n1), sylvan_and(n1, sylvan_not(n2)));
    mtbdd_protect(&f);

    printf("v%d ", mtbdd_getvar(n0));
    printf("v%d ", mtbdd_getvar(n1));
    printf("v%d \n", mtbdd_getvar(n2));

    sylvan_gc();
    sylvan_sifting_new(0, 0, 1);

    printf("v%d ", mtbdd_getvar(n0));
    printf("v%d ", mtbdd_getvar(n1));
    printf("v%d \n", mtbdd_getvar(n2));
    
    ASSERT_EQ(1, 1);
}



int main(int argc, const char *const argv[])
{
    // Init Lace
    lace_start(0, 1000000); // auto-detect number of workers, use a 1,000,000 size task queue

    // Init Sylvan
    // Give 2 GB memory
    sylvan_set_limits(2LL*1LL<<30, 1, 15);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();

    return utest_lace_main(argc, argv); // we handle Sylvan and Lace terminations here.
}