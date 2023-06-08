#include <stdio.h>
#include <locale.h>
#include <sys/time.h>

#include "sylvan.h"
#include "test_assert.h"
#include "sylvan_int.h"
#include "sylvan_varswap.h"
#include "sylvan_reorder.h"
#include "sylvan_levels.h"
#include "common.h"
#include "sylvan_interact.h"

/* Obtain current wallclock time */
static double
wctime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

static double t_start;
#define INFO(s, ...) fprintf(stdout, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__)
#define Abort(s, ...) { fprintf(stderr, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); exit(-1); }

void _sylvan_start();
void _sylvan_quit();

#define create_example_bdd(is_optimal) RUN(create_example_bdd, is_optimal)
TASK_1(BDD, create_example_bdd, size_t, is_optimal)
{
//    BDD is from the paper:
//    Randal E. Bryant Graph-Based Algorithms for Boolean Function Manipulation,
//    IEEE Transactions on Computers, 1986 http://www.cs.cmu.edu/~bryant/pubdir/ieeetc86.pdf

    // the variable indexing is relative to the current level
    BDD v0 = sylvan_newlevel();
    BDD v1 = sylvan_newlevel();
    BDD v2 = sylvan_newlevel();
    BDD v3 = sylvan_newlevel();
    BDD v4 = sylvan_newlevel();
    BDD v5 = sylvan_newlevel();

    if (is_optimal) {
        // optimal order 0, 1, 2, 3, 4, 5
        // minimum 8 nodes including 2 terminal nodes
        return sylvan_or(sylvan_and(v0, v1), sylvan_or(sylvan_and(v2, v3), sylvan_and(v4, v5)));
    } else {
        // not optimal order 0, 3, 1, 4, 2, 5
        // minimum 16 nodes including 2 terminal nodes
        return sylvan_or(sylvan_and(v0, v3), sylvan_or(sylvan_and(v1, v4), sylvan_and(v2, v5)));
    }
}

#define create_example_map(is_optimal) RUN(create_example_map, is_optimal)
TASK_1(BDDMAP, create_example_map, size_t, is_optimal)
{
    BDDMAP map = sylvan_map_empty();
    BDD bdd = create_example_bdd(is_optimal);
    map = sylvan_map_add(map, 0, bdd);
    return map;
}

TASK_0(int, test_varswap)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();
    sylvan_newlevels(10);

    /* test ithvar, switch 6 and 7 */
    BDD one = sylvan_ithlevel(6);
    BDD two = sylvan_ithlevel(7);

    test_assert(sylvan_level_to_order(6) == 6);
    test_assert(sylvan_level_to_order(7) == 7);
    test_assert(sylvan_order_to_level(6) == 6);
    test_assert(sylvan_order_to_level(7) == 7);
    test_assert(one == sylvan_ithvar(6));
    test_assert(two == sylvan_ithvar(7));
    test_assert(mtbdd_getvar(one) == 6);
    test_assert(mtbdd_getvar(two) == 7);

    test_assert(CALL(sylvan_varswap, 6) == SYLVAN_REORDER_SUCCESS);

    test_assert(sylvan_level_to_order(7) == 6);
    test_assert(sylvan_level_to_order(6) == 7);
    test_assert(sylvan_order_to_level(7) == 6);
    test_assert(sylvan_order_to_level(6) == 7);
    test_assert(mtbdd_getvar(one) == 7);
    test_assert(mtbdd_getvar(two) == 6);
    test_assert(one == sylvan_ithvar(7));
    test_assert(two == sylvan_ithvar(6));

    return 0;
}

