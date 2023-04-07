#include "sylvan_int.h"
#include "sylvan_interact.h"

int interact_alloc(interact_state_t *state, size_t len)
{
    state->nrows = len;
    state->ncols = len;
    state->interact = calloc(state->nrows * state->ncols, sizeof(uint8_t));

    if (!state->interact) {
        fprintf(stderr, "interact_alloc failed to allocate new memory!");
        return 0;
    }
    for (size_t row= 0; row < state->nrows; row++) {
        for (size_t column = 0; column < state->ncols; column++) {
            interact_set(state, row, column, 0);
        }
    }
    return 1;
}

void interact_free(interact_state_t *state)
{
    free(state->interact);
    state->interact = NULL;
    state->ncols = 0;
    state->nrows = 0;
}


void interact_update(interact_state_t *state, uint8_t* support)
{
    size_t i, j;
    size_t n = mtbdd_levelscount();

    for (i = 0; i < n - 1; i++) {
        if (support[i] == 1) {
            support[i] = 0;
            for (j = i + 1; j < n; j++) {
                if (support[j] == 1) {
                    interact_set(state, i, j, 1);
                }
            }
        }
    }
    support[n - 1] = 0;
}

VOID_TASK_IMPL_4(interact_find_support_par,
                 mtbddnode_t, node,
                 volatile uint8_t*, support,
                 size_t, first,
                 size_t, count)
{
    // Divide-and-conquer if count above COUNT_NODES_BLOCK_SIZE
    if (count > COUNT_NODES_BLOCK_SIZE) {
        SPAWN(interact_find_support_par, node, support, first, count / 2);
        CALL(interact_find_support_par, node, support, first + count / 2, count - count / 2);
        SYNC(interact_find_support_par);
    } else {
        const size_t end = first + count;
        for (; first < end; first++) {
            if (!llmsset_is_marked(nodes, first)) continue; // unused bucket
            mtbddnode_t nnode = MTBDD_GETNODE(first);
            if (mtbddnode_isleaf(node)) continue; // a leaf
            if (mtbddnode_getcomp(node)) continue; // a complemented node

            BDDLABEL nvar = mtbddnode_getvariable(nnode);
            BDDLABEL var = mtbddnode_getvariable(node);
            if (nvar != var) continue; // not interested in other variables

            support[nvar] = 1;
        }
    }
}

VOID_TASK_IMPL_1(interact_init_par, interact_state_t*, state)
{
    size_t n = mtbdd_levelscount();
    uint8_t* support = calloc(n, sizeof(uint8_t));
    for (size_t i = 0; i < n; i++) support[i] = 0;

    for (size_t i = 0; i < nodes->table_size; ++i){
        if (!llmsset_is_marked(nodes, i)) continue; // unused bucket
        mtbddnode_t node = MTBDD_GETNODE(i);
        printf("find support for node %u\n", mtbddnode_getvariable(node));
        if(!mtbddnode_getcomp(node)) continue;
        interact_find_support_par(node, support);
        interact_update(state, support);
    }
    printf("\n");
    free(support);
}
