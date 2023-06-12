#ifndef SYLVAN_SYLVAN_INTERACT_H
#define SYLVAN_SYLVAN_INTERACT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

char interact_malloc(levels_t dbs);

void interact_free(levels_t dbs);

static inline void interact_set(levels_t dbs, size_t row, size_t col)
{
    atomic_bitmap_t bitmap = {
        .container = dbs->bitmap_i,
        .size = dbs->bitmap_i_size
    };
    atomic_bitmap_set(&bitmap, (row * dbs->bitmap_i_nrows) + col);
}

static inline int interact_get(const levels_t dbs, size_t row, size_t col)
{
    atomic_bitmap_t bitmap = {
        .container = dbs->bitmap_i,
        .size = dbs->bitmap_i_size
    };
    return atomic_bitmap_get(&bitmap, (row * dbs->bitmap_i_nrows) + col);
}

static inline int interact_test(const levels_t dbs, BDDVAR x, BDDVAR y)
{
    // ensure x < y
    // this is because we only keep the upper triangle of the matrix
    if (x > y) {
        int tmp = x;
        x = y;
        y = tmp;
    }
    return interact_get(dbs, x, y);
}

/**
  @brief Marks as interacting all pairs of variables that appear in
  support.

  @details If support[i] == support[j] == 1, sets the (i,j) entry
  of the interaction matrix to 1.

  @sideeffect Clears support.

*/
void interact_update(levels_t dbs, atomic_bitmap_t *bitmap_s);

void interact_print_state(const levels_t dbs);

VOID_TASK_DECL_1(interaction_matrix_init, levels_t)
/**
  @brief Initialize the variable interaction matrix, nodes count for each variable, and internal reference count for each variable.
*/
#define interaction_matrix_init(dbs) RUN(interaction_matrix_init, dbs)

VOID_TASK_DECL_1(var_ref_init, levels_t)
/**
  @brief Initialize nodes count for each variable, and internal reference count for each variable.
*/
#define var_ref_init(dbs) RUN(var_ref_init, dbs)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_INTERACT_H