TASK_0(int, test_varswap_down)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    MTBDD zero = sylvan_newlevel();
    MTBDD one = sylvan_newlevel();
    MTBDD two = sylvan_newlevel();
    MTBDD three = sylvan_newlevel();

    /* swap down manually var 0 to level 3 */
    test_assert(sylvan_level_to_order(0) == 0);
    test_assert(sylvan_level_to_order(1) == 1);
    test_assert(sylvan_level_to_order(2) == 2);
    test_assert(sylvan_level_to_order(3) == 3);

    test_assert(sylvan_order_to_level(0) == 0);
    test_assert(sylvan_order_to_level(1) == 1);
    test_assert(sylvan_order_to_level(2) == 2);
    test_assert(sylvan_order_to_level(3) == 3);

    test_assert(zero == sylvan_ithvar(0));
    test_assert(one == sylvan_ithvar(1));
    test_assert(two == sylvan_ithvar(2));
    test_assert(three == sylvan_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    // 0, 1, 2, 3
    test_assert(CALL(sylvan_varswap, 0) == SYLVAN_REORDER_SUCCESS);
    test_assert(CALL(sylvan_varswap, 1) == SYLVAN_REORDER_SUCCESS);
    test_assert(CALL(sylvan_varswap, 2) == SYLVAN_REORDER_SUCCESS);
    // 1, 2, 3, 0

    test_assert(sylvan_level_to_order(0) == 1);
    test_assert(sylvan_level_to_order(1) == 2);
    test_assert(sylvan_level_to_order(2) == 3);
    test_assert(sylvan_level_to_order(3) == 0);

    test_assert(sylvan_order_to_level(1) == 0);
    test_assert(sylvan_order_to_level(2) == 1);
    test_assert(sylvan_order_to_level(3) == 2);
    test_assert(sylvan_order_to_level(0) == 3);

    test_assert(zero == sylvan_ithvar(3));
    test_assert(one == sylvan_ithvar(0));
    test_assert(two == sylvan_ithvar(1));
    test_assert(three == sylvan_ithvar(2));

    test_assert(mtbdd_getvar(zero) == 3);
    test_assert(mtbdd_getvar(one) == 0);
    test_assert(mtbdd_getvar(two) == 1);
    test_assert(mtbdd_getvar(three) == 2);

    return 0;
}

TASK_0(int, test_varswap_up)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    MTBDD zero = sylvan_newlevel();
    MTBDD one = sylvan_newlevel();
    MTBDD two = sylvan_newlevel();
    MTBDD three = sylvan_newlevel();

    /* swap up manually var 3 to level 0 */
    test_assert(zero == sylvan_ithvar(0));
    test_assert(one == sylvan_ithvar(1));
    test_assert(two == sylvan_ithvar(2));
    test_assert(three == sylvan_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    // 0, 1, 2, 3
    test_assert(CALL(sylvan_varswap, 2) == SYLVAN_REORDER_SUCCESS);
    test_assert(CALL(sylvan_varswap, 1) == SYLVAN_REORDER_SUCCESS);
    test_assert(CALL(sylvan_varswap, 0) == SYLVAN_REORDER_SUCCESS);
    // 3, 0, 1, 2

    test_assert(sylvan_level_to_order(0) == 3);
    test_assert(sylvan_level_to_order(1) == 0);
    test_assert(sylvan_level_to_order(2) == 1);
    test_assert(sylvan_level_to_order(3) == 2);

    test_assert(sylvan_order_to_level(3) == 0);
    test_assert(sylvan_order_to_level(0) == 1);
    test_assert(sylvan_order_to_level(1) == 2);
    test_assert(sylvan_order_to_level(2) == 3);

    test_assert(zero == sylvan_ithvar(1));
    test_assert(one == sylvan_ithvar(2));
    test_assert(two == sylvan_ithvar(3));
    test_assert(three == sylvan_ithvar(0));

    test_assert(mtbdd_getvar(zero) == 1);
    test_assert(mtbdd_getvar(one) == 2);
    test_assert(mtbdd_getvar(two) == 3);
    test_assert(mtbdd_getvar(three) == 0);

    return 0;
}

