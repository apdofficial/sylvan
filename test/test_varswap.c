#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "sylvan_int.h"
#include "utest_lace.h"
#include "sylvan_varswap.h"
#include "sylvan_levels.h"
#include "common.h"

UTEST_STATE();

/**
 * Test functionality of sylvan_levels
 */
UTEST_TASK_0(test_simple_varswap, levels)  {
//    uint32_t num_of_levels = COUNT_NODES_BLOCK_SIZE * 8;
    uint32_t num_of_levels =  8;

    // manually trigger sylvan garbage collection
    sylvan_gc();

    mtbdd_levels_reset();
    mtbdd_levels_new(num_of_levels);

    /// start with the happy flow
    BDD node_0 = mtbdd_levels_ithlevel(0);
    BDD node_1 = mtbdd_levels_ithlevel(1);

    uint32_t var_0  = mtbdd_getvar(node_0);

    ASSERT_NE(node_0, mtbdd_invalid);
    ASSERT_NE(node_1, mtbdd_invalid);

    // assert functionality of node to level
    ASSERT_EQ(mtbdd_levels_node_to_level(node_0), 0u);
    ASSERT_EQ(mtbdd_levels_node_to_level(node_1), 1u);
    // assert correct size of levels
    ASSERT_EQ(mtbdd_levels_size(), (size_t)num_of_levels);

    // test var swap function used by sifting
    mtbdd_levels_varswap(var_0);

    // assert swapped functionality of level to node
    ASSERT_EQ(mtbdd_levels_ithlevel(0), node_1);
    ASSERT_EQ(mtbdd_levels_ithlevel(1), node_0);

    mtbdd_levels_varswap(var_0);

    // check that the variable was correctly swapped back
    // assert swapped functionality of level to node
    ASSERT_EQ(mtbdd_levels_ithlevel(0), node_0);
    ASSERT_EQ(mtbdd_levels_ithlevel(1), node_1);

    size_t level_counts[mtbdd_levels_size()];
    for (size_t i = 0; i < mtbdd_levels_size(); ++i) level_counts[i] = 0;
    mtbdd_levels_count_nodes(level_counts);

    for (size_t i = 1; i < mtbdd_levels_size(); ++i) ASSERT_EQ(level_counts[i], 1u);

    /// now test the sad flow
    ASSERT_EQ(mtbdd_levels_ithlevel(-1), mtbdd_invalid);
    ASSERT_EQ(mtbdd_levels_ithlevel(num_of_levels), mtbdd_invalid);
    ASSERT_EQ(mtbdd_levels_ithlevel(num_of_levels+1), mtbdd_invalid);
    ASSERT_EQ(mtbdd_levels_level_to_var(mtbdd_levels_size()), mtbdd_levels_size());

    /// test exit procedure
    mtbdd_levels_reset();
    ASSERT_EQ(mtbdd_levels_size(), 0u);

    sylvan_levels_destroy();
}

/**
 * Test functionality of single variable swap(sylvan_varswap) + sylvan_levels.
 */
UTEST_TASK_0(test_simple_varswap, var_single_swap)  {
    // manually trigger sylvan garbage collection
    sylvan_gc();

    // initialize 10 levels of variables
    mtbdd_levels_reset();
    mtbdd_levels_new(5);

    BDD node_3 = mtbdd_levels_ithlevel(3);
    BDD node_4 = mtbdd_levels_ithlevel(4);

    uint32_t var_3  = mtbdd_getvar(node_3);
    uint32_t var_4  = mtbdd_getvar(node_4);

    ASSERT_EQ(mtbdd_levels_var_to_level(var_3), 3u);
    ASSERT_EQ(mtbdd_levels_var_to_level(var_4), 4u);

    ASSERT_EQ(sylvan_simple_varswap(var_3), SYLVAN_VARSWAP_SUCCESS);

    ASSERT_EQ(mtbdd_levels_var_to_level(var_4), 3u);
    ASSERT_EQ(mtbdd_levels_var_to_level(var_3), 4u);

    sylvan_levels_destroy();
}

/**
 * Test functionality of multiple variable swaps(sylvan_varswap) + sylvan_levels.
 */
