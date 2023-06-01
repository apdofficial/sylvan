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
    dbs->bitmap_i = (atomic_word_t *) alloc_aligned(dbs->bitmap_i_size);

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

void interact_update(levels_t dbs, atomic_word_t *bitmap)
{
    size_t i, j;
    for (i = 0; i < dbs->bitmap_i_nrows - 1; i++) {
        if (bitmap_atomic_get(bitmap, i) == 1) {
            bitmap_atomic_clear(bitmap, i);
            for (j = i + 1; j < dbs->bitmap_i_nrows; j++) {
                if (bitmap_atomic_get(bitmap, j) == 1) {
                    interact_set(dbs, i, j);
                }
            }
        }
    }
    bitmap_atomic_clear(bitmap, dbs->bitmap_i_nrows - 1);
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

/* 40 bits for the index, 24 bits for the hash */
#define MASK_INDEX ((uint64_t)0x000000ffffffffff)
#define MASK_HASH  ((uint64_t)0xffffff0000000000)

#define node_ref_inc(dd) levels_node_ref_count_add(levels, (dd) & MASK_INDEX, 1);
#define var_inc(lvl) levels_var_count_add(levels, levels->level_to_order[lvl], 1);
#define ref_inc(lvl) levels_ref_count_add(levels, levels->level_to_order[lvl], 1);

/**
 *
 * @brief Find the support of f. (parallel)
 *
 * @sideeffect Accumulates in support the variables on which f depends.
 *
 * If F00 = F01 and F10 = F11, then F does not depend on <y>. If this is the case
 * for all the nodes of variable <x>, we say that variables <x> and <y> do not interact.
 *
 * Performs a tree search on the BDD to accumulate the support array of the variables on which f depends.
 *
 *        (x)F
 *       /   \
 *    (y)F0   (y)F1
 *    / \     / \
 *  F00 F01 F10 F11
 */
#define find_support(f, bitmap_s, bitmap_g, bitmap_l) RUN(find_support, f, bitmap_s, bitmap_g, bitmap_l)
VOID_TASK_4(find_support, MTBDD, f, atomic_word_t *, bitmap_s, atomic_word_t *, bitmap_g, atomic_word_t *, bitmap_l)
{
    if (f == mtbdd_true || f == mtbdd_false) return;
    uint64_t index = f & MASK_INDEX;
    if (bitmap_atomic_get(bitmap_l, index)) return;

    BDDVAR var = mtbdd_getvar(f);
    // set support bitmap, <var> contributes to the outcome of <f>
    bitmap_atomic_set(bitmap_s, levels->level_to_order[var]);

    MTBDD f1 = mtbdd_gethigh(f);
    MTBDD f0 = mtbdd_getlow(f);

    if (f0 != f && f0 != mtbdd_true && f0 != mtbdd_false) {
        ref_inc(mtbdd_getvar(f0));
        node_ref_inc(f0);
    }
    if (f1 != f && f1 != mtbdd_true && f1 != mtbdd_false) {
        ref_inc(mtbdd_getvar(f1));
        node_ref_inc(f1);
    }


    CALL(find_support, f1, bitmap_s, bitmap_g, bitmap_l);
    SPAWN(find_support, f0, bitmap_s, bitmap_g, bitmap_l);
    SYNC(find_support);

    // locally visited node used to avoid duplicate node visit for a given tree
    bitmap_atomic_set(bitmap_l, index);
    // globally visited node used to determining root nodes
    bitmap_atomic_set(bitmap_g, index);
}

VOID_TASK_IMPL_1(interact_var_ref_init, levels_t, dbs)
{
    size_t nnodes = nodes->table_size; // worst case (if table is full)
    size_t nvars = dbs->count;

    atomic_word_t *bitmap_s = (atomic_word_t *) alloc_aligned(nvars);  // support bitmap
    atomic_word_t *bitmap_g = (atomic_word_t *) alloc_aligned(nnodes); // globally visited nodes bitmap (forest wise)
    atomic_word_t *bitmap_l = (atomic_word_t *) alloc_aligned(nnodes); // locally visited nodes bitmap (tree wise)

    if (bitmap_s == 0 || bitmap_g == 0 || bitmap_l == 0) {
        fprintf(stderr, "interact_init failed to allocate new memory: %s!\n", strerror(errno));
        exit(1);
    }

    for (size_t index = llmsset_next(1); index != llmsset_nindex; index = llmsset_next(index)) {
        mtbddnode_t node = MTBDD_GETNODE(index);
        BDDVAR var = mtbddnode_getvariable(node);

        // we iterate over all nodes in the table so just grab var of the node and increment the var counter
        var_inc(var);

        if (bitmap_atomic_get(bitmap_g, index) == 1) {
            // already visited node, thus can not be a root and we can skip it
            continue;
        }
        // A node is a root of the DAG if it cannot be reached by nodes above it.
        // If a node was never reached during the previous searches,
        // then it is a root, and we start a new search from it.

        // set support bitmap, <var> is on the support of <f>
        bitmap_atomic_set(bitmap_s, levels->level_to_order[var]);

        if (mtbddnode_isleaf(node)) {
            // if the node was a leaf, job done
            continue;
        }

        MTBDD f1 = mtbddnode_gethigh(node);
        MTBDD f0 = mtbddnode_getlow(node);

//        if ((f0 & MASK_INDEX) != index && f0 != mtbdd_true && f0 != mtbdd_false) {
//            ref_inc(mtbdd_getvar(f0));
//            node_ref_inc(f0);
//        }
//        if ((f1 & MASK_INDEX) != index && f1 != mtbdd_true && f1 != mtbdd_false) {
//            ref_inc(mtbdd_getvar(f1));
//            node_ref_inc(f1);
//        }

        // visit all nodes reachable from <f>
        SPAWN(find_support, f1, bitmap_s, bitmap_g, bitmap_l);
        CALL(find_support, f0, bitmap_s, bitmap_g, bitmap_l);
        SYNC(find_support);

        // clear locally visited nodes bitmap,
        clear_aligned(bitmap_l, nnodes);
        // update interaction matrix
        interact_update(dbs, bitmap_s);
    }

    for (size_t i = 0; i < levels->count; i++) {
        if (levels_ref_count_load(levels, i) <= 1) {
            levels->isolated_count++;
        }
    }

    free_aligned(bitmap_s, nvars);
    free_aligned(bitmap_g, nnodes);
    free_aligned(bitmap_l, nnodes);
}