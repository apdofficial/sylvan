#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "sylvan_int.h"

__thread uint64_t seed = 1;

void print_level_arrays(int iteration){
    printf("iteration=%d  \n", iteration);
    for (size_t i = 0; i < mtbdd_levels_size(); ++i)
        printf("level_to_var[%zu]=%d, ", i, mtbdd_levels_level_to_var(i));
    printf("\n");
    for (size_t i = 0; i < mtbdd_levels_size(); ++i)
        printf("var_to_level[%zu]=%d, ", i, mtbdd_levels_var_to_level(i));
    printf("\n");
}

uint64_t
xorshift_rand(void)
{
    uint64_t x = seed;
    if (seed == 0) seed = rand();
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    seed = x;
    return x * 2685821657736338717LL;
}

double
uniform_deviate(uint64_t seed)
{
    return seed * (1.0 / ((double)(UINT64_MAX) + 1.0));
}

int
rng(int low, int high)
{
    return low + uniform_deviate(xorshift_rand()) * (high-low);
}

/**
 * Make a random BDD
 */
BDD
make_random(int i, int j)
{
    if (i == j) return rng(0, 2) ? sylvan_true : sylvan_false;

    BDD yes = make_random(i+1, j);
    BDD no = make_random(i+1, j);
    BDD result = sylvan_invalid;

    switch(rng(0, 4)) {
    case 0:
        result = no;
        sylvan_deref(yes);
        break;
    case 1:
        result = yes;
        sylvan_deref(no);
        break;
    case 2:
        result = sylvan_ref(sylvan_makenode(i, yes, no));
        sylvan_deref(no);
        sylvan_deref(yes);
        break;
    case 3:
    default:
        result = sylvan_ref(sylvan_makenode(i, no, yes));
        sylvan_deref(no);
        sylvan_deref(yes);
        break;
    }

    return result;
}