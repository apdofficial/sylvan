#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "sylvan_int.h"
#include "utest_lace.h"
#include "sylvan_levels.h"
#include "common.h"

UTEST_STATE();

UTEST_TASK_0(test_sifting, basic_manual_sifting) {
    uint32_t level_0 = 0;
    uint32_t level_5 = 5;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_levels_new(7);

    // suppose f = x1 & x2 | x2 & ~x3
#if 0
    // not optimal order x1 < x2 < x3 expect 6 nodes
//    printf("Using not optimal order: x0 < x1 < x2 < x3 < x4 < x5\n");
    BDD x0 = mtbdd_levels_ithlevel(0);
    BDD x1 = mtbdd_levels_ithlevel(1);
    BDD x2 = mtbdd_levels_ithlevel(2);
    BDD x3 = mtbdd_levels_ithlevel(3);
    BDD x4 = mtbdd_levels_ithlevel(4);
    BDD x5 = mtbdd_levels_ithlevel(5);
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

    // let f1 = x1 & x2 | x2 & ~x3
    BDD f1 = sylvan_or(sylvan_and(x0, x1), sylvan_and(x1, sylvan_not(x2)));
    // let f2 = x1 & x2 | x2 & ~x3
    BDD f2 = sylvan_or(sylvan_and(x3, x4), sylvan_and(x4, sylvan_not(x5)));

    BDD node_0 = mtbdd_levels_ithlevel(0);
    uint32_t var_0  = mtbdd_getvar(node_0);

    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), level_0);

    // sift var_0 to from level_0 to level_6
    for (size_t lvl = level_0; lvl < level_5; lvl++) {
        ASSERT_EQ(sylvan_simple_varswap(var_0), SYLVAN_VARSWAP_SUCCESS);
    }
    // check whether var_0 was swapped to level_6
    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), level_5);
}

UTEST_TASK_0(test_sifting, basic_sift_up) {
    uint32_t level_0 = 0;
    uint32_t level_5 = 5;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_levels_new(6);

    // suppose f = x1 & x2 | x2 & ~x3
#if 0
    // not optimal order x1 < x2 < x3 expect 6 nodes
//    printf("Using not optimal order: x0 < x1 < x2 < x3 < x4 < x5\n");
    BDD x0 = mtbdd_levels_ithlevel(0);
    BDD x1 = mtbdd_levels_ithlevel(1);
    BDD x2 = mtbdd_levels_ithlevel(2);
    BDD x3 = mtbdd_levels_ithlevel(3);
    BDD x4 = mtbdd_levels_ithlevel(4);
    BDD x5 = mtbdd_levels_ithlevel(5);
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

    // let f1 = x1 & x2 | x2 & ~x3
    BDD f1 = sylvan_or(sylvan_and(x0, x1), sylvan_and(x1, sylvan_not(x2)));
    // let f2 = x1 & x2 | x2 & ~x3
    BDD f2 = sylvan_or(sylvan_and(x3, x4), sylvan_and(x4, sylvan_not(x5)));

    BDD node_0 = mtbdd_levels_ithlevel(level_0);
    uint32_t var_0  = mtbdd_getvar(node_0);

    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), level_0);

    size_t cursize = llmsset_count_marked(nodes);
    size_t bestsize = cursize;
    size_t bestlvl = mtbdd_levels_var_to_level(var_0);
    float maxGrowth = 1.2f;

    // sift var_0 to from level_0 to level_5
    sift_up(var_0, level_5, maxGrowth, &cursize, &bestsize, &bestlvl);

    // check whether var_0 was swapped to level_5
    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), level_5);
}

UTEST_TASK_0(test_sifting, basic_sift_down){
    uint32_t level_0 = 0;
    uint32_t level_5 = 5;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_levels_new(6);

    // suppose f = x1 & x2 | x2 & ~x3
#if 0
    // not optimal order x1 < x2 < x3 expect 6 nodes
//    printf("Using not optimal order: x0 < x1 < x2 < x3 < x4 < x5\n");
    BDD x0 = mtbdd_levels_ithlevel(0);
    BDD x1 = mtbdd_levels_ithlevel(1);
    BDD x2 = mtbdd_levels_ithlevel(2);
    BDD x3 = mtbdd_levels_ithlevel(3);
    BDD x4 = mtbdd_levels_ithlevel(4);
    BDD x5 = mtbdd_levels_ithlevel(5);
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

    // let f1 = x1 & x2 | x2 & ~x3
    BDD f1 = sylvan_or(sylvan_and(x0, x1), sylvan_and(x1, sylvan_not(x2)));
    // let f2 = x1 & x2 | x2 & ~x3
    BDD f2 = sylvan_or(sylvan_and(x3, x4), sylvan_and(x4, sylvan_not(x5)));

    BDD node_0 = mtbdd_levels_ithlevel(level_5);
    uint32_t var_0  = mtbdd_getvar(node_0);

    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), level_5);

    size_t cursize = llmsset_count_marked(nodes);
    size_t bestsize = cursize;
    size_t bestlvl = mtbdd_levels_var_to_level(var_0);
    float maxGrowth = 1.2f;

    // sift var_0 to from level_0 to level_5
    sift_down(var_0, level_0, maxGrowth, &cursize, &bestsize, &bestlvl);

    // check whether var_0 was swapped to level_5
    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), level_0);

}

UTEST_TASK_0(test_sifting, basic_sift_to_best_level){
    uint32_t level_0 = 0;
    uint32_t level_5 = 5;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_levels_new(6);

    // suppose f = x1 & x2 | x2 & ~x3
#if 0
    // not optimal order x1 < x2 < x3 expect 6 nodes
//    printf("Using not optimal order: x0 < x1 < x2 < x3 < x4 < x5\n");
    BDD x0 = mtbdd_levels_ithlevel(0);
    BDD x1 = mtbdd_levels_ithlevel(1);
    BDD x2 = mtbdd_levels_ithlevel(2);
    BDD x3 = mtbdd_levels_ithlevel(3);
    BDD x4 = mtbdd_levels_ithlevel(4);
    BDD x5 = mtbdd_levels_ithlevel(5);
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

    // let f1 = x1 & x2 | x2 & ~x3
    BDD f1 = sylvan_or(sylvan_and(x0, x1), sylvan_and(x1, sylvan_not(x2)));
    // let f2 = x1 & x2 | x2 & ~x3
    BDD f2 = sylvan_or(sylvan_and(x3, x4), sylvan_and(x4, sylvan_not(x5)));

    BDD node_0 = mtbdd_levels_ithlevel(level_5);
    uint32_t var_0  = mtbdd_getvar(node_0);

    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), level_5);

    size_t cursize = llmsset_count_marked(nodes);
    size_t bestsize = cursize;
    size_t bestlvl = mtbdd_levels_var_to_level(var_0);
    float maxGrowth = 1.2f;

    // sift var_0 to from level_0 to level_5
    sift_down(var_0, level_0, maxGrowth, &cursize, &bestsize, &bestlvl);

    // check whether var_0 was swapped to level_5
    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), level_0);

    sift_to_lvl(var_0, level_5);

    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), level_5);
}

int main(int argc, const char *const argv[]) {
    // Init Lace
    lace_start(0, 1000000); // auto-detect number of workers, use a 1,000,000 size task queue

    // Init Sylvan
    // Give 2 GB memory
    sylvan_set_limits(2LL*1LL<<30, 1, 15);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();
    sylvan_gc_disable();


    return utest_lace_main(argc, argv);
}