UTEST_TASK_0(test_simple_varswap, var_multiple_swaps)  {
    // manually trigger sylvan garbage collection
    sylvan_gc();

    // initialize 10 levels of variables
    mtbdd_levels_reset();
    mtbdd_levels_new(3);

    BDD node_0 = mtbdd_levels_ithlevel(0);
    BDD node_1 = mtbdd_levels_ithlevel(1);
    BDD node_2 = mtbdd_levels_ithlevel(2);

    uint32_t var_0  = mtbdd_getvar(node_0);
    uint32_t var_1  = mtbdd_getvar(node_1);
    uint32_t var_2  = mtbdd_getvar(node_2);

    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), 0u);
    ASSERT_EQ(mtbdd_levels_var_to_level(var_1), 1u);
    ASSERT_EQ(mtbdd_levels_var_to_level(var_2), 2u);

    /// swap var_0 with var_2

    // var_0, var_1, var_2
    ASSERT_EQ(sylvan_simple_varswap(0), SYLVAN_VARSWAP_SUCCESS);
    ASSERT_EQ(sylvan_simple_varswap(1), SYLVAN_VARSWAP_SUCCESS);
    ASSERT_EQ(sylvan_simple_varswap(0), SYLVAN_VARSWAP_SUCCESS);
    // var_2, var_1, var_0

    var_2  = mtbdd_getvar(node_0);
    var_1  = mtbdd_getvar(node_1);
    var_0  = mtbdd_getvar(node_2);

    ASSERT_EQ(mtbdd_levels_var_to_level(var_2), 0u);
    ASSERT_EQ(mtbdd_levels_var_to_level(var_1), 1u);
    ASSERT_EQ(mtbdd_levels_var_to_level(var_0), 2u);

    sylvan_levels_destroy();
}

UTEST_TASK_0(test_simple_varswap, var_single_swap_hash)  {
    char node_5_hash_before_swp[65];
    char node_6_hash_before_swp[65];
    char node_5_hash_after_swp[65];
    char node_6_hash_after_swp[65];

    // manually trigger sylvan garbage collection
    sylvan_gc();

    // initialize 10 levels of variables
    mtbdd_levels_reset();
    mtbdd_levels_new(10);

    // peek nodes x6 and x7 at level 5 and 6 respectively
    BDD node_5 = mtbdd_levels_ithlevel(5);
    BDD node_6 = mtbdd_levels_ithlevel(6);

    uint32_t var_5  = mtbdd_getvar(node_5);

    // get hash of x6 and x7 before var swap
    sylvan_getsha(node_5, node_5_hash_before_swp);
    sylvan_getsha(node_6, node_6_hash_before_swp);

    // swap var_5 with var_6
    ASSERT_EQ(sylvan_simple_varswap(var_5), SYLVAN_VARSWAP_SUCCESS);

    // get hash of x6 and x7 after var swap
    sylvan_getsha(node_5, node_5_hash_after_swp);
    sylvan_getsha(node_6, node_6_hash_after_swp);

    // check if hash of x6 and x7 are swapped
    ASSERT_STREQ(node_5_hash_before_swp, node_6_hash_after_swp);
    ASSERT_STREQ(node_6_hash_before_swp, node_5_hash_after_swp);

    sylvan_levels_destroy();
}

