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

    // suppose f = x1 & x2 | x2 & ~x3
#if 1
    // not optimal order x1 < x2 < x3 expect 6 nodes
    BDD x0 = mtbdd_ithlevel(0);
    BDD x1 = mtbdd_ithlevel(1);
    BDD x2 = mtbdd_ithlevel(2);
#else
    // optimal order x1 < x3 < x2 expect 5 nodes
    BDD x0 = mtbdd_levels_ithlevel(0);
    BDD x2 = mtbdd_levels_ithlevel(1);
    BDD x1 = mtbdd_levels_ithlevel(2);
#endif

    // let f1 = x1 & x2 | x2 & ~x3
    BDD f1 = sylvan_or(sylvan_and(x0, x1), sylvan_and(x1, sylvan_not(x2)));
    mtbdd_protect(&f1);

    ASSERT_EQ(mtbdd_levels_var_to_level(0), 0u);

    size_t cursize = llmsset_count_marked(nodes);
    size_t bestsize = cursize;
    size_t bestlvl = 0;
    float maxGrowth = 1.2f;

    // sift var_0 to from level_0 to level_3
    sift_up(0, 3, maxGrowth, &cursize, &bestsize, &bestlvl);

    // check whether var_0 was swapped to level_3
    ASSERT_EQ(mtbdd_levels_var_to_level(0), 2u);

    sylvan_levels_destroy();
}

UTEST_TASK_0(test_sifting, basic_sift_down)
{
    // manually trigger sylvan garbage collection
    sylvan_gc();

    mtbdd_levels_reset();
    mtbdd_levels_new(6);

    // suppose f = x1 & x2 | x2 & ~x3
#if 1
    // not optimal order x1 < x2 < x3 expect 6 nodes
//    printf("Using not optimal order: x0 < x1 < x2 < x3 < x4 < x5\n");
    BDD x0 = mtbdd_ithlevel(0);
    BDD x1 = mtbdd_ithlevel(1);
    BDD x2 = mtbdd_ithlevel(2);
    BDD x3 = mtbdd_ithlevel(3);
    BDD x4 = mtbdd_ithlevel(4);
    BDD x5 = mtbdd_ithlevel(5);
#else
    // optimal order x1 < x3 < x2 expect 5 nodes
//    printf("Using optimal order: x0 < x2 < x1 < x3 < x5 < x4\n");
    BDD x0 = mtbdd_levels_ithlevel(0);
    BDD x2 = mtbdd_levels_ithlevel(1);
    BDD x1 = mtbdd_levels_ithlevel(2);
    BDD x3 = mtbdd_levels_ithlevel(3);
    BDD x5 = mtbdd_levels_ithlevel(4);
    BDD x4 = mtbdd_levels_ithlevel(5);
#endif
//  let f1 = x1 & x2 | x2 & ~x3
    BDD f1 = sylvan_or(sylvan_and(x0, x1), sylvan_and(x1, sylvan_not(x2)));
    mtbdd_protect(&f1);
//  let f2 = x1 & x2 | x2 & ~x3
    BDD f2 = sylvan_or(sylvan_and(x3, x4), sylvan_and(x4, sylvan_not(x5)));
    mtbdd_protect(&f2);

    ASSERT_EQ(mtbdd_levels_var_to_level(5), 5u);

    size_t cursize = llmsset_count_marked(nodes);
    size_t bestsize = cursize;
    size_t bestlvl = 0;
    float maxGrowth = 1.2f;

    sift_down(5, 0, maxGrowth, &cursize, &bestsize, &bestlvl);

    ASSERT_EQ(mtbdd_levels_var_to_level(5), 0u);

    sylvan_levels_destroy();
}

