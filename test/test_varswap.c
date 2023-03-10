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
UTEST_TASK_0(test_simple_varswap, levels)
{
    uint32_t num_of_levels = COUNT_NODES_BLOCK_SIZE * 8;

    // manually trigger sylvan garbage collection
    sylvan_gc();

    mtbdd_levels_reset();
    mtbdd_levels_new(num_of_levels);

    /// start with the happy flow
    BDD n0 = mtbdd_ithlevel(0);
    BDD n1 = mtbdd_ithlevel(1);

    uint32_t v0  = mtbdd_getvar(n0);

    ASSERT_NE(n0, mtbdd_invalid);
    ASSERT_NE(n1, mtbdd_invalid);

    // assert functionality of node to level
    ASSERT_EQ(mtbdd_node_to_level(n0), 0u);
    ASSERT_EQ(mtbdd_node_to_level(n1), 1u);
    // assert correct size of levels
    ASSERT_EQ(mtbdd_levels_size(), (size_t)num_of_levels);

    // test var swap function used by sifting
    mtbdd_varswap(v0);

    // assert swapped functionality of level to node
    ASSERT_EQ(mtbdd_ithlevel(0), n1);
    ASSERT_EQ(mtbdd_ithlevel(1), n0);

    mtbdd_varswap(v0);

    // check that the variable was correctly swapped back
    // assert swapped functionality of level to node
    ASSERT_EQ(mtbdd_ithlevel(0), n0);
    ASSERT_EQ(mtbdd_ithlevel(1), n1);

    size_t level_counts[mtbdd_levels_size()];
    for (size_t i = 0; i < mtbdd_levels_size(); ++i) level_counts[i] = 0;
    mtbdd_count_levels(level_counts);

    for (size_t i = 1; i < mtbdd_levels_size(); ++i) ASSERT_EQ(level_counts[i], 1u);

    /// now test the sad flow
    ASSERT_EQ(mtbdd_ithlevel(-1), mtbdd_invalid);
    ASSERT_EQ(mtbdd_ithlevel(num_of_levels), mtbdd_invalid);
    ASSERT_EQ(mtbdd_ithlevel(num_of_levels + 1), mtbdd_invalid);
    ASSERT_EQ(mtbdd_level_to_var(mtbdd_levels_size()), mtbdd_levels_size());

    /// test exit procedure
    mtbdd_levels_reset();
    ASSERT_EQ(mtbdd_levels_size(), 0u);

    sylvan_levels_destroy();
}

/**
 * Test functionality of single variable swap(sylvan_varswap) + sylvan_levels.
 */
UTEST_TASK_0(test_simple_varswap, var_single_swap)
{
    // manually trigger sylvan garbage collection
    sylvan_gc();

    // initialize 10 levels of variables
    mtbdd_levels_reset();
    mtbdd_levels_new(5);

    BDD n3 = mtbdd_ithlevel(3);
    BDD n4 = mtbdd_ithlevel(4);

    ASSERT_EQ(mtbdd_getvar(n3), 3u);
    ASSERT_EQ(mtbdd_getvar(n4), 4u);

    ASSERT_EQ(n3, mtbdd_ithlevel(3));
    ASSERT_EQ(n4, mtbdd_ithlevel(4));

    ASSERT_EQ(n3, mtbdd_ithvar(3));
    ASSERT_EQ(n4, mtbdd_ithvar(4));

    ASSERT_EQ(sylvan_varswap(mtbdd_getvar(n3)), SYLVAN_VARSWAP_SUCCESS);

    ASSERT_EQ(mtbdd_getvar(n4), 3u);
    ASSERT_EQ(mtbdd_getvar(n3), 4u);

    ASSERT_EQ(n4, mtbdd_ithlevel(3));
    ASSERT_EQ(n3, mtbdd_ithlevel(4));

    ASSERT_EQ(n4, mtbdd_ithvar(3));
    ASSERT_EQ(n3, mtbdd_ithvar(4));

    sylvan_levels_destroy();
}