TASK_0(int, test_sift_down)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    MTBDD zero = sylvan_newlevel();
    MTBDD one = sylvan_newlevel();
    MTBDD two = sylvan_newlevel();
    MTBDD three = sylvan_newlevel();

    // we need to make relation between the variables otherwise the lower bounds will make sifting down skip the variables swaps
    MTBDD bdd = sylvan_and(sylvan_and(sylvan_and(zero, one), two), three);
    mtbdd_protect(&bdd);

    /* swap down manually var 0 to level 3 */
    test_assert(sylvan_level_to_order(0) == 0);
    test_assert(sylvan_level_to_order(1) == 1);
    test_assert(sylvan_level_to_order(2) == 2);
    test_assert(sylvan_level_to_order(3) == 3);

    test_assert(sylvan_order_to_level(0) == 0);
    test_assert(sylvan_order_to_level(1) == 1);
    test_assert(sylvan_order_to_level(2) == 2);
    test_assert(sylvan_order_to_level(3) == 3);

    test_assert(zero == sylvan_ithvar(0));
    test_assert(one == sylvan_ithvar(1));
    test_assert(two == sylvan_ithvar(2));
    test_assert(three == sylvan_ithvar(3));

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

    sylvan_pre_reorder(SYLVAN_REORDER_BOUNDED_SIFT);
    interaction_matrix_init(levels);

    // (0), 1, 2, 3
    test_assert(CALL(sylvan_siftdown, &state) == SYLVAN_REORDER_SUCCESS);
    // 1, 2, (0), 3
    // due to the lower bounds the last variable will not be sifted as no improved in size is possible

    test_assert(sylvan_level_to_order(0) == 1);
    test_assert(sylvan_level_to_order(1) == 2);
    test_assert(sylvan_level_to_order(2) == 0);
    test_assert(sylvan_level_to_order(3) == 3);

    test_assert(sylvan_order_to_level(1) == 0);
    test_assert(sylvan_order_to_level(2) == 1);
    test_assert(sylvan_order_to_level(0) == 2);
    test_assert(sylvan_order_to_level(3) == 3);

    test_assert(zero == sylvan_ithvar(2));
    test_assert(one == sylvan_ithvar(0));
    test_assert(two == sylvan_ithvar(1));
    test_assert(three == sylvan_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 2);
    test_assert(mtbdd_getvar(one) == 0);
    test_assert(mtbdd_getvar(two) == 1);
    test_assert(mtbdd_getvar(three) == 3);

    return 0;
}

TASK_0(int, test_sift_up)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    MTBDD zero = sylvan_newlevel();
    MTBDD one = sylvan_newlevel();
    MTBDD two = sylvan_newlevel();
    MTBDD three = sylvan_newlevel();

    // we need to make relation between the variables otherwise the lower bounds will make sifting skip the variables swaps
    MTBDD bdd = sylvan_and(sylvan_and(sylvan_and(zero, one), two), three);
    mtbdd_protect(&bdd);

    /* swap up manually var 3 to level 0 */
    test_assert(zero == sylvan_ithvar(0));
    test_assert(one == sylvan_ithvar(1));
    test_assert(two == sylvan_ithvar(2));
    test_assert(three == sylvan_ithvar(3));

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

    sylvan_pre_reorder(SYLVAN_REORDER_BOUNDED_SIFT);
    interaction_matrix_init(levels);

    // 0, 1, 2, (3)
    test_assert(CALL(sylvan_siftup, &state) == SYLVAN_REORDER_SUCCESS);
    // 0, (3), 1, 2
    // due to the lower bounds the last variable will not be sifted as no improved in size is possible

    test_assert(sylvan_level_to_order(0) == 0);
    test_assert(sylvan_level_to_order(1) == 3);
    test_assert(sylvan_level_to_order(2) == 1);
    test_assert(sylvan_level_to_order(3) == 2);

    test_assert(sylvan_order_to_level(0) == 0);
    test_assert(sylvan_order_to_level(3) == 1);
    test_assert(sylvan_order_to_level(1) == 2);
    test_assert(sylvan_order_to_level(2) == 3);

    test_assert(zero == sylvan_ithvar(0));
    test_assert(one == sylvan_ithvar(2));
    test_assert(two == sylvan_ithvar(3));
    test_assert(three == sylvan_ithvar(1));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 2);
    test_assert(mtbdd_getvar(two) == 3);
    test_assert(mtbdd_getvar(three) == 1);

    return 0;
}

