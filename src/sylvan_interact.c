#include <sylvan_int.h>
#include <sylvan_align.h>
#include <roaring.h>
#include <errno.h>

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
    dbs->bitmap_i = (_Atomic(uint64_t) *) alloc_aligned(dbs->bitmap_i_size);

    if (dbs->bitmap_i == NULL) {
        fprintf(stderr, "interact_malloc failed to allocate new memory: %s!\n", strerror(errno));
        return 0;
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

void interact_update(levels_t dbs, atomic_bitmap_t *bitmap)
{
    size_t i, j;
    for (i = 0; i < dbs->bitmap_i_nrows - 1; i++) {
        if (atomic_bitmap_get(bitmap, i) == 1) {
            atomic_bitmap_clear(bitmap, i);
            for (j = i + 1; j < dbs->bitmap_i_nrows; j++) {
                if (atomic_bitmap_get(bitmap, j) == 1) {
                    interact_set(dbs, i, j);
                }
            }
        }
    }
    atomic_bitmap_clear(bitmap, dbs->bitmap_i_nrows - 1);
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
VOID_TASK_4(find_support, MTBDD, f, atomic_bitmap_t*, bitmap_s, atomic_bitmap_t*, bitmap_g, atomic_bitmap_t*, bitmap_l)
{
    uint64_t index = f & SYLVAN_TABLE_MASK_INDEX;
    if (index == 0 || index == 1 || index == sylvan_invalid) return;
    if (f == mtbdd_true || f == mtbdd_false) return;

    if (atomic_bitmap_get(bitmap_l, index)) return;

    BDDVAR var = mtbdd_getvar(f);
    // set support bitmap, <var> contributes to the outcome of <f>
    atomic_bitmap_set(bitmap_s, levels->level_to_order[var]);

    if(!mtbdd_isleaf(f)) {
        // visit all nodes reachable from <f>
        MTBDD f1 = mtbdd_gethigh(f);
        MTBDD f0 = mtbdd_getlow(f);
        SPAWN(find_support, f1, bitmap_s, bitmap_g, bitmap_l);
        CALL(find_support, f0, bitmap_s, bitmap_g, bitmap_l);
        SYNC(find_support);
    }

    // locally visited node used to avoid duplicate node visit for a given tree
    atomic_bitmap_set(bitmap_l, index);
    // globally visited node used to determining root nodes
    atomic_bitmap_set(bitmap_g, index);
}

VOID_TASK_IMPL_1(interaction_matrix_init, levels_t, dbs)
{
    size_t nnodes = nodes->table_size; // worst case (if table is full)
    size_t nvars = dbs->count;

    atomic_bitmap_t *bitmap_s = (atomic_bitmap_t *) alloc_aligned(nvars);  // support bitmap
    atomic_bitmap_t *bitmap_g = (atomic_bitmap_t *) alloc_aligned(nnodes); // globally visited nodes bitmap (forest wise)
    atomic_bitmap_t *bitmap_l = (atomic_bitmap_t *) alloc_aligned(nnodes); // locally visited nodes bitmap (tree wise)

    if (bitmap_s == NULL || bitmap_g == NULL || bitmap_l == NULL) {
        fprintf(stderr, "interact_init failed to allocate new memory: %s!\n", strerror(errno));
        exit(1);
    }

    nodes_iterator_t *it = roaring_create_iterator(reorder_db->node_ids);
    roaring_move_uint32_iterator_equalorlarger(it, 2);

    while (it->has_value && it->current_value < nodes->table_size) {
        size_t index = it->current_value;
        roaring_advance_uint32_iterator(it);
        // A node is a root of the DAG if it cannot be reached by nodes above it.
        // If a node was never reached during the previous searches,
        // then it is a root, and we start a new search from it.
        mtbddnode_t node = MTBDD_GETNODE(index);
        if (mtbddnode_isleaf(node)) {
            // if the node was a leaf, job done
            continue;
        }

        if (atomic_bitmap_get(bitmap_g, index) == 1) {
            // already visited node, thus can not be a root and we can skip it
            continue;
        }

        // visit all nodes reachable from <f>
        MTBDD f1 = mtbddnode_gethigh(node);
        MTBDD f0 = mtbddnode_getlow(node);
        SPAWN(find_support, f1, bitmap_s, bitmap_g, bitmap_l);
        CALL(find_support, f0, bitmap_s, bitmap_g, bitmap_l);
        SYNC(find_support);

        BDDVAR var = mtbddnode_getvariable(node);
        // set support bitmap, <var> contributes to the outcome of <f>
        atomic_bitmap_set(bitmap_s, dbs->level_to_order[var]);

        // clear locally visited nodes bitmap,
        // TODO: investigate: it is a hotspot of this function takes cca 10 - 20% of the runtime aand it scales with the table size :(
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

    nodes_iterator_t *it = roaring_create_iterator(reorder_db->node_ids);
    roaring_move_uint32_iterator_equalorlarger(it, 2);

    while (it->has_value && it->current_value < nodes->table_size) {
        size_t index = it->current_value;
        roaring_advance_uint32_iterator(it);
        if (index == 0 || index == 1) continue;
        ref_counters_node_count_add(&reorder_db->ref_counters, 1);
        levels_nodes_count_add(dbs, 1);

        mtbddnode_t node = MTBDD_GETNODE(index);
        BDDVAR var = mtbddnode_getvariable(node);
        levels_var_count_add(dbs, var, 1);

        if (mtbddnode_isleaf(node)) continue;

        MTBDD f1 = mtbddnode_gethigh(node);
        if (f1 != sylvan_invalid && (f1 & SYLVAN_TABLE_MASK_INDEX) != 0 && (f1 & SYLVAN_TABLE_MASK_INDEX) != 1) {
            levels_ref_count_add(dbs, mtbdd_getvar(f1), 1);
            levels_node_ref_count_add(dbs, f1 & SYLVAN_TABLE_MASK_INDEX, 1);
        }

        MTBDD f0 = mtbddnode_getlow(node);
        if (f0 != sylvan_invalid && (f0 & SYLVAN_TABLE_MASK_INDEX) != 0 && (f0 & SYLVAN_TABLE_MASK_INDEX) != 1) {
            levels_ref_count_add(dbs, mtbdd_getvar(f0), 1);
            levels_node_ref_count_add(dbs, f0 & SYLVAN_TABLE_MASK_INDEX, 1);
        }
    }

    it = roaring_create_iterator(reorder_db->node_ids);
    roaring_move_uint32_iterator_equalorlarger(it, 2);

    while (it->has_value && it->current_value < nodes->table_size) {
        size_t index = it->current_value;
        roaring_advance_uint32_iterator(it);
        if (index == 0 || index == 1) continue;
        mtbddnode_t node = MTBDD_GETNODE(index);
        BDDVAR var = mtbddnode_getvariable(node);
        if (levels_node_ref_count_load(levels, index) == 0) {
            levels_node_ref_count_add(dbs, index, 1);
        }
        if (levels_ref_count_load(levels, var) == 0) {
            levels_ref_count_add(dbs, var, 1);
        }
    }
}