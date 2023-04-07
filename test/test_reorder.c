#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sylvan.h"
#include "test_assert.h"
#include "sylvan_int.h"
#include "sylvan_varswap.h"
#include "sylvan_reorder.h"
#include "sylvan_levels.h"
#include "common.h"
#include "sylvan_interact.h"

TASK_DECL_1(MTBDD, create_example_bdd, size_t);
#define create_example_bdd(is_optimal) CALL(create_example_bdd, is_optimal)

TASK_IMPL_1(MTBDD, create_example_bdd, size_t, is_optimal)
{
//    BDD is from the paper:
//    Randal E. Bryant Graph-Based Algorithms for Boolean Function Manipulation,
//    IEEE Transactions on Computers, 1986 http://www.cs.cmu.edu/~bryant/pubdir/ieeetc86.pdf
    MTBDD v0 = mtbdd_newlevel();
    MTBDD v1 = mtbdd_newlevel();
    MTBDD v2 = mtbdd_newlevel();
    MTBDD v3 = mtbdd_newlevel();
    MTBDD v4 = mtbdd_newlevel();
    MTBDD v5 = mtbdd_newlevel();

    MTBDD bdd;
    if (is_optimal) {
        // optimal order 0, 1, 2, 3, 4, 5
        // minimum 8 nodes including 2 terminal nodes
        bdd = sylvan_or(sylvan_and(v0, v1), sylvan_or(sylvan_and(v2, v3), sylvan_and(v4, v5)));
    } else {
        // not optimal order 0, 3, 1, 4, 2, 5
        // minimum 16 nodes including 2 terminal nodes
        bdd = sylvan_or(sylvan_and(v0, v3), sylvan_or(sylvan_and(v1, v4), sylvan_and(v2, v5)));
    }
    return bdd;
}

TASK_0(int, test_varswap)
{
    BDD one, two;
    BDDMAP map;

    char hash1[65];
    char hash2[65];
    char hash3[65];
    char hash4[65];

    sylvan_gc();

    mtbdd_resetlevels();
    mtbdd_newlevels(10);

    /* test ithvar, switch 6 and 7 */
    one = mtbdd_ithlevel(6);
    two = mtbdd_ithlevel(7);

    test_assert(mtbdd_level_to_var(6) == 6);
    test_assert(mtbdd_level_to_var(7) == 7);
    test_assert(mtbdd_var_to_level(6) == 6);
    test_assert(mtbdd_var_to_level(7) == 7);
    test_assert(one == mtbdd_ithvar(6));
    test_assert(two == mtbdd_ithvar(7));
    test_assert(mtbdd_getvar(one) == 6);
    test_assert(mtbdd_getvar(two) == 7);

    test_assert(sylvan_varswap(6) == SYLVAN_VARSWAP_SUCCESS);

    test_assert(mtbdd_level_to_var(7) == 6);
    test_assert(mtbdd_level_to_var(6) == 7);
    test_assert(mtbdd_var_to_level(7) == 6);
    test_assert(mtbdd_var_to_level(6) == 7);
    test_assert(mtbdd_getvar(one) == 7);
    test_assert(mtbdd_getvar(two) == 6);
    test_assert(one == mtbdd_ithvar(7));
    test_assert(two == mtbdd_ithvar(6));

    /* test random, switch 6 and 7 */
    one = make_random(3, 16);
    map = sylvan_map_empty();
    map = sylvan_map_add(map, 6, mtbdd_ithvar(7));
    map = sylvan_map_add(map, 7, mtbdd_ithvar(6));
    two = sylvan_compose(one, map);

    test_assert(sylvan_compose(two, map) == one);

    sylvan_getsha(one, hash1);
    sylvan_getsha(two, hash2);

    test_assert(sylvan_varswap(6) == SYLVAN_VARSWAP_SUCCESS);

    sylvan_getsha(one, hash3);
    sylvan_getsha(two, hash4);

    test_assert(strcmp(hash1, hash4) == 0);
    test_assert(strcmp(hash2, hash3) == 0);

    return 0;
}

