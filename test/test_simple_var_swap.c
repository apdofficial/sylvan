#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "sylvan_int.h"
#include "utest_lace.h"
#include "sylvan_var_swap.h"
#include "sylvan_var_level.h"
#include "common.h"

UTEST_STATE();

UTEST_TASK_0(variable_swap_test, var_level)  {
    BDD var_six, var_seven;

    uint32_t level_six = 6;
    uint32_t level_seven = 7;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_newlevels(10);

    var_six = mtbdd_ithlevel(6);
    var_seven = mtbdd_ithlevel(level_seven);

    // check if the returned variables are matching the levels
    ASSERT_EQ(var_six, mtbdd_ithvar(level_six));
    ASSERT_EQ(mtbdd_getvar(var_six), level_six);
    ASSERT_EQ(var_seven, mtbdd_ithvar(level_seven));
    ASSERT_EQ(mtbdd_getvar(var_seven), level_seven);
}

UTEST_TASK_0(variable_swap_test, var_swap)  {
    BDD one, two;

    uint32_t level_six = 6;
    uint32_t level_seven = 7;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_newlevels(10);

    one = mtbdd_ithlevel(level_six);
    two = mtbdd_ithlevel(level_seven);

    // swap the variables
    ASSERT_EQ(sylvan_simple_varswap(level_six), SYLVAN_VAR_SWAP_SUCCESS);

    // check if the levels are indeed swapped
    ASSERT_EQ(mtbdd_getvar(one), level_seven);
    ASSERT_EQ(mtbdd_getvar(two), level_six);
}

UTEST_TASK_0(variable_swap_test, var_swap_hash)  {
    BDD one, two;
    BDDMAP map;

    char hash1[65];
    char hash2[65];
    char hash3[65];
    char hash4[65];

    uint32_t level_six = 6;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_newlevels(10);

    one = mtbdd_ithlevel(level_six);

    map = sylvan_map_empty();
    map = sylvan_map_add(map, 6, mtbdd_ithvar(7));
    map = sylvan_map_add(map, 7, mtbdd_ithvar(6));
    two = sylvan_compose(one, map);

    ASSERT_EQ(sylvan_compose(two, map), one);

    sylvan_getsha(one, hash1);
    sylvan_getsha(two, hash2);

    ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VAR_SWAP_SUCCESS);

    sylvan_getsha(one, hash3);
    sylvan_getsha(two, hash4);

    ASSERT_STREQ(hash1, hash4);
    ASSERT_STREQ(hash2, hash3);
}

UTEST_TASK_0(variable_swap_test, var_swap_random_swap)  {
    BDD one, two;
    BDDMAP map;

    char hash1[65];
    char hash2[65];
    char hash3[65];
    char hash4[65];

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_newlevels(10);

    for (int i = 0; i < 10; ++i) {
        /* test random, swap 6 and 7 */
        one = make_random(3, 16);
        map = sylvan_map_empty();
        map = sylvan_map_add(map, 6, mtbdd_ithvar(7));
        map = sylvan_map_add(map, 7, mtbdd_ithvar(6));
        two = sylvan_compose(one, map);

        ASSERT_EQ(sylvan_compose(two, map), one);

        sylvan_getsha(one, hash1);
        sylvan_getsha(two, hash2);

        ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VAR_SWAP_SUCCESS);

        sylvan_getsha(one, hash3);
        sylvan_getsha(two, hash4);

        ASSERT_STREQ(hash1, hash4);
        ASSERT_STREQ(hash2, hash3);

        /* test random, swap 6 and 8 */
        one = make_random(3, 16);
        map = sylvan_map_empty();
        map = sylvan_map_add(map, 6, mtbdd_ithvar(8));
        map = sylvan_map_add(map, 8, mtbdd_ithvar(6));
        two = sylvan_compose(one, map);

        ASSERT_EQ(sylvan_compose(two, map), one);

        sylvan_getsha(one, hash1);
        sylvan_getsha(two, hash2);

        ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VAR_SWAP_SUCCESS);
        ASSERT_EQ(sylvan_simple_varswap(7), SYLVAN_VAR_SWAP_SUCCESS);
        ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VAR_SWAP_SUCCESS);

        sylvan_getsha(one, hash3);
        sylvan_getsha(two, hash4);

        ASSERT_STREQ(hash1, hash4);
        ASSERT_STREQ(hash2, hash3);
    }
}

UTEST_TASK_0(variable_swap_test, bddmap)  {
    BDDMAP map;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    mtbdd_levels_reset();
    mtbdd_newlevels(10);

    /* test bddmap [6 -> 6] becomes [7 -> 7] */
    map = sylvan_map_add(sylvan_map_empty(), 6, mtbdd_ithvar(6));
    ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VAR_SWAP_SUCCESS);
    ASSERT_EQ(sylvan_map_key(map), (uint32_t)7);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(7));

    /* test bddmap [6 -> 7] becomes [7 -> 6] */
    map = sylvan_map_add(sylvan_map_empty(), 6, mtbdd_ithvar(7));
    ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VAR_SWAP_SUCCESS);
    ASSERT_EQ(sylvan_map_key(map), (uint32_t)7);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(6));

    /* test bddmap [6 -> 7, 7 -> 8] becomes [6 -> 8, 7 -> 6] */
    map = sylvan_map_add(sylvan_map_empty(), 6, mtbdd_ithvar(7));
    map = sylvan_map_add(map, 7, mtbdd_ithvar(8));
    ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VAR_SWAP_SUCCESS);
    ASSERT_EQ(sylvan_map_key(map), (uint32_t)6);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(8));

    map = sylvan_map_next(map);
    ASSERT_EQ(sylvan_map_key(map), (uint32_t)7);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(6));
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

    return utest_lace_main(argc, argv);
}