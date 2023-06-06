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
    printf("  \t");
    for (size_t i = 0; i < dbs->bitmap_i_nrows; ++i) printf("%zu ", i);
    printf("\n");

    for (size_t i = 0; i < dbs->bitmap_i_nrows; ++i) {
        printf("%zu \t", i);
        for (size_t j = 0; j < dbs->bitmap_i_nrows; ++j) {
            printf("%d ", interact_test(dbs, i, j));
            if (j > 9) printf(" ");
            if (j > 99) printf(" ");
            if (j > 999) printf(" ");
        }
        printf("\n");
    }

    printf("\n");
}

/* 40 bits for the index, 24 bits for the hash */
#define MASK_INDEX ((uint64_t)0x000000ffffffffff)
#define MASK_HASH  ((uint64_t)0xffffff0000000000)

#define node_ref_inc(dbs, dd) levels_node_ref_count_add(dbs, (dd) & MASK_INDEX, 1)
#define var_inc(dbs, lvl) levels_var_count_add(dbs, lvl, 1)
#define ref_inc(dbs, lvl) levels_ref_count_add(dbs, lvl, 1)

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
    uint64_t index = f & MASK_INDEX;
    if (index == 0 || index == 1) return;
    if (f == mtbdd_true || f == mtbdd_false) return;

    if (bitmap_atomic_get(bitmap_l, index)) return;

    BDDVAR var = mtbdd_getvar(f);
    // set support bitmap, <var> contributes to the outcome of <f>
    bitmap_atomic_set(bitmap_s, levels->level_to_order[var]);

    if(!mtbdd_isleaf(f)) {
        MTBDD f1 = mtbdd_gethigh(f);
        MTBDD f0 = mtbdd_getlow(f);

        CALL(find_support, f1, bitmap_s, bitmap_g, bitmap_l);
        SPAWN(find_support, f0, bitmap_s, bitmap_g, bitmap_l);
        SYNC(find_support);
    }

    // locally visited node used to avoid duplicate node visit for a given tree
    bitmap_atomic_set(bitmap_l, index);
    // globally visited node used to determining root nodes
    bitmap_atomic_set(bitmap_g, index);
}

VOID_TASK_IMPL_1(interaction_matrix_init, levels_t, dbs)
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

    for (size_t index = llmsset_first(); index < nodes->table_size; index = llmsset_next(index)) {
        if (index == 0 || index == 1 || index == sylvan_invalid) continue; // reserved sylvan nodes

        // A node is a root of the DAG if it cannot be reached by nodes above it.
        // If a node was never reached during the previous searches,
        // then it is a root, and we start a new search from it.
        mtbddnode_t node = MTBDD_GETNODE(index);
        if (mtbddnode_isleaf(node)) {
            // if the node was a leaf, job done
            continue;
        }

        if (bitmap_atomic_get(bitmap_g, index) == 1) {
            // already visited node, thus can not be a root and we can skip it
            continue;
        }

        MTBDD f1 = mtbddnode_gethigh(node);
        MTBDD f0 = mtbddnode_getlow(node);

        // visit all nodes reachable from <f>
        SPAWN(find_support, f1, bitmap_s, bitmap_g, bitmap_l);
        CALL(find_support, f0, bitmap_s, bitmap_g, bitmap_l);
        SYNC(find_support);

        BDDVAR var = mtbddnode_getvariable(node);
        // set support bitmap, <var> contributes to the outcome of <f>
        bitmap_atomic_set(bitmap_s, dbs->level_to_order[var]);

        // clear locally visited nodes bitmap,
        clear_aligned(bitmap_l, nnodes);
        // update interaction matrix
        interact_update(dbs, bitmap_s);
    }


    free_aligned(bitmap_s, nvars);
    free_aligned(bitmap_g, nnodes);
    free_aligned(bitmap_l, nnodes);
}

VOID_TASK_IMPL_1(var_ref_init, levels_t, dbs)
{
    levels_nodes_count_set(dbs, 2);
    for (size_t index = llmsset_first(); index < nodes->table_size; index = llmsset_next(index)) {
        if (index == 0 || index == 1) continue;
        levels_nodes_count_add(dbs, 1);

        mtbddnode_t node = MTBDD_GETNODE(index);
        BDDVAR var = mtbddnode_getvariable(node);
        var_inc(dbs, var);

        if (mtbddnode_isleaf(node)) continue;

        MTBDD f1 = mtbddnode_gethigh(node);
        if (f1 != sylvan_invalid && (f1 & MASK_INDEX) != 0 && (f1 & MASK_INDEX) != 1) {
            ref_inc(dbs, mtbdd_getvar(f1));
            node_ref_inc(dbs, f1);
        }

        MTBDD f0 = mtbddnode_getlow(node);
        if (f0 != sylvan_invalid && (f0 & MASK_INDEX) != 0 && (f0 & MASK_INDEX) != 1) {
            ref_inc(dbs, mtbdd_getvar(f0));
            node_ref_inc(dbs, f0);
        }

    }

    for (size_t index = llmsset_first(); index < nodes->table_size; index = llmsset_next(index)) {
        if (index == 0 || index == 1) continue;
        mtbddnode_t node = MTBDD_GETNODE(index);
        BDDVAR var = mtbddnode_getvariable(node);

        if (levels_node_ref_count_load(levels, index) == 0) {
            node_ref_inc(dbs, index);
        }
        if (levels_ref_count_load(levels, var) == 0) {
            ref_inc(dbs, var);
        }
    }

//    for (size_t i = 0; i < dbs->count; i++) {
//        if (levels_is_isolated(levels, i)) {
//            dbs->isolated_count++;
//        }
//    }
}