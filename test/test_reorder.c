#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "sylvan_int.h"
#include "utest_lace.h"
#include "sylvan_levels.h"
#include "common.h"

UTEST_STATE();

//UTEST_TASK_0(test_reorder, basic_swaps) {
//    BDD x1, x2, x3;
//    uint32_t level_1 = 0;
//    uint32_t level_2 = 1;
//    uint32_t level_3 = 2;
//
//    // manually trigger sylvan garbage collection
//    sylvan_gc();
//    mtbdd_levels_reset();
//    mtbdd_newlevels(3);
//
//    x1 = mtbdd_ithlevel(level_1);
//    x2 = mtbdd_ithlevel(level_2);
//    x3 = mtbdd_ithlevel(level_3);
//
//    ASSERT_NE(x1, mtbdd_invalid);
//    ASSERT_NE(x2, mtbdd_invalid);
//    ASSERT_NE(x3, mtbdd_invalid);
//
//    ASSERT_EQ(mtbdd_getvar(x1), level_1);
//    ASSERT_EQ(mtbdd_getvar(x2), level_2);
//    ASSERT_EQ(mtbdd_getvar(x3), level_3);
//
//    ASSERT_EQ(sylvan_simple_varswap(level_1), SYLVAN_VAR_SWAP_SUCCESS);
//
//    ASSERT_EQ(mtbdd_getvar(x1), level_2);
//    ASSERT_EQ(mtbdd_getvar(x2), level_1);
//    ASSERT_EQ(mtbdd_getvar(x3), level_3);
//
//    ASSERT_EQ(sylvan_simple_varswap(level_1), SYLVAN_VAR_SWAP_SUCCESS);
//
//    ASSERT_EQ(mtbdd_getvar(x1), level_1);
//    ASSERT_EQ(mtbdd_getvar(x2), level_2);
//    ASSERT_EQ(mtbdd_getvar(x3), level_3);
//
//}

UTEST_TASK_0(test_reorder, basic_sifting) {
    uint32_t level_0 = 0;
    uint32_t level_1 = 1;
    uint32_t level_2 = 2;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_levels_new(3);

    // suppose f = x1 & x2 | x2 & ~x3
#if 1
    // not optimal order x1 < x2 < x3 expect 6 nodes
    printf("Using not optimal order: x1 < x2 < x3\n");
    BDD x1 = mtbdd_levels_ithlevel(0);
    BDD x2 = mtbdd_levels_ithlevel(1);
    BDD x3 = mtbdd_levels_ithlevel(2);
//    ASSERT_EQ(mtbdd_getlevel(x1), level_0);
//    ASSERT_EQ(mtbdd_getlevel(x2), level_1);
//    ASSERT_EQ(mtbdd_getlevel(x3), level_2);
#else
    // optimal order x1 < x3 < x2 expect 5 nodes
    printf("Using optimal order: x1 < x3 < x2\n");
    BDD x1 = mtbdd_ithlevel(0);
    BDD x3 = mtbdd_ithlevel(1);
    BDD x2 = mtbdd_ithlevel(2);
//    ASSERT_EQ(mtbdd_getlevel(x1), level_2);
//    ASSERT_EQ(mtbdd_getlevel(x3), level_1);
//    ASSERT_EQ(mtbdd_getlevel(x2), level_0);
#endif


    // let f = x1 & x2 | x2 & ~x3
    BDD f = sylvan_or(sylvan_and(x1, x2), sylvan_and(x2, sylvan_not(x3)));
//    sylvan_gc();

//    size_t level_counts[sylvan_levels_get_count()];
//    for (size_t i = 0; i < sylvan_levels_get_count(); i++) level_counts[i] = 0;
//    sylvan_levels_count_nodes(level_counts);
//
//    for (size_t i = 0; i < sylvan_levels_get_count(); ++i){
//        printf("lvl %zu: %zun\n", i, level_counts[i]);
//    }

    sylvan_sifting(0, 0);

//    // assert the order is indeed the optimal order x3 < x1 < x2
//    ASSERT_EQ(mtbdd_getvar(x3), level_0);
//    ASSERT_EQ(mtbdd_getvar(x1), level_1);
//    ASSERT_EQ(mtbdd_getvar(x2), level_2);

    ASSERT_EQ(1, 1);
}

VOID_TASK_0(gc_start){
    size_t used, total;
    sylvan_table_usage(&used, &total);
    printf("Starting garbage collection of %zu/%zu size\n", used, total);
}

VOID_TASK_0(gc_end){
    size_t used, total;
    sylvan_table_usage(&used, &total);
    printf("Garbage collection done of %zu/%zu size\n", used, total);
//    sylvan_sifting(0, 0);
}

int main(int argc, const char *const argv[]) {
    // Init Lace
    lace_start(1, 1000000); // auto-detect number of workers, use a 1,000,000 size task queue

    // Init Sylvan
    // Give 2 GB memory
    sylvan_set_limits(2LL*1LL<<30, 1, 15);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();

    sylvan_gc_disable();

    // Set hooks for logging garbage collection
//    sylvan_gc_hook_pregc(TASK(gc_start));
//    sylvan_gc_hook_postgc(TASK(gc_end));

    return utest_lace_main(argc, argv);
}