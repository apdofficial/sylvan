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
    uint8_t *interact;   // interacting variable matrix
    size_t nrows;
    size_t ncols;
} interact_state_t;

int interact_alloc(interact_state_t *state, size_t len);

void interact_free(interact_state_t *state);

static inline void interact_set(interact_state_t *state, size_t row, size_t column, uint8_t value)
{
    state->interact[(row) * state->ncols + (column)] = value;
}

static inline uint8_t interact_get(const interact_state_t *state, size_t row, size_t column)
{
    return state->interact[(row) * state->ncols + (column)];
}

static inline uint8_t interact_test(const interact_state_t *state, BDDVAR x, BDDVAR y)
{
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
  of the interaction matrix to 1.]

  @sideeffect Clears support.

*/
void interact_update(interact_state_t *state, uint8_t* support);

VOID_TASK_DECL_1(interact_init, interact_state_t*)
/**
  @brief Initialize the variable interaction matrix.
*/
#define interact_init(state) RUN(interact_init, state)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_INTERACT_H
