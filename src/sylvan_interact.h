//
// Created by Andrej Pistek on 07/04/2023.
//

#ifndef SYLVAN_SYLVAN_INTERACT_H
#define SYLVAN_SYLVAN_INTERACT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "sylvan_bitmap.h"

typedef struct interact_state
{
     size_t           nrows;                  // size of a row
     size_t           size;                   // size of the bitmaps
     word_t*          bitmap;                 // bitmap for "visited node" , as many bits as there are buckets in the table, 1 -> visited, 0 -> not visited
} interact_t;

// number of variables can be at most number of nodes
#define interact_alloc_max(dbs) interact_alloc(dbs, nodes->table_size);
char interact_malloc(levels_t dbs);

void interact_free(levels_t dbs);

static inline void interact_set(levels_t dbs, size_t row, size_t col)
{
    bitmap_atomic_set(dbs->bitmap_i, (row * dbs->bitmap_i_nrows) + col);
}

static inline int interact_get(const levels_t dbs, size_t row, size_t col)
{
    return bitmap_atomic_get(dbs->bitmap_i, (row * dbs->bitmap_i_nrows) + col);
}

static inline int interact_test(const levels_t dbs, BDDVAR x, BDDVAR y)
{
    if (dbs->bitmap_i == NULL) return 1; // if the bitmap is not allocated, conservatively return 1 (positive interaction
    // fail fast, if the variable is not registered within our interaction matrix, conservatively return 1 (positive interaction)
    if (x >= dbs->bitmap_i_nrows || y >= dbs->bitmap_i_nrows) return 1;

    // ensure x < y
    // this is because we only keep the upper triangle of the matrix
    if (x > y) return interact_get(dbs, y, x);
    else return interact_get(dbs, x, y);
}

/**
  @brief Marks as interacting all pairs of variables that appear in
  support.

  @details If support[i] == support[j] == 1, sets the (i,j) entry
  of the interaction matrix to 1.

  @sideeffect Clears support.

*/
void interact_update(levels_t dbs, atomic_word_t* bitmap_s);

void interact_print_state(const levels_t dbs);

VOID_TASK_DECL_1(interact_init, levels_t)
/**
  @brief Initialize the variable interaction matrix.
*/
#define interact_init(levels) RUN(interact_init, levels)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_INTERACT_H