/**
 * Test functionality of multiple variable swaps(sylvan_varswap) + sylvan_levels.
 */
UTEST_TASK_0(test_simple_varswap, var_multiple_swaps)
{
    // manually trigger sylvan garbage collection
    sylvan_gc();

    // initialize 10 levels of variables
    mtbdd_levels_reset();
    mtbdd_levels_new(3);

    BDD n0 = mtbdd_ithlevel(0);
    BDD n1 = mtbdd_ithlevel(1);
    BDD n2 = mtbdd_ithlevel(2);

    ASSERT_EQ(mtbdd_getvar(n0), 0u);
    ASSERT_EQ(mtbdd_getvar(n1), 1u);
    ASSERT_EQ(mtbdd_getvar(n2), 2u);

    ASSERT_EQ(n0, mtbdd_ithlevel(0));
    ASSERT_EQ(n1, mtbdd_ithlevel(1));
    ASSERT_EQ(n2, mtbdd_ithlevel(2));

    ASSERT_EQ(n0, mtbdd_ithvar(0));
    ASSERT_EQ(n1, mtbdd_ithvar(1));
    ASSERT_EQ(n2, mtbdd_ithvar(2));

    /// swap var 0 with var 2
    // v0, v1, v2
    ASSERT_EQ(sylvan_varswap(mtbdd_getvar(n0)), SYLVAN_VARSWAP_SUCCESS);
    ASSERT_EQ(sylvan_varswap(mtbdd_getvar(n0)), SYLVAN_VARSWAP_SUCCESS);
    ASSERT_EQ(sylvan_varswap(mtbdd_getvar(n1)), SYLVAN_VARSWAP_SUCCESS);
    // v2, v1, v0

    ASSERT_EQ(mtbdd_getvar(n2), 0u);
    ASSERT_EQ(mtbdd_getvar(n1), 1u);
    ASSERT_EQ(mtbdd_getvar(n0), 2u);

    ASSERT_EQ(n2, mtbdd_ithlevel(0));
    ASSERT_EQ(n1, mtbdd_ithlevel(1));
    ASSERT_EQ(n0, mtbdd_ithlevel(2));

    ASSERT_EQ(n2, mtbdd_ithvar(0));
    ASSERT_EQ(n1, mtbdd_ithvar(1));
    ASSERT_EQ(n0, mtbdd_ithvar(2));

    sylvan_levels_destroy();
}

UTEST_TASK_0(test_simple_varswap, var_single_swap_hash)
{
    char n5_hash_before_swp[65];
    char n6_hash_before_swp[65];
    char n5_hash_after_swp[65];
    char n6_hash_after_swp[65];

    // manually trigger sylvan garbage collection
    sylvan_gc();

    // initialize 10 levels of variables
    mtbdd_levels_reset();
    mtbdd_levels_new(10);

    BDD n5 = mtbdd_ithlevel(5);
    BDD n6 = mtbdd_ithlevel(6);

    // get hash of x6 and x7 before var swap
    sylvan_getsha(n5, n5_hash_before_swp);
    sylvan_getsha(n6, n6_hash_before_swp);

    // swap var_5 with var_6
    ASSERT_EQ(sylvan_varswap(mtbdd_getvar(n5)), SYLVAN_VARSWAP_SUCCESS);

    // get hash of x6 and x7 after var swap
    sylvan_getsha(n5, n5_hash_after_swp);
    sylvan_getsha(n6, n6_hash_after_swp);

    // check if hash of x6 and x7 are swapped
    ASSERT_STREQ(n5_hash_before_swp, n6_hash_after_swp);
    ASSERT_STREQ(n6_hash_before_swp, n5_hash_after_swp);

    sylvan_levels_destroy();
}

