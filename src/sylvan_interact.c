#include "sylvan_int.h"
#include "sylvan_interact.h"

char interact_alloc(interact_state_t *state, size_t nvars)
{
    // TODO: reduce the memory usage, we only use 1 bit out of 8 bit char data type
    state->nrows = nvars;
    state->ncols = nvars;

    // Allocate rows
    state->interact = (char **) calloc(state->nrows, sizeof(char *));
    // Allocate columns for each row
    for (size_t i = 0; i < state->nrows; i++) {
        state->interact[i] = (char *) calloc(state->ncols, sizeof(char *));
         if (!state->interact[i]) {
            fprintf(stderr, "interact_alloc failed to allocate new memory!");
            return 0;
        }
    }

    if (!state->interact) {
        fprintf(stderr, "interact_alloc failed to allocate new memory!");
        return 0;
    }

    return 1;
}

void interact_free(interact_state_t *state)
{
    free(state->interact);
    state->interact = NULL;
    state->nrows = 0;
    state->ncols = 0;
}

void interact_update(interact_state_t *state, char *support)
{
    size_t i, j;
    size_t n = nodes->table_size;

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

void print_interact_state(const interact_state_t *state, size_t nvars)
{
    printf("Interaction matrix: \n");
    printf("  ");
    for (size_t i = 0; i < nvars; ++i) printf("%zu ", i);
    printf("\n");

    for (size_t i = 0; i < nvars; ++i) {
        printf("%zu ", i);
        for (size_t j = 0; j < nvars; ++j) {
            printf("%d ", interact_get(state, i, j));
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
 *        (x)F
 *       /   \
 *    (y)F0   (y)F1
 *    / \     / \
 *  F00 F01 F10 F11
 */
#define find_support(f, support) RUN(find_support, f, support)
VOID_TASK_2(find_support, MTBDD, f, char*, support)
{
    if (mtbdd_isleaf(f)) return;
    mtbddnode_t node = MTBDD_GETNODE(f);
    if (mtbddnode_getflag(node) == 1) return;

    SPAWN(find_support, mtbdd_gethigh(f), support);
    CALL(find_support, mtbdd_getlow(f), support);
    SYNC(find_support);

    // TODO: fix array mutation from the thread local stack
    support[mtbddnode_getvariable(node)] = 1;
    // local visited node used for calculating support array
    mtbddnode_setflag(node, 1);
    // global visited node used to determining root nodes
    mtbddnode_setvisited(MTBDD_GETNODE(f), 1);
}

#define clear_flags(f) RUN(clear_flags, f)
VOID_TASK_1(clear_flags, MTBDD, f)
{
    if (mtbdd_isleaf(f)) return;
    mtbddnode_t node = MTBDD_GETNODE(f);
    if (mtbddnode_getflag(node) == 0) return;

    SPAWN(clear_flags, mtbdd_gethigh(f));
    CALL(clear_flags, mtbdd_getlow(f));
    SYNC(clear_flags);

    mtbddnode_setflag(node, 0);
}

#define clear_visited() RUN(clear_visited, 0, nodes->table_size)
VOID_TASK_2(clear_visited, size_t, first, size_t, count)
{
    if (count > COUNT_NODES_BLOCK_SIZE) {
        size_t split = count / 2;
        SPAWN(clear_visited, first, split);
        CALL(clear_visited, first + split, count - split);
        SYNC(clear_visited);
    } else {
        const size_t end = first + count;
        for (; first < end; first++) {
            if (!llmsset_is_marked(nodes, first)) continue; // unused bucket
            mtbddnode_setvisited(MTBDD_GETNODE(first), 0);
        }
    }
}

VOID_TASK_IMPL_1(interact_init, interact_state_t*, state)
{
    size_t n = nodes->table_size;
    char *support = calloc(n, sizeof(char));
    if (!support) {
        fprintf(stderr, "interact_init failed to allocate memory!");
        return;
    }
    for (size_t i = 0; i < n; i++) support[i] = 0;

    for (size_t i = 0; i < n; i++) {
        if (!llmsset_is_marked(nodes, i)) continue; // unused bucket
        mtbddnode_t f = MTBDD_GETNODE(i);
        // A node is a root of the DAG if it cannot be reached by nodes above it.
        // If a node was never reached during the previous depth-first searches,
        // then it is a root, and we start a new depth-first search from it.
        if (!mtbddnode_getvisited(f)) {
            support[mtbddnode_getvariable(f)] = 1;
            mtbddnode_setvisited(f, 1);

            MTBDD f1 = mtbddnode_gethigh(f);
            find_support(f1, support);
            MTBDD f0 = mtbddnode_getlow(f);
            find_support(f0, support);

            clear_flags(f1);
            clear_flags(f0);

            interact_update(state, support);
        }
    }

    free(support);
    clear_visited();
}


