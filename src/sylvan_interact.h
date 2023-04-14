//
// Created by Andrej Pistek on 07/04/2023.
//

#ifndef SYLVAN_SYLVAN_INTERACT_H
#define SYLVAN_SYLVAN_INTERACT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct interact_state
{
    char **interact;   // interacting variable matrix
    size_t ncols;
    size_t nrows;
} interact_state_t;

// number of variables can be at most number of nodes
#define interact_alloc_max(state) interact_alloc(state, nodes->table_size);
char interact_alloc(interact_state_t *state, size_t len);

void interact_free(interact_state_t *state);

static inline void interact_set(interact_state_t *state, size_t row, size_t col, char value)
{
    state->interact[row][col] = value;
}

static inline int interact_get(const interact_state_t *state, size_t row, size_t col)
{
    return state->interact[row][col];
}

static inline int interact_test(const interact_state_t *state, BDDVAR x, BDDVAR y)
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
void interact_update(interact_state_t *state, char* support);

void print_interact_state(const interact_state_t *state, size_t nvars);

VOID_TASK_DECL_1(interact_init, interact_state_t*)
/**
  @brief Initialize the variable interaction matrix.
*/
#define interact_init(state) RUN(interact_init, state)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_INTERACT_H
