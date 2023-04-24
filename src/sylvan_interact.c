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

    return 1;
}

void interact_free(interact_t *matrix)
{
    free_aligned(matrix->bitmap, matrix->size);
    matrix->bitmap = NULL;
    matrix->nrows = 0;
    matrix->size = 0;
}

void interact_update(interact_t *state, atomic_word_t *bitmap_s, size_t nvars)
{
    size_t i, j;
    for (i = 0; i < nvars - 1; i++) {
        if (bitmap_atomic_get(bitmap_s, i) == 1) {
            bitmap_atomic_clear(bitmap_s, i);
            for (j = i + 1; j < nvars; j++) {
                if (bitmap_atomic_get(bitmap_s, j) == 1) {
                    interact_set(state, i, j);
                }
            }
        }
    }
    bitmap_atomic_clear(bitmap_s, nvars - 1);
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
#define find_support(f, bitmap_s, bitmap_v, bitmap_l) RUN(find_support, f, bitmap_s, bitmap_v, bitmap_l)
VOID_TASK_4(find_support, MTBDD, f, atomic_word_t *, bitmap_s, atomic_word_t *, bitmap_v, atomic_word_t *, bitmap_l)
{
    if (mtbdd_isleaf(f)) return;
    // The low 40 bits are an index into the unique table.
    uint64_t index = f & 0x000000ffffffffff;
    if (bitmap_atomic_get(bitmap_l, index) == 1) return;
    mtbddnode_t node = MTBDD_GETNODE(f);
    // set support bitmap, <var> is on the support of <f>
    bitmap_atomic_set(bitmap_s, mtbddnode_getvariable(node));

    SPAWN(find_support, mtbdd_gethigh(f), bitmap_s, bitmap_v, bitmap_l);
    CALL(find_support, mtbdd_getlow(f), bitmap_s, bitmap_v, bitmap_l);
    SYNC(find_support);

    // local visited node used for calculating support array
    bitmap_atomic_set(bitmap_l, index);
    // global visited node used to determining root nodes
    bitmap_atomic_set(bitmap_v, index);
}

VOID_TASK_IMPL_1(interact_init, interact_t *, state)
{
    size_t nnodes = nodes->table_size; // worst case (if table is full)
    size_t nvars = levels->count;

    atomic_word_t *bitmap_s = (atomic_word_t *) alloc_aligned(nvars); // support bitmap
    atomic_word_t *bitmap_v = (atomic_word_t *) alloc_aligned(nnodes); // visited root nodes bitmap
    atomic_word_t *bitmap_l = (atomic_word_t *) alloc_aligned(nnodes); // locally visited nodes bitmap

    if (bitmap_s == 0 || bitmap_v == 0 || bitmap_l == 0) {
        fprintf(stderr, "interact_init failed to allocate new memory: %s!\n", strerror(errno));
        exit(1);
    }

    for (size_t index = llmsset_first(); index != llmsset_nindex; index = llmsset_next(index)){
        if (bitmap_atomic_get(bitmap_v, index) == 1) continue; // already visited root node
        mtbddnode_t f = MTBDD_GETNODE(index);
        // set support bitmap, <var> is on the support of <f>
        bitmap_atomic_set(bitmap_s, mtbddnode_getvariable(f));
        // A node is a root of the DAG if it cannot be reached by nodes above it.
        // If a node was never reached during the previous depth-first searches,
        // then it is a root, and we start a new depth-first search from it.
        MTBDD f1 = mtbddnode_gethigh(f);
        MTBDD f0 = mtbddnode_getlow(f);

        // visit all nodes reachable from <f>
        SPAWN(find_support, f1, bitmap_s, bitmap_v, bitmap_l);
        CALL(find_support, f0, bitmap_s, bitmap_v, bitmap_l);
        SYNC(find_support);

        // clear locally visited nodes bitmap,
        clear_aligned(bitmap_l, nnodes);
        // update interaction matrix
        interact_update(state, bitmap_s, nvars);
    }

    free_aligned(bitmap_s, nvars);
    free_aligned(bitmap_v, nnodes);
    free_aligned(bitmap_l, nnodes);
}


