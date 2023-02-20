#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "sylvan_int.h"
#include "utest.h"

/**
 * Test variable swap
 * This test creates two variables, swaps them, and checks if the result is correct.
 * @return 0 if successful, 1 otherwise
 */
int
test_basic_variable_swap()
{
    LACE_VARS;
    BDD var_six, var_seven;
    BDDMAP map;

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

    return 0;
}

TASK_0(int, runtests){
    // we are not testing garbage collection
    sylvan_gc_disable();

    printf("test_variable_swap.\n");
    for (int j=0;j<10;j++) if (test_basic_variable_swap()) return 1;

    return EXIT_SUCCESS;
}

int main(){
    // Standard Lace initialization with 1 worker
    lace_start(1, 0);

    // Simple Sylvan initialization, also initialize BDD, MTBDD and LDD support
    sylvan_set_sizes(1LL<<20, 1LL<<20, 1LL<<16, 1LL<<16);
    sylvan_init_package();
    sylvan_init_bdd();
    sylvan_init_mtbdd();
    sylvan_init_ldd();
    sylvan_init_reorder();

    printf("Sylvan initialization complete.\n");

    int res = RUN(runtests);

    sylvan_quit();
    lace_stop();

    return res;
}