UTEST_TASK_0(test_sifting, basic_sift_to_best_level)
{
    // manually trigger sylvan garbage collection
    sylvan_gc();

    mtbdd_levels_reset();
    mtbdd_levels_new(6);

    // suppose f = x1 & x2 | x2 & ~x3
#if 0
    // not optimal order x1 < x2 < x3 expect 6 nodes
    BDD x0 = mtbdd_levels_ithlevel(0);
    BDD x1 = mtbdd_levels_ithlevel(1);
    BDD x2 = mtbdd_levels_ithlevel(2);
    BDD x3 = mtbdd_levels_ithlevel(3);
    BDD x4 = mtbdd_levels_ithlevel(4);
    BDD x5 = mtbdd_levels_ithlevel(5);
#else
    // optimal order x1 < x3 < x2 expect 5 nodes
    BDD x0 = mtbdd_ithlevel(0);
    BDD x2 = mtbdd_ithlevel(1);
    BDD x1 = mtbdd_ithlevel(2);
    BDD x3 = mtbdd_ithlevel(3);
    BDD x5 = mtbdd_ithlevel(4);
    BDD x4 = mtbdd_ithlevel(5);
#endif
//  let f1 = x1 & x2 | x2 & ~x3
    BDD f1 = sylvan_or(sylvan_and(x0, x1), sylvan_and(x1, sylvan_not(x2)));
    mtbdd_protect(&f1);
//  let f2 = x1 & x2 | x2 & ~x3
    BDD f2 = sylvan_or(sylvan_and(x3, x4), sylvan_and(x4, sylvan_not(x5)));
    mtbdd_protect(&f2);

    ASSERT_EQ(mtbdd_levels_var_to_level(5), 5u);

    size_t cursize = llmsset_count_marked(nodes);
    size_t bestsize = cursize;
    size_t bestlvl = mtbdd_levels_var_to_level(0);
    float maxGrowth = 1.2f;

    sift_down(5, 0, maxGrowth, &cursize, &bestsize, &bestlvl);

    ASSERT_EQ(mtbdd_levels_var_to_level(5), 0u);

    print_levels_ordering();

    sift_to_pos(5, 5);

    print_levels_ordering();

    ASSERT_EQ(mtbdd_levels_var_to_level(5), 5u);
}


//UTEST_TASK_0(test_sifting, basic_sift)
//{
//    size_t level_counts[mtbdd_levels_size()];
//
//    // manually trigger sylvan garbage collection
//    sylvan_gc();
//    mtbdd_levels_reset();
//    mtbdd_levels_new(3);
//
//    // suppose f = x1 & x2 | x2 & ~x3
//#if 1
//    // not optimal order x0 < x1 < x2 expect 4 nodes + 2 terminal
//    BDD n0 = mtbdd_levels_ithlevel(0);
//    BDD n1 = mtbdd_levels_ithlevel(1);
//    BDD n2 = mtbdd_levels_ithlevel(2);
//    ASSERT_EQ(sylvan_var(n0), 0U);
//    ASSERT_EQ(sylvan_var(n1), 1U);
//    ASSERT_EQ(sylvan_var(n2), 2U);
//#else
//    // optimal order x1 < x2 < x0 expect 3 nodes + 2 terminal
//    BDD n1 = mtbdd_levels_ithlevel(0);
//    BDD n2 = mtbdd_levels_ithlevel(1);
//    BDD n0 = mtbdd_levels_ithlevel(2);
//    ASSERT_EQ(sylvan_var(n1), 0U);
//    ASSERT_EQ(sylvan_var(n2), 1U);
//    ASSERT_EQ(sylvan_var(n0), 2U);
//#endif
//
//    // let f1 = x0 & x1 | x1 & ~x2
//    MTBDD f = sylvan_or(sylvan_and(n0, n1), sylvan_and(n1, sylvan_not(n2)));
//    mtbdd_protect(&f);
//    sylvan_print(f);
//    printf("\n");
//
//    printf("before sifting: \n");
//    print_levels_ordering();
//
//    printf("\n");
//
//    sylvan_gc();
//    sylvan_sifting_new(0, 0);
//    sylvan_gc();
//
//    printf("order after sifting:\n");
//    print_levels_ordering();
//    printf("\n");
//
//
//    printf("\n");
//    sylvan_print(f);
//    printf("\n");
//
//    ASSERT_EQ(1, 1);
//}



int main(int argc, const char *const argv[])
{
    // Init Lace
    lace_start(1, 1000000); // auto-detect number of workers, use a 1,000,000 size task queue

    // Init Sylvan
    // Give 2 GB memory
    sylvan_set_limits(2LL*1LL<<30, 1, 15);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();

    return utest_lace_main(argc, argv);
}