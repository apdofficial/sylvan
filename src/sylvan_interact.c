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

void print_interact(const interact_state_t *state)
{
    printf("Interaction matrix: \n");
    for (size_t col = 0; col < state->ncols; ++col){
        for (size_t row = 0; row < state->nrows; ++row){
            printf("%d ", interact_get(state, row, col));
        }
        printf("\n");
    }

    printf("\n");
}

/**
 *
 * @brief Find the support of f. (parallel)
 *
 * @sideeffect Accumulates in support the variables on which f depends.
 *
 * If F00 = F01 and F10 = F11, then F does not depend on <y>.
 * Therefore, it is not moved or changed by the swap. If this is the case
 * for all the nodes of variable <x>, we say that variables <x> and <y> do not interact.
 *
 * Performs a DFS on the BDD to accumulate the support array of the variables on which f depends.
 *
 *        (x) F
 *       /   \
 *    (y)F0   (y)F1
 *    / \     / \
 *  F00 F01 F10 F11
 */
#define find_support(f, support) RUN(find_support, f, support)
VOID_TASK_2(find_support, MTBDD, f, volatile uint8_t*, support)
{
    if (mtbdd_isleaf(f) || mtbddnode_getflag(MTBDD_GETNODE(f))) return; // a leaf or already visited node
    BDDVAR var = mtbdd_getvar(f);
    // these are atomic operations on a hot location with false sharing inside
    // another thread's program stack
    __sync_add_and_fetch(&support[var], 1);

    SPAWN(find_support, mtbdd_getlow(f), support);
    CALL(find_support, mtbdd_gethigh(f), support);
    SYNC(find_support);

    // local visited node used for calculating support array
    mtbddnode_setflag(MTBDD_GETNODE(mtbdd_gethigh(f)), 1);
    // global visited node used to determining root nodes
    mtbddnode_setvisited(MTBDD_GETNODE(f), 1);
}

#define clear_flags(f) RUN(clear_flags, f)
VOID_TASK_1(clear_flags, MTBDD, f)
{
    if (mtbdd_isleaf(f) || !mtbddnode_getflag(MTBDD_GETNODE(mtbdd_gethigh(f)))) return;
    mtbddnode_setvisited(MTBDD_GETNODE(f), 0);

    SPAWN(clear_flags, mtbdd_getlow(f));
    CALL(clear_flags, mtbdd_gethigh(f));
    SYNC(clear_flags);
}

#define clear_visited() RUN(clear_visited, 0, nodes->table_size)
VOID_TASK_2(clear_visited, size_t, first, size_t, count)
{
    // Divide-and-conquer if count above COUNT_NODES_BLOCK_SIZE
    if (count > COUNT_NODES_BLOCK_SIZE) {
        size_t split = count/2;
        SPAWN(clear_visited, first, split);
        CALL(clear_visited, first + split, count - split);
        SYNC(clear_visited);
    } else {
        const size_t end = first + count;
        for (; first < end; first++) {
            if (!llmsset_is_marked(nodes, first)) continue; // unused bucket
            mtbddnode_t node = MTBDD_GETNODE(first);
            mtbddnode_setvisited(node, 0);
        }
    }
}

VOID_TASK_IMPL_1(interact_init, interact_state_t*, state)
{
    size_t n = mtbdd_levelscount();
    uint8_t* support = calloc(n, sizeof(uint8_t));
    for (size_t i = 0; i < n; i++) support[i] = 0;

    for (size_t i = 0; i < nodes->table_size; i++){
        if (!llmsset_is_marked(nodes, i)) continue; // unused bucket
        mtbddnode_t f = MTBDD_GETNODE(i);
        // A node is a root of the DAG if it cannot be reached by nodes above it.
        // If a node was never reached during the previous depth-first searches,
        // then it is a root, and we start a new depth-first search from it.
        if(!mtbddnode_getvisited(f)){
            find_support(i, support);
            clear_flags(i);
            interact_update(state, support);
        }
    }
    clear_visited();
    free(support);
}