TASK_0(int, test_sift_back)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    MTBDD zero = sylvan_newlevel();
    MTBDD one = sylvan_newlevel();
    MTBDD two = sylvan_newlevel();
    MTBDD three = sylvan_newlevel();

    /* swap up manually var 3 to level 0 */
    test_assert(zero == sylvan_ithvar(0));
    test_assert(one == sylvan_ithvar(1));
    test_assert(two == sylvan_ithvar(2));
    test_assert(three == sylvan_ithvar(3));

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

    sylvan_pre_reorder(SYLVAN_REORDER_BOUNDED_SIFT);
    interaction_matrix_init(levels);
    return 0;

    // 0, 1, 2, (3)
    test_assert(CALL(sylvan_siftback, &state) == SYLVAN_REORDER_SUCCESS);
    // (3), 0, 1, 2

    test_assert(sylvan_level_to_order(0) == 3);
    test_assert(sylvan_level_to_order(1) == 0);
    test_assert(sylvan_level_to_order(2) == 1);
    test_assert(sylvan_level_to_order(3) == 2);

    test_assert(sylvan_order_to_level(3) == 0);
    test_assert(sylvan_order_to_level(0) == 1);
    test_assert(sylvan_order_to_level(1) == 2);
    test_assert(sylvan_order_to_level(2) == 3);

    test_assert(zero == sylvan_ithvar(1));
    test_assert(one == sylvan_ithvar(2));
    test_assert(two == sylvan_ithvar(3));
    test_assert(three == sylvan_ithvar(0));

    test_assert(mtbdd_getvar(zero) == 1);
    test_assert(mtbdd_getvar(one) == 2);
    test_assert(mtbdd_getvar(two) == 3);
    test_assert(mtbdd_getvar(three) == 0);

    // (3), 0, 1, 2
    test_assert(CALL(sylvan_siftback, &state) == SYLVAN_REORDER_SUCCESS);
    // 0, 1, 2, (3)

    test_assert(zero == sylvan_ithvar(0));
    test_assert(one == sylvan_ithvar(1));
    test_assert(two == sylvan_ithvar(2));
    test_assert(three == sylvan_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    return 0;
}

TASK_0(int, test_reorder_perm)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    MTBDD zero = sylvan_newlevel();
    MTBDD one = sylvan_newlevel();
    MTBDD two = sylvan_newlevel();
    MTBDD three = sylvan_newlevel();

    /* reorder the variables according to the variable permutation*/
    test_assert(zero == sylvan_ithvar(0));
    test_assert(one == sylvan_ithvar(1));
    test_assert(two == sylvan_ithvar(2));
    test_assert(three == sylvan_ithvar(3));

    test_assert(mtbdd_getvar(zero) == 0);
    test_assert(mtbdd_getvar(one) == 1);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 3);

    uint32_t perm[4] = {3, 0, 2, 1};

    test_assert(sylvan_reorder_perm(perm) == SYLVAN_REORDER_SUCCESS);

    test_assert(sylvan_level_to_order(0) == perm[0]);
    test_assert(sylvan_level_to_order(1) == perm[1]);
    test_assert(sylvan_level_to_order(2) == perm[2]);
    test_assert(sylvan_level_to_order(3) == perm[3]);

    test_assert(sylvan_order_to_level(perm[0]) == 0);
    test_assert(sylvan_order_to_level(perm[1]) == 1);
    test_assert(sylvan_order_to_level(perm[2]) == 2);
    test_assert(sylvan_order_to_level(perm[3]) == 3);

    test_assert(zero == sylvan_ithvar(1));
    test_assert(one == sylvan_ithvar(3));
    test_assert(two == sylvan_ithvar(2));
    test_assert(three == sylvan_ithvar(0));

    test_assert(mtbdd_getvar(zero) == 1);
    test_assert(mtbdd_getvar(one) == 3);
    test_assert(mtbdd_getvar(two) == 2);
    test_assert(mtbdd_getvar(three) == 0);

    return 0;
}

