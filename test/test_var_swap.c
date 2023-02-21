#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "sylvan_int.h"
#include "utest_lace.h"
#include "sylvan_var_swap.h"
#include "sylvan_var_level.h"

UTEST_STATE();

UTEST_TASK_0(variable_swap_test, var_level)  {
    BDD var_six, var_seven;

    uint32_t level_six = 6;
    uint32_t level_seven = 7;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    // reset all levels
    mtbdd_levels_reset();
    // initialize 5 levels
    mtbdd_newlevels(10);

    var_six = mtbdd_ithlevel(level_six);
    var_seven = mtbdd_ithlevel(level_seven);

    // check if the returned variables are matching the levels
    ASSERT_EQ(var_six, mtbdd_ithvar(level_six));
    ASSERT_EQ(mtbdd_getvar(var_six), level_six);
    ASSERT_EQ(var_seven, mtbdd_ithvar(level_seven));
    ASSERT_EQ(mtbdd_getvar(var_seven), level_seven);
}

UTEST_TASK_0(variable_swap_test, var_swap)  {
    BDD var_six, var_seven;

    uint32_t level_six = 6;
    uint32_t level_seven = 7;

    // manually trigger sylvan garbage collection
    sylvan_gc();
    // reset all levels
    mtbdd_levels_reset();
    // initialize 5 levels
    mtbdd_newlevels(10);

    var_six = mtbdd_ithlevel(level_six);
    var_seven = mtbdd_ithlevel(level_seven);

    // check if the returned variables are matching the levels
    ASSERT_EQ(var_six, mtbdd_ithvar(level_six));
    ASSERT_EQ(mtbdd_getvar(var_six), level_six);
    ASSERT_EQ(var_seven, mtbdd_ithvar(level_seven));
    ASSERT_EQ(mtbdd_getvar(var_seven), level_seven);

    // swap the variables
    ASSERT_EQ(sylvan_varswap(level_six, level_seven), SYLVAN_VAR_SWAP_SUCCESS);

    // check if the levels are indeed swapped
    ASSERT_EQ(mtbdd_getvar(var_six), level_seven);
    ASSERT_EQ(mtbdd_getvar(var_seven), level_six);
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