UTEST_TASK_0(test_simple_varswap, var_swap_random)
{
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

        ASSERT_EQ(sylvan_varswap(6), SYLVAN_VARSWAP_SUCCESS);

        // get hash of rnd_bdd and rnd_bdd_cmp after var swap
        sylvan_getsha(rnd_bdd, rnd_bdd_hash_after_swp);
        sylvan_getsha(rnd_bdd_cmp, rnd_bdd_cmp_hash_after_swp);

        // check if hash of rnd_bdd and rnd_bdd_cmp are swapped
        ASSERT_STREQ(rnd_bdd_hash_before_swp, rnd_bdd_cmp_hash_after_swp);
        ASSERT_STREQ(rnd_bdd_cmp_hash_before_swp, rnd_bdd_hash_after_swp);

        /// test random, swap of level 6 with level 8
        rnd_bdd = make_random(3, 16);
        map = sylvan_map_empty();
        map = sylvan_map_add(map, 6, mtbdd_ithvar(8));
        map = sylvan_map_add(map, 8, mtbdd_ithvar(6));
        rnd_bdd_cmp = sylvan_compose(rnd_bdd, map);

        ASSERT_EQ(sylvan_compose(rnd_bdd, map), rnd_bdd_cmp);

        // get hash of rnd_bdd and rnd_bdd_cmp before var swap
        sylvan_getsha(rnd_bdd, rnd_bdd_hash_before_swp);
        sylvan_getsha(rnd_bdd_cmp, rnd_bdd_cmp_hash_before_swp);

        ASSERT_EQ(sylvan_varswap(6), SYLVAN_VARSWAP_SUCCESS);
        ASSERT_EQ(sylvan_varswap(7), SYLVAN_VARSWAP_SUCCESS);
        ASSERT_EQ(sylvan_varswap(6), SYLVAN_VARSWAP_SUCCESS);

        // get hash of rnd_bdd and rnd_bdd_cmp after var swap
        sylvan_getsha(rnd_bdd, rnd_bdd_hash_after_swp);
        sylvan_getsha(rnd_bdd_cmp, rnd_bdd_cmp_hash_after_swp);
    }

    sylvan_levels_destroy();
}

UTEST_TASK_0(test_simple_varswap, bddmap)
{
    // manually trigger sylvan garbage collection
    sylvan_gc();

    // initialize 10 levels of variables
    mtbdd_levels_reset();
    mtbdd_levels_new(10);

    /* test bddmap [6 -> 6] becomes [7 -> 7] */
    BDDMAP map = sylvan_map_add(sylvan_map_empty(), 6, mtbdd_ithvar(6));
    ASSERT_EQ(sylvan_varswap(6), SYLVAN_VARSWAP_SUCCESS);
    ASSERT_EQ(sylvan_map_key(map), 7u);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(7));

    /* test bddmap [6 -> 7] becomes [7 -> 6] */
    map = sylvan_map_add(sylvan_map_empty(), 6, mtbdd_ithvar(7));
    ASSERT_EQ(sylvan_varswap(6), SYLVAN_VARSWAP_SUCCESS);
    ASSERT_EQ(sylvan_map_key(map), 7u);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(6));

    /* test bddmap [6 -> 7, 7 -> 8] becomes [6 -> 8, 7 -> 6] */
    map = sylvan_map_add(sylvan_map_empty(), 6, mtbdd_ithvar(7));
    map = sylvan_map_add(map, 7, mtbdd_ithvar(8));
    ASSERT_EQ(sylvan_varswap(6), SYLVAN_VARSWAP_SUCCESS);
    ASSERT_EQ(sylvan_map_key(map), 6u);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(8));

    map = sylvan_map_next(map);
    ASSERT_EQ(sylvan_map_key(map), 7u);
    ASSERT_EQ(sylvan_map_value(map), mtbdd_ithvar(6));

    sylvan_levels_destroy();
}

int main(int argc, const char *const argv[])
{
    // Init Lace
    lace_start(2, 1000000); // 2 workers, use a 1,000,000 size task queue

    // Init Sylvan
    // Give 2 GB memory
    sylvan_set_limits(2LL*1LL<<30, 1, 10);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();

    return utest_lace_main(argc, argv); // we handle Sylvan and Lace terminations here.
}