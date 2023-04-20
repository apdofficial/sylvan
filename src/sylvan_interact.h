//
// Created by Andrej Pistek on 07/04/2023.
//

#ifndef SYLVAN_SYLVAN_INTERACT_H
#define SYLVAN_SYLVAN_INTERACT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "sylvan_bitset.h"

typedef struct interact_state
{
     size_t           nrows;                  // size of a row
     size_t           size;                   // size of the bitmaps
     word_t*          bitmap;                 // bitmap for "visited node" , as many bits as there are buckets in the table, 1 -> visited, 0 -> not visited
} interact_t;

// number of variables can be at most number of nodes
#define interact_alloc_max(state) interact_alloc(state, nodes->table_size);
char interact_malloc(interact_t *matrix, size_t nvars);

void interact_free(interact_t *state);

static inline void interact_set(interact_t *state, size_t row, size_t col)
{
    bitmap_set(state->bitmap, row * state->nrows + col);
}

static inline int interact_get(const interact_t *state, size_t row, size_t col)
{
    return bitmap_get(state->bitmap, row * state->nrows + col);
}

static inline int interact_test(const interact_t *state, BDDVAR x, BDDVAR y)
{
    // ensure x < y
    // this is because we only keep the upper triangle of the matrix
    if (x > y) {
        BDDVAR tmp = x;
        x = y;
        y = tmp;
    }
    return interact_get(state, x, y);
}

/**
  @brief Marks as interacting all pairs of variables that appear in
  support.

  @details If support[i] == support[j] == 1, sets the (i,j) entry
  of the interaction matrix to 1.

  @sideeffect Clears support.

*/
void interact_update(interact_t *state, atomic_word_t* bitmap_s, size_t nvars);

void interact_print_state(const interact_t *state, size_t nvars);

VOID_TASK_DECL_1(interact_init, interact_t*)
/**
  @brief Initialize the variable interaction matrix.
*/
#define interact_init(state) RUN(interact_init, state)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_INTERACT_H