TASK_0(int, test_reorder)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    BDD bdd = create_example_bdd(0);
    sylvan_protect(&bdd);

    size_t not_optimal_order_size = sylvan_nodecount(bdd);
    sylvan_reduce_heap(SYLVAN_REORDER_SIFT);
    size_t not_optimal_order_reordered_size = sylvan_nodecount(bdd);

    test_assert(not_optimal_order_reordered_size < not_optimal_order_size);

    uint32_t perm[6] = { 0, 1, 2, 3, 4, 5 };
    int identity = 1;
    // check if the new order is identity with the old order
    for (size_t i = 0; i < sylvan_levelscount(); i++) {
        if (sylvan_order_to_level(i) != perm[i]) {
            identity = 0;
            break;
        }
    }

//     if we gave it not optimal ordering then the new ordering should not be identity
    test_assert(identity == 0);

    test_assert(sylvan_reorder_perm(perm) == SYLVAN_REORDER_SUCCESS);

    size_t not_optimal_size_again = sylvan_nodecount(bdd);
    test_assert(not_optimal_order_size == not_optimal_size_again);

    for (size_t i = 0; i < sylvan_levelscount(); i++) {
        test_assert(sylvan_order_to_level(i) == perm[i]);
    }

    sylvan_unprotect(&bdd);

    return 0;
}

TASK_0(int, test_map_reorder)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    BDDMAP map = create_example_map(0);
    sylvan_protect(&map);

    size_t size_before = sylvan_nodecount(map);
    sylvan_reduce_heap(SYLVAN_REORDER_SIFT);
    size_t size_after = sylvan_nodecount(map);

    test_assert(size_after < size_before);
    sylvan_unprotect(&map);

    return 0;
}

TASK_0(int, test_interact)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    BDD bdd1 = sylvan_or(sylvan_newlevel(), sylvan_newlevel());
    sylvan_protect(&bdd1);

    MTBDD bdd2 = create_example_bdd(0);
    sylvan_protect(&bdd2);

    sylvan_pre_reorder(SYLVAN_REORDER_BOUNDED_SIFT);
    interaction_matrix_init(levels);

    interact_print_state(levels);

    assert(interact_test(levels, 0, 1));
    assert(interact_test(levels, 1, 0));

    for (size_t i = 2; i < sylvan_levelscount(); ++i) {
        for (size_t j = i + 1; j < sylvan_levelscount(); ++j) {
            // test interaction of variables belonging to bdd2
            assert(interact_test(levels, i, j));
            assert(interact_test(levels, j, i));
            // test interaction of variables not belonging to bdd2
            assert(!interact_test(levels, 0, j));
            assert(!interact_test(levels, 0, i));
            assert(!interact_test(levels, 1, j));
            assert(!interact_test(levels, 1, i));
        }
    }

    interact_free(levels);

    sylvan_unprotect(&bdd1);
    sylvan_unprotect(&bdd2);
    return 0;
}

TASK_0(int, test_var_count)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    BDD bdd1 = sylvan_or(sylvan_newlevel(), sylvan_newlevel());
    sylvan_protect(&bdd1);

    MTBDD bdd2 = create_example_bdd(0);
    sylvan_protect(&bdd2);

    sylvan_pre_reorder(SYLVAN_REORDER_BOUNDED_SIFT);
    interaction_matrix_init(levels);

    for (size_t i = 0; i < levels->count; ++i) {
        printf("var %zu has %u nodes\n", i, atomic_load(&levels->ref_count[levels->level_to_order[i]]));
    }

    interact_free(levels);

    sylvan_unprotect(&bdd1);
    sylvan_unprotect(&bdd2);

    return 0;
}

