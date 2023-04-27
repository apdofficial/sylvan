#include "sylvan_int.h"
#include "sylvan_interact.h"
#include <sylvan_align.h>
#include <errno.h>      // for errno

char interact_malloc(levels_t dbs)
{
    if (dbs->bitmap_i_size == dbs->count) {
        clear_aligned(dbs->bitmap_i, dbs->bitmap_i_size);
        return 1;
    } else if (dbs->bitmap_i_size != 0) {
        interact_free(dbs);
    }

    dbs->bitmap_i_size = dbs->count * dbs->count; // we have a square matrix, # of vars * # of vars
    dbs->bitmap_i_nrows = dbs->count;
    dbs->bitmap_i = NULL;
    dbs->bitmap_i = (atomic_word_t*) alloc_aligned(dbs->bitmap_i_size);

    if (dbs->bitmap_i == 0) {
        fprintf(stderr, "interact_malloc failed to allocate new memory: %s!\n", strerror(errno));
        exit(1);
    }

    return 1;
}

void interact_free(levels_t dbs)
{
    if (dbs->bitmap_i_size == 0) return;

    free_aligned(dbs->bitmap_i, dbs->bitmap_i_size);
    dbs->bitmap_i = NULL;
    dbs->bitmap_i_nrows = 0;
    dbs->bitmap_i_size = 0;
}

void interact_update(levels_t dbs, atomic_word_t *bitmap_s)
{
    size_t i, j;
    for (i = 0; i < dbs->bitmap_i_nrows - 1; i++) {
        if (bitmap_atomic_get(bitmap_s, i) == 1) {
            bitmap_atomic_clear(bitmap_s, i);
            for (j = i + 1; j < dbs->bitmap_i_nrows; j++) {
                if (bitmap_atomic_get(bitmap_s, j) == 1) {
                    interact_set(dbs, i, j);
                }
            }
        }
    }
    bitmap_atomic_clear(bitmap_s, dbs->bitmap_i_nrows - 1);
}

void interact_print_state(const levels_t dbs)
{
    printf("Interaction matrix: \n");
    printf("  ");
    for (size_t i = 0; i < dbs->bitmap_i_nrows; ++i) printf("%zu ", i);
    printf("\n");

    for (size_t i = 0; i < dbs->bitmap_i_nrows; ++i) {
        printf("%zu ", i);
        for (size_t j = 0; j < dbs->bitmap_i_nrows; ++j) {
            printf("%d ", interact_get(dbs, i, j));
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

VOID_TASK_IMPL_1(interact_init, levels_t, dbs)
{
    interact_malloc(dbs);
    size_t nnodes = nodes->table_size; // worst case (if table is full)
    size_t nvars = dbs->count;

    atomic_word_t *bitmap_s = (atomic_word_t *) alloc_aligned(nvars); // support bitmap
    atomic_word_t *bitmap_v = (atomic_word_t *) alloc_aligned(nnodes); // visited root nodes bitmap
    atomic_word_t *bitmap_l = (atomic_word_t *) alloc_aligned(nnodes); // locally visited nodes bitmap

    if (bitmap_s == 0 || bitmap_v == 0 || bitmap_l == 0) {
        fprintf(stderr, "interact_init failed to allocate new memory: %s!\n", strerror(errno));
        exit(1);
    }

    for (size_t index = llmsset_next(1); index != llmsset_nindex; index = llmsset_next(index)){
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
        interact_update(dbs, bitmap_s);
    }

    free_aligned(bitmap_s, nvars);
    free_aligned(bitmap_v, nnodes);
    free_aligned(bitmap_l, nnodes);
}


