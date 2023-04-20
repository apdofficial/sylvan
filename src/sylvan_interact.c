#include "sylvan_int.h"
#include "sylvan_interact.h"
#include <sylvan_align.h>
#include <errno.h>      // for errno

char interact_malloc(interact_t *matrix, size_t nvars)
{
    matrix->size = nvars * 2; // we have a square matrix
    matrix->nrows = nvars;

    matrix->bitmap = (uint64_t *) alloc_aligned(matrix->size);

    if (matrix->bitmap == 0) {
        fprintf(stderr, "interact_malloc failed to allocate new memory: %s!\n", strerror(errno));
        exit(1);
    }
    memset(matrix->bitmap, 0, matrix->size);

    return 1;
}

void interact_free(interact_t *matrix)
{
    free_aligned(matrix->bitmap, matrix->size);
    matrix->bitmap = NULL;
    matrix->nrows = 0;
    matrix->size = 0;
}

void interact_update(interact_t *state, atomic_word_t *bitmap_s)
{
    size_t i, j;
    size_t n = nodes->table_size;

    for (i = 0; i < n - 1; i++) {
        if (bitmap_atomic_get(bitmap_s, i) == 1) {
            bitmap_atomic_clear(bitmap_s, i);
            for (j = i + 1; j < n; j++) {
                if (bitmap_atomic_get(bitmap_s, j) == 1) {
                    interact_set(state, i, j);
                }
            }
        }
    }
    bitmap_atomic_clear(bitmap_s, n - 1);
}

void interact_print_state(const interact_t *state, size_t nvars)
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
VOID_TASK_2(find_support, MTBDD, f, _Atomic (word_t)*, bitmap_s)
{
    if (mtbdd_isleaf(f)) return;
    mtbddnode_t node = MTBDD_GETNODE(f);
    if (mtbddnode_getflag(node) == 1) return;

    bitmap_atomic_set(bitmap_s, mtbddnode_getvariable(node));

    SPAWN(find_support, mtbdd_gethigh(f), bitmap_s);
    CALL(find_support, mtbdd_getlow(f), bitmap_s);
    SYNC(find_support);

    // local visited node used for calculating support array
    mtbddnode_setflag(node, 1);
    // global visited node used to determining root nodes
    mtbddnode_setvisited(node, 1);
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
    if (count > 1024) {
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

char **subtables_malloc()
{
    char **subtables = calloc(levels->count, sizeof(char *));
    if (!subtables) {
        fprintf(stderr, "interact_malloc failed to allocate new memory!");
        return NULL;
    }

    for (size_t i = 0; i < levels->count; i++) {
        subtables[i] = calloc(nodes->table_size, sizeof(char));
        if (!subtables[i]) {
            fprintf(stderr, "interact_malloc failed to allocate new memory!");
            return NULL;
        }
    }
    return subtables;
}

void subtables_free(char **subtables)
{
    for (size_t i = 0; i < levels->count; i++) {
        free(subtables[i]);
        subtables[i] = NULL;
    }
}

VOID_TASK_IMPL_1(interact_init, interact_t *, state)
{
    size_t n = nodes->table_size;

    atomic_word_t *bitmap_s = (atomic_word_t *) alloc_aligned(n); // support bitmap

    if (bitmap_s == 0) {
        fprintf(stderr, "interact_init failed to allocate new memory: %s!\n", strerror(errno));
        exit(1);
    }
    memset(bitmap_s, 0, n);

    char **subtables = subtables_malloc();
    sylvan_init_subtables(subtables);

    for (size_t var = 0; var < levels->count; var++) {
        char *nodelist = subtables[var];
        for (size_t index = 0; index < n; ++index) {
            if (!nodelist[index]) continue;
            if (!llmsset_is_marked(nodes, index)) continue; // unused bucket
            mtbddnode_t f = MTBDD_GETNODE(index);
            if (mtbddnode_isleaf(f)) continue;

            // A node is a root of the DAG if it cannot be reached by nodes above it.
            // If a node was never reached during the previous depth-first searches,
            // then it is a root, and we start a new depth-first search from it.
            if (!mtbddnode_getvisited(f)) {
                bitmap_atomic_set(bitmap_s, mtbddnode_getvariable(f));
                mtbddnode_setvisited(f, 1);

                MTBDD f1 = mtbddnode_gethigh(f);
                MTBDD f0 = mtbddnode_getlow(f);

                SPAWN(find_support, f1, bitmap_s);
                CALL(find_support, f0, bitmap_s);
                SYNC(find_support);

                SPAWN(clear_flags, f1);
                CALL(clear_flags, f0);
                SYNC(clear_flags);

                interact_update(state, bitmap_s);
            }
        }
    }

    subtables_free(subtables);
    free_aligned(bitmap_s, n);
    clear_visited();
}