TASK_0(int, test_ref_count)
{
    // we need to delete all data so we reset sylvan
    _sylvan_quit();
    _sylvan_start();

    BDD bdd1 = sylvan_or(sylvan_newlevel(), sylvan_newlevel());
    sylvan_protect(&bdd1);

    MTBDD bdd2 = create_example_bdd(0);
    sylvan_protect(&bdd2);

    sylvan_pre_reorder(SYLVAN_REORDER_BOUNDED_SIFT);
    interaction_matrix_init(levels);

    for (size_t i = 0; i < levels->count; ++i) {
        size_t ref_count = levels_ref_count_load(levels, levels->level_to_order[i]);
        if (ref_count > 0) {
            printf("var %zu has %zu references\n", i, ref_count);
        }
    }

    interact_free(levels);

    sylvan_unprotect(&bdd1);
    sylvan_unprotect(&bdd2);
    return 0;
}

TASK_1(int, runtests, size_t, ntests)
{
    printf("test_varswap\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_varswap)) return 1;
    printf("test_varswap_down\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_varswap_down)) return 1;
    printf("test_varswap_up\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_varswap_up)) return 1;
//    printf("test_sift_down\n");
//    for (size_t j=0;j<ntests;j++) if (RUN(test_sift_down)) return 1;
//    printf("test_sift_up\n");
//    for (size_t j=0;j<ntests;j++) if (RUN(test_sift_up)) return 1;
//    printf("test_sift_back\n");
//    for (size_t j=0;j<ntests;j++) if (RUN(test_sift_back)) return 1;
    printf("test_reorder_perm\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_reorder_perm)) return 1;
    printf("test_reorder\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_reorder)) return 1;
    printf("test_map_reorder\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_map_reorder)) return 1;
    printf("test_interact\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_interact)) return 1;
    printf("test_var_count\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_var_count)) return 1;
    printf("test_ref_count\n");
    for (size_t j=0;j<ntests;j++) if (RUN(test_ref_count)) return 1;
    return 0;
}

static int terminate_reordering = 0;

VOID_TASK_0(reordering_start)
{
    sylvan_gc();
    size_t size = llmsset_count_marked(nodes);
    printf("DE: start: %zu size\n", size);
}

VOID_TASK_0(reordering_progress)
{
    size_t size = llmsset_count_marked(nodes);
    printf("DE: progress: %zu size\n", size);
}

VOID_TASK_0(reordering_end)
{
    sylvan_gc();
    size_t size = llmsset_count_marked(nodes);
    printf("DE: end: %zu size\n", size);
}

int should_reordering_terminate()
{
    return terminate_reordering;
}

void _sylvan_start(){
    sylvan_set_limits(1LL<<20, 1, 8);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();
    sylvan_gc_enable();
}

void _sylvan_quit(){
    sylvan_quit();
    sylvan_quit_reorder();
}


int main()
{
    setlocale(LC_NUMERIC, "en_US.utf-8");
    t_start = wctime();

    lace_start(1, 0);

    _sylvan_start();

    sylvan_set_reorder_nodes_threshold(2); // keep it 2, otherwise we skip levels which will fail the test expectations
    sylvan_set_reorder_maxgrowth(1.2f);
    sylvan_set_reorder_timelimit_sec(30);

    sylvan_re_hook_prere(TASK(reordering_start));
    sylvan_re_hook_postre(TASK(reordering_end));
    sylvan_re_hook_progre(TASK(reordering_progress));
    sylvan_re_hook_termre(should_reordering_terminate);

    size_t ntests = 5;

    int res = RUN(runtests, ntests);

    sylvan_stats_report(stdout);

    _sylvan_quit();
    lace_stop();

    return res;
}
