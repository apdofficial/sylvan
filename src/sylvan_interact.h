#ifndef SYLVAN_SYLVAN_INTERACT_H
#define SYLVAN_SYLVAN_INTERACT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

char interact_malloc(levels_t dbs);

void interact_free(levels_t dbs);

void interact_set(levels_t dbs, size_t row, size_t col);

int interact_get(levels_t dbs, size_t row, size_t col);

int interact_test(levels_t dbs, uint32_t x, uint32_t y);

/**
  @brief Marks as interacting all pairs of variables that appear in
  support.

  @details If support[i] == support[j] == 1, sets the (i,j) entry
  of the interaction matrix to 1.

  @sideeffect Clears support.

*/
void interact_update(levels_t dbs, atomic_bitmap_t *bitmap_s);

void interact_print_state(levels_t dbs);

VOID_TASK_DECL_1(interaction_matrix_init, size_t)
/**
  @brief Initialize the variable interaction matrix, nodes count for each variable, and internal reference count for each variable.
*/
#define interaction_matrix_init(nnodes) RUN(interaction_matrix_init, nnodes)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_INTERACT_H