UTEST_TASK_0(test_simple_varswap, var_swap_random)  {
    char rnd_bdd_hash_before_swp[65];
    char rnd_bdd_cmp_hash_before_swp[65];
    char rnd_bdd_hash_after_swp[65];
    char rnd_bdd_cmp_hash_after_swp[65];

    // manually trigger sylvan garbage collection
    sylvan_gc();

    // initialize 10 levels of variables
    mtbdd_levels_reset();
    mtbdd_levels_new(10);

    for (int i = 0; i < 10; ++i) {
        /// test random, swap of level 6 with level 7
        BDD rnd_bdd = make_random(3, 16);
        BDDMAP map = sylvan_map_empty();
        map = sylvan_map_add(map, 6, mtbdd_ithvar(7));
        map = sylvan_map_add(map, 7, mtbdd_ithvar(6));
        BDD rnd_bdd_cmp = sylvan_compose(rnd_bdd, map);

        ASSERT_EQ(sylvan_compose(rnd_bdd, map), rnd_bdd_cmp);

        // get hash of rnd_bdd and rnd_bdd_cmp before var swap
        sylvan_getsha(rnd_bdd, rnd_bdd_hash_before_swp);
        sylvan_getsha(rnd_bdd_cmp, rnd_bdd_cmp_hash_before_swp);

        ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VARSWAP_SUCCESS);

        // get hash of rnd_bdd and rnd_bdd_cmp after var swap
        sylvan_getsha(rnd_bdd, rnd_bdd_hash_after_swp);
        sylvan_getsha(rnd_bdd_cmp, rnd_bdd_cmp_hash_after_swp);

        // check if hash of rnd_bdd and rnd_bdd_cmp are swapped
        ASSERT_STREQ(rnd_bdd_hash_before_swp, rnd_bdd_cmp_hash_after_swp);
        ASSERT_STREQ(rnd_bdd_cmp_hash_before_swp, rnd_bdd_hash_after_swp);

        /// test random, swap of level 6 with level 8
        //TODO: investigate why this test fails
        rnd_bdd = make_random(3, 16);
        map = sylvan_map_empty();
        map = sylvan_map_add(map, 6, mtbdd_ithvar(8));
        map = sylvan_map_add(map, 8, mtbdd_ithvar(6));
        rnd_bdd_cmp = sylvan_compose(rnd_bdd, map);

        ASSERT_EQ(sylvan_compose(rnd_bdd, map), rnd_bdd_cmp);

        // get hash of rnd_bdd and rnd_bdd_cmp before var swap
        sylvan_getsha(rnd_bdd, rnd_bdd_hash_before_swp);
        sylvan_getsha(rnd_bdd_cmp, rnd_bdd_cmp_hash_before_swp);

        ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VARSWAP_SUCCESS);
        ASSERT_EQ(sylvan_simple_varswap(7), SYLVAN_VARSWAP_SUCCESS);
        ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VARSWAP_SUCCESS);

        // get hash of rnd_bdd and rnd_bdd_cmp after var swap
        sylvan_getsha(rnd_bdd, rnd_bdd_hash_after_swp);
        sylvan_getsha(rnd_bdd_cmp, rnd_bdd_cmp_hash_after_swp);

        // check if hash of rnd_bdd and rnd_bdd_cmp are swapped
//        ASSERT_STREQ(rnd_bdd_hash_before_swp, rnd_bdd_cmp_hash_after_swp);
//        ASSERT_STREQ(rnd_bdd_cmp_hash_before_swp, rnd_bdd_hash_after_swp);
    }

    sylvan_levels_destroy();
}

UTEST_TASK_0(test_simple_varswap, bddmap)  {
    // manually trigger sylvan garbage collection
    sylvan_gc();

    // initialize 10 levels of variables
    mtbdd_levels_reset();
    mtbdd_levels_new(10);

    /* test bddmap [6 -> 6] becomes [7 -> 7] */
    BDDMAP map = sylvan_map_add(sylvan_map_empty(), 6, mtbdd_ithvar(6));
    ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VARSWAP_SUCCESS);
    ASSERT_EQ(sylvan_map_key(map), (uint32_t)7);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(7));

    /* test bddmap [6 -> 7] becomes [7 -> 6] */
    map = sylvan_map_add(sylvan_map_empty(), 6, mtbdd_ithvar(7));
    ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VARSWAP_SUCCESS);
    ASSERT_EQ(sylvan_map_key(map), (uint32_t)7);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(6));

    /* test bddmap [6 -> 7, 7 -> 8] becomes [6 -> 8, 7 -> 6] */
    map = sylvan_map_add(sylvan_map_empty(), 6, mtbdd_ithvar(7));
    map = sylvan_map_add(map, 7, mtbdd_ithvar(8));
    ASSERT_EQ(sylvan_simple_varswap(6), SYLVAN_VARSWAP_SUCCESS);
    ASSERT_EQ(sylvan_map_key(map), (uint32_t)6);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(8));

    map = sylvan_map_next(map);
    ASSERT_EQ(sylvan_map_key(map), (uint32_t)7);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(6));

    sylvan_levels_destroy();
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