TASK_0(int, test_varswap_down)
{
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    /* swap down manually var 0 to level 3 */
    test_assert(mtbdd_level_to_var(0) == 0);
    test_assert(mtbdd_level_to_var(1) == 1);
    test_assert(mtbdd_level_to_var(2) == 2);
    test_assert(mtbdd_level_to_var(3) == 3);

    test_assert(mtbdd_var_to_level(0) == 0);
    test_assert(mtbdd_var_to_level(1) == 1);
    test_assert(mtbdd_var_to_level(2) == 2);
    test_assert(mtbdd_var_to_level(3) == 3);

    test_assert(zero == mtbdd_ithvar(0));
    test_assert(one == mtbdd_ithvar(1));
    test_assert(two == mtbdd_ithvar(2));
    test_assert(three == mtbdd_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    // 0, 1, 2, 3
    test_assert(sylvan_varswap(0) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap(1) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap(2) == SYLVAN_VARSWAP_SUCCESS);
    // 1, 2, 3, 0

    test_assert(mtbdd_level_to_var(0) == 1);
    test_assert(mtbdd_level_to_var(1) == 2);
    test_assert(mtbdd_level_to_var(2) == 3);
    test_assert(mtbdd_level_to_var(3) == 0);

    test_assert(mtbdd_var_to_level(1) == 0);
    test_assert(mtbdd_var_to_level(2) == 1);
    test_assert(mtbdd_var_to_level(3) == 2);
    test_assert(mtbdd_var_to_level(0) == 3);

    test_assert(zero == mtbdd_ithvar(3));
    test_assert(one == mtbdd_ithvar(0));
    test_assert(two == mtbdd_ithvar(1));
    test_assert(three == mtbdd_ithvar(2));

    test_assert(mtbdd_getvar(zero) == 3);
    test_assert(mtbdd_getvar(one) == 0);
    test_assert(mtbdd_getvar(two) == 1);
    test_assert(mtbdd_getvar(three) == 2);

    return 0;
}

TASK_0(int, test_varswap_up)
{
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    /* swap up manually var 3 to level 0 */
    test_assert(zero == mtbdd_ithvar(0));
    test_assert(one == mtbdd_ithvar(1));
    test_assert(two == mtbdd_ithvar(2));
    test_assert(three == mtbdd_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    // 0, 1, 2, 3
    test_assert(sylvan_varswap(2) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap(1) == SYLVAN_VARSWAP_SUCCESS);
    test_assert(sylvan_varswap(0) == SYLVAN_VARSWAP_SUCCESS);
    // 3, 0, 1, 2

    test_assert(mtbdd_level_to_var(0) == 3);
    test_assert(mtbdd_level_to_var(1) == 0);
    test_assert(mtbdd_level_to_var(2) == 1);
    test_assert(mtbdd_level_to_var(3) == 2);

    test_assert(mtbdd_var_to_level(3) == 0);
    test_assert(mtbdd_var_to_level(0) == 1);
    test_assert(mtbdd_var_to_level(1) == 2);
    test_assert(mtbdd_var_to_level(2) == 3);

    test_assert(zero == mtbdd_ithvar(1));
    test_assert(one == mtbdd_ithvar(2));
    test_assert(two == mtbdd_ithvar(3));
    test_assert(three == mtbdd_ithvar(0));

    test_assert(mtbdd_getvar(zero) == 1);
    test_assert(mtbdd_getvar(one) == 2);
    test_assert(mtbdd_getvar(two) == 3);
    test_assert(mtbdd_getvar(three) == 0);

    return 0;
}

TASK_0(int, test_sift_down)
{
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    /* swap down manually var 0 to level 3 */
    test_assert(mtbdd_level_to_var(0) == 0);
    test_assert(mtbdd_level_to_var(1) == 1);
    test_assert(mtbdd_level_to_var(2) == 2);
    test_assert(mtbdd_level_to_var(3) == 3);

    test_assert(mtbdd_var_to_level(0) == 0);
    test_assert(mtbdd_var_to_level(1) == 1);
    test_assert(mtbdd_var_to_level(2) == 2);
    test_assert(mtbdd_var_to_level(3) == 3);

    test_assert(zero == mtbdd_ithvar(0));
    test_assert(one == mtbdd_ithvar(1));
    test_assert(two == mtbdd_ithvar(2));
    test_assert(three == mtbdd_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    sifting_state_t state;
    state.size = llmsset_count_marked(nodes);
    state.best_size = state.size;
    state.pos = 0;
    state.best_pos = 0;
    state.low = 0;
    state.high = 3;

    // 0, 1, 2, 3
    test_assert(sylvan_siftdown(&state) == SYLVAN_VARSWAP_SUCCESS);
    // 1, 2, 3, 0

    test_assert(mtbdd_level_to_var(0) == 1);
    test_assert(mtbdd_level_to_var(1) == 2);
    test_assert(mtbdd_level_to_var(2) == 3);
    test_assert(mtbdd_level_to_var(3) == 0);

    test_assert(mtbdd_var_to_level(1) == 0);
    test_assert(mtbdd_var_to_level(2) == 1);
    test_assert(mtbdd_var_to_level(3) == 2);
    test_assert(mtbdd_var_to_level(0) == 3);

    test_assert(zero == mtbdd_ithvar(3));
    test_assert(one == mtbdd_ithvar(0));
    test_assert(two == mtbdd_ithvar(1));
    test_assert(three == mtbdd_ithvar(2));

    test_assert(mtbdd_getvar(zero) == 3);
    test_assert(mtbdd_getvar(one) == 0);
    test_assert(mtbdd_getvar(two) == 1);
    test_assert(mtbdd_getvar(three) == 2);

    return 0;
}

TASK_0(int, test_sift_up)
{
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    /* swap up manually var 3 to level 0 */
    test_assert(zero == mtbdd_ithvar(0));
    test_assert(one == mtbdd_ithvar(1));
    test_assert(two == mtbdd_ithvar(2));
    test_assert(three == mtbdd_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    sifting_state_t state;
    state.size = llmsset_count_marked(nodes);
    state.best_size = state.size;
    state.pos = 3;
    state.best_pos = 0;
    state.low = 0;
    state.high = 3;

    // 0, 1, 2, 3
    test_assert(sylvan_siftup(&state) == SYLVAN_VARSWAP_SUCCESS);
    // 3, 0, 1, 2

    test_assert(mtbdd_level_to_var(0) == 3);
    test_assert(mtbdd_level_to_var(1) == 0);
    test_assert(mtbdd_level_to_var(2) == 1);
    test_assert(mtbdd_level_to_var(3) == 2);

    test_assert(mtbdd_var_to_level(3) == 0);
    test_assert(mtbdd_var_to_level(0) == 1);
    test_assert(mtbdd_var_to_level(1) == 2);
    test_assert(mtbdd_var_to_level(2) == 3);

    test_assert(zero == mtbdd_ithvar(1));
    test_assert(one == mtbdd_ithvar(2));
    test_assert(two == mtbdd_ithvar(3));
    test_assert(three == mtbdd_ithvar(0));

    test_assert(mtbdd_getvar(zero) == 1);
    test_assert(mtbdd_getvar(one) == 2);
    test_assert(mtbdd_getvar(two) == 3);
    test_assert(mtbdd_getvar(three) == 0);
    return 0;
}

TASK_0(int, test_siftpos)
{
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    /* swap up manually var 3 to level 0 */
    test_assert(zero == mtbdd_ithvar(0));
    test_assert(one == mtbdd_ithvar(1));
    test_assert(two == mtbdd_ithvar(2));
    test_assert(three == mtbdd_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    // 0, 1, 2, 3
    test_assert(sylvan_siftpos(3, 0) == SYLVAN_VARSWAP_SUCCESS);
    // 3, 0, 1, 2

    test_assert(mtbdd_level_to_var(0) == 3);
    test_assert(mtbdd_level_to_var(1) == 0);
    test_assert(mtbdd_level_to_var(2) == 1);
    test_assert(mtbdd_level_to_var(3) == 2);

    test_assert(mtbdd_var_to_level(3) == 0);
    test_assert(mtbdd_var_to_level(0) == 1);
    test_assert(mtbdd_var_to_level(1) == 2);
    test_assert(mtbdd_var_to_level(2) == 3);

    test_assert(zero == mtbdd_ithvar(1));
    test_assert(one == mtbdd_ithvar(2));
    test_assert(two == mtbdd_ithvar(3));
    test_assert(three == mtbdd_ithvar(0));

    test_assert(mtbdd_getvar(zero) == 1);
    test_assert(mtbdd_getvar(one) == 2);
    test_assert(mtbdd_getvar(two) == 3);
    test_assert(mtbdd_getvar(three) == 0);

    // 3, 0, 1, 2
    test_assert(sylvan_siftpos(0, 3) == SYLVAN_VARSWAP_SUCCESS);
    // 0, 1, 2, 3

    test_assert(zero == mtbdd_ithvar(0));
    test_assert(one == mtbdd_ithvar(1));
    test_assert(two == mtbdd_ithvar(2));
    test_assert(three == mtbdd_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    return 0;
}

TASK_0(int, test_reorder_perm)
{
    sylvan_clear_and_mark();
    sylvan_rehash_all();
    sylvan_gc();
    mtbdd_resetlevels();
    mtbdd_newlevels(4);

    MTBDD zero = mtbdd_ithlevel(0);
    MTBDD one = mtbdd_ithlevel(1);
    MTBDD two = mtbdd_ithlevel(2);
    MTBDD three = mtbdd_ithlevel(3);

    /* reorder the variables according to the variable permutation*/
    test_assert(zero == mtbdd_ithvar(0));
    test_assert(one == mtbdd_ithvar(1));
    test_assert(two == mtbdd_ithvar(2));
    test_assert(three == mtbdd_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    uint32_t perm[4] = {3, 0, 2, 1};

    test_assert(sylvan_reorder_perm(perm) == SYLVAN_VARSWAP_SUCCESS);

    test_assert(mtbdd_level_to_var(0) == perm[0]);
    test_assert(mtbdd_level_to_var(1) == perm[1]);
    test_assert(mtbdd_level_to_var(2) == perm[2]);
    test_assert(mtbdd_level_to_var(3) == perm[3]);

    test_assert(mtbdd_var_to_level(perm[0]) == 0);
    test_assert(mtbdd_var_to_level(perm[1]) == 1);
    test_assert(mtbdd_var_to_level(perm[2]) == 2);
    test_assert(mtbdd_var_to_level(perm[3]) == 3);

    test_assert(zero == mtbdd_ithvar(1));
    test_assert(one == mtbdd_ithvar(3));
    test_assert(two == mtbdd_ithvar(2));
    test_assert(three == mtbdd_ithvar(0));

    test_assert(mtbdd_getvar(zero) == 1);
    test_assert(mtbdd_getvar(one) == 3);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 0);

    return 0;
}

TASK_0(int, test_reorder)
{
    sylvan_gc();
    mtbdd_resetlevels();

    MTBDD bdd = create_example_bdd(0);
    mtbdd_protect(&bdd);

    size_t size_before = mtbdd_nodecount(bdd);
    sylvan_reorder_all();
    size_t size_after = mtbdd_nodecount(bdd);

    test_assert(size_after < size_before);
    mtbdd_unprotect(&bdd);

    return 0;
}

TASK_0(int, test_interact)
{
    sylvan_gc();
    mtbdd_resetlevels();

    MTBDD bdd = create_example_bdd(0);
    mtbdd_protect(&bdd);

    interact_state_t state;
    interact_alloc(&state, mtbdd_levelscount());
    interact_init_par(&state);

    for (size_t col = 0; col < state.ncols; ++col){
        for (size_t row = 0; row < state.nrows; ++row){
            printf("%d ", interact_get(&state, row, col));
        }
        printf("\n");
    }

    printf("\n");

    mtbdd_unprotect(&bdd);
    interact_free(&state);

    return 0;
}

TASK_1(int, runtests, size_t, ntests)
{
//    printf("test_reorder\n"); RUN(test_reorder);
//    printf("test_varswap\n");
//    for (size_t j=0;j<ntests;j++) if (RUN(test_varswap)) return 1;
//    printf("test_varswap_down\n");
//    for (size_t j=0;j<ntests;j++) if (RUN(test_varswap_down)) return 1;
//    printf("test_varswap_up\n");
//    for (size_t j=0;j<ntests;j++) if (RUN(test_varswap_up)) return 1;
//    printf("test_sift_down.\n");
//    for (size_t j=0;j<ntests;j++) if (RUN(test_sift_down)) return 1;
//    printf("test_sift_up.\n");
//    for (size_t j=0;j<ntests;j++) if (RUN(test_sift_up)) return 1;
//    printf("test_siftpos.\n");
//    for (size_t j=0;j<ntests;j++) if (RUN(test_siftpos)) return 1;
//    printf("test_reorder_perm.\n");
//    for (size_t j=0;j<ntests;j++) if (RUN(test_reorder_perm)) return 1;
    printf("test_interact.\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_interact)) return 1;
    return 0;
}

int main()
{
    lace_start(4, 1000000); // 4 workers, use a 1,000,000 size task queue

    sylvan_set_sizes(1LL<<20, 1LL<<20, 1LL<<16, 1LL<<16);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();
    sylvan_gc_enable();

    sylvan_set_reorder_threshold(1);
    sylvan_set_reorder_maxgrowth(1.2f);

    size_t ntests = 1;

    int res = RUN(runtests, ntests);

    sylvan_quit();
    lace_stop();

    return res;
}
