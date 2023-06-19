#include <sylvan_int.h>
#include <sylvan_align.h>

#include <errno.h>

/**
 * Atomic counters
 */
void atomic_counters_init(atomic_counters_t *self, size_t new_size)
{
    atomic_counters_deinit(self);
    self->container = (atomic_counter_t *) alloc_aligned(sizeof(atomic_counter_t[new_size]));
    if (self->container == NULL) {
        fprintf(stderr, "atomic_counter_realloc: Unable to allocate memory: %s!\n", strerror(errno));
        exit(1);
    }
    self->size = new_size;
}

void atomic_counters_deinit(atomic_counters_t *self)
{
    if (self->container != NULL && self->size > 0) {
        free_aligned(self->container, self->size);
    }
    self->size = 0;
    self->container = NULL;
}

void atomic_counters_add(atomic_counters_t *self, size_t idx, int val)
{
#ifndef NDEBUG
    assert(self->container != NULL);
    assert(self->size != 0);
#endif
    counter_t curr = atomic_counters_get(self, idx);
    if (curr == 0 && val < 0) return;               // underflow
    if ((curr + val) >= COUNTER_T_MAX) return;      // overflow
    if (idx >= self->size) return;                  // out of bounds
    atomic_counter_t *ptr = self->container + idx;
    atomic_fetch_add(ptr, val);
}

void atomic_counters_set(atomic_counters_t *self, size_t idx, counter_t val)
{
#ifndef NDEBUG
    assert(self->container != NULL);
    assert(self->size != 0);
#endif
    if (val >= COUNTER_T_MAX) return;               // overflow
    if (idx >= self->size) return;                  // out of bounds
    atomic_counter_t *ptr = self->container + idx;
    atomic_store(ptr, val);
}

counter_t atomic_counters_get(const atomic_counters_t *self, size_t idx)
{
#ifndef NDEBUG
    assert(self->container != NULL);
    assert(self->size != 0);
#endif
    atomic_counter_t *ptr = self->container + idx;
    return atomic_load(ptr);
}

/**
 * @brief MRC initialization.
 *
 * @details Traverse the forest and count the number of nodes and variables and their internal and external references.
 *
 * @preconditions
 * - The forest must be initialized.
 * - The MRC counters must be freshly initialized.
 */
void mrc_init(mrc_t *self, size_t nvars, size_t nnodes, roaring_bitmap_t *node_ids)
{
    // memory usage: # of nodes * sizeof (counter_t) bits       (16n)
    atomic_counters_init(&self->ref_nodes, nnodes);
    // memory usage: # of variables * sizeof (counter_t) bits   (16v)
    atomic_counters_init(&self->ref_vars, nvars);
    // memory usage: # of variables * sizeof (counter_t) bits   (16v)
    atomic_counters_init(&self->var_nnodes, nvars);
    // memory usage: # of nodes * 1 bit                         (n)
    atomic_bitmap_init(&self->ext_ref_nodes, nnodes);

    mrc_nnodes_set(self, 2);

    roaring_uint32_iterator_t *it = roaring_create_iterator(node_ids);
    roaring_move_uint32_iterator_equalorlarger(it, 2);

    while (it->has_value) {
        size_t index = it->current_value;
        roaring_advance_uint32_iterator(it);
        if (index == 0 || index == 1) continue;
        mrc_nnodes_add(self, 1);

        mtbddnode_t node = MTBDD_GETNODE(index);
        BDDVAR var = mtbddnode_getvariable(node);
        mrc_var_nnodes_add(self, var, 1);

        if (mtbddnode_isleaf(node)) continue;

        MTBDD f1 = mtbddnode_gethigh(node);
        size_t f1_index = f1 & SYLVAN_TABLE_MASK_INDEX;
        if (f1 != sylvan_invalid && (f1_index) != 0 && (f1_index) != 1) {
            mrc_ref_vars_add(self, mtbdd_getvar(f1), 1);
            mrc_ref_nodes_add(self, f1_index, 1);
        }

        MTBDD f0 = mtbddnode_getlow(node);
        size_t f0_index = f0 & SYLVAN_TABLE_MASK_INDEX;
        if (f0 != sylvan_invalid && (f0_index) != 0 && (f0_index) != 1) {
            mrc_ref_vars_add(self, mtbdd_getvar(f0), 1);
            mrc_ref_nodes_add(self, f0_index, 1);
        }
    }

    it = roaring_create_iterator(node_ids);
    roaring_move_uint32_iterator_equalorlarger(it, 2);

    // set all nodes with ref == 0 to 1
    // every node nees to have at least 1 reference otherwise it will be garbage collected
    while (it->has_value) {
        size_t index = it->current_value;
        roaring_advance_uint32_iterator(it);
        if (index == 0 || index == 1) continue;
        mtbddnode_t node = MTBDD_GETNODE(index);
        BDDVAR var = mtbddnode_getvariable(node);
        if (mrc_ref_nodes_get(self, index) == 0) {
            mrc_ref_nodes_add(self, index, 1);
        }
        if (mrc_ref_vars_get(self, var) == 0) {
            mrc_ref_vars_add(self, var, 1);
        }
    }

    mtbdd_re_mark_external_refs(self->ext_ref_nodes.container);
    mtbdd_re_mark_protected(self->ext_ref_nodes.container);
}

void mrc_deinit(mrc_t *self)
{
    atomic_counters_deinit(&self->ref_nodes);
    atomic_counters_deinit(&self->ref_vars);
    atomic_counters_deinit(&self->var_nnodes);
    atomic_bitmap_deinit(&self->ext_ref_nodes);
}

void mrc_isolated_count_set(mrc_t *self, int val)
{
    self->isolated_count = val;
}

void mrc_ref_nodes_set(mrc_t *self, size_t idx, int val)
{
    atomic_counters_set(&self->ref_nodes, idx, val);
}

void mrc_ref_vars_set(mrc_t *self, size_t idx, int val)
{
    atomic_counters_set(&self->ref_vars, idx, val);
}

void mrc_var_nodes_set(mrc_t *self, size_t idx, int val)
{
    atomic_counters_set(&self->var_nnodes, idx, val);
}

void mrc_nnodes_set(mrc_t *self, int val)
{
    atomic_store(&self->nnodes, val);
}

void mrc_ref_nodes_add(mrc_t *self, size_t idx, int val)
{
    atomic_counters_add(&self->ref_nodes, idx, val);
}

void mrc_ref_vars_add(mrc_t *self, size_t idx, int val)
{
    atomic_counters_add(&self->ref_vars, idx, val);
}

void mrc_var_nnodes_add(mrc_t *self, size_t idx, int val)
{
    atomic_counters_add(&self->var_nnodes, idx, val);
}

void mrc_nnodes_add(mrc_t *self, int val)
{
    atomic_fetch_add(&self->nnodes, val);
}

counter_t mrc_ext_ref_nodes_get(const mrc_t *self, size_t idx)
{
    return atomic_bitmap_get(&self->ext_ref_nodes, idx);
}

counter_t mrc_ref_nodes_get(const mrc_t *self, size_t idx)
{
    return atomic_counters_get(&self->ref_nodes, idx);
}

counter_t mrc_ref_vars_get(const mrc_t *self, size_t idx)
{
    return atomic_counters_get(&self->ref_vars, idx);
}

counter_t mrc_var_nnodes_get(const mrc_t *self, size_t idx)
{
    return atomic_counters_get(&self->var_nnodes, idx);
}

size_t mrc_nnodes_get(const mrc_t *self)
{
    return atomic_load(&self->nnodes);
}

int mrc_is_var_isolated(const mrc_t *self, size_t idx)
{
    if (self->ref_vars.size == 0) return 0;
    return mrc_ref_vars_get(self, idx) == 1;
}

int mrc_is_node_dead(const mrc_t *self, size_t idx)
{
    if (self->ext_ref_nodes.size == 0 && self->ref_nodes.size == 0) return 0;
    counter_t int_count = mrc_ref_nodes_get(self, idx);
    counter_t ext_count = mrc_ext_ref_nodes_get(self, idx);
    return int_count == 0 && ext_count == 0;
}

void mrc_gc(mrc_t *self, roaring_bitmap_t *node_ids)
{
#ifndef NDEBUG
//    // precondition:
//    // MRC and mark-and-sweep should agree on the number of nodes
//    size_t a = llmsset_count_marked(nodes) + 2;
//    size_t b = mrc_nnodes_get(&reorder_db->mrc) + 2;
//    assert(a == b);
//
//    // and with what nodes are marked
//    atomic_bitmap_t bitmap2 = {
//            .container = nodes->bitmap2,
//            .size = nodes->table_size
//    };
//    bitmap_container_t idx = atomic_bitmap_next(&bitmap2, 1);
//    for (; idx != npos && idx < nodes->table_size; idx = atomic_bitmap_next(&bitmap2, idx)) {
//        assert(llmsset_is_marked(nodes, idx));
//        assert(roaring_bitmap_contains(node_ids, idx));
//    }
//
//    // and vice versa
//    roaring_uint32_iterator_t *it_tmp = roaring_create_iterator(node_ids);
//    roaring_move_uint32_iterator_equalorlarger(it_tmp, 2);
//    while (it_tmp->has_value) {
//        size_t index = it_tmp->current_value;
//        roaring_advance_uint32_iterator(it_tmp);
//        assert(llmsset_is_marked(nodes, index));
//    }
#endif

    roaring_uint32_iterator_t *it = roaring_create_iterator(node_ids);
    roaring_move_uint32_iterator_equalorlarger(it, 2);

    while (it->has_value) {
        size_t index = it->current_value;
        roaring_advance_uint32_iterator(it);
        if (mrc_is_node_dead(self, index)) {
            mrc_delete_node(self, index);
        }
    }

#if SYLVAN_USE_LINEAR_PROBING
    sylvan_clear_and_mark();
    sylvan_rehash_all();
#else
    // together lets all workers execute a local copy of the given task
    // since we are removing nodes there will be sap ece left in the buckets which were already claimed.
    // this would generally result in occupying half of the buckets in the table since
    // all bucket would be owned by some thread but mrc_delete_node with chaining
    // would silently delete individual entries.
    llmsset_reset_all_regions();
#endif

#ifndef NDEBUG
//    // post condition:
//    // MRC and mark-and-sweep should agree on the number of nodes
//    sylvan_clear_and_mark();
//    sylvan_rehash_all();
//
//    a = llmsset_count_marked(nodes) + 2;
//    b = mrc_nnodes_get(&reorder_db->mrc) + 2;
//    assert(a == b);
//
//    // and with what nodes are marked
//    idx = atomic_bitmap_next(&bitmap2, 1);
//    for (; idx != npos && idx < nodes->table_size; idx = atomic_bitmap_next(&bitmap2, idx)) {
//        assert(roaring_bitmap_contains(node_ids, idx));
//    }
//    it_tmp = roaring_create_iterator(node_ids);
//    roaring_move_uint32_iterator_equalorlarger(it_tmp, 2);
//    while (it_tmp->has_value) {
//        size_t index = it_tmp->current_value;
//        roaring_advance_uint32_iterator(it_tmp);
//        assert(llmsset_is_marked(nodes, index));
//    }
#endif
}

void mrc_delete_node(mrc_t *self, size_t index)
{
    mtbddnode_t f = MTBDD_GETNODE(index);
    mrc_var_nnodes_add(self, mtbddnode_getvariable(f), -1);
    mrc_nnodes_add(self, -1);
    if (!mtbddnode_isleaf(f)) {
        MTBDD f1 = mtbddnode_gethigh(f);
        size_t f1_index = f1 & SYLVAN_TABLE_MASK_INDEX;
        if (f1 != sylvan_invalid && (f1_index) != 0 && (f1_index) != 1 && mtbdd_isnode(f1)) {
            mrc_ref_nodes_add(self, f1_index, -1);
            mrc_ref_vars_add(self, mtbdd_getvar(f1), -1);
//            if(mrc_is_node_dead(self, f1_index)) {
//                mrc_delete_node(self, f1_index);
//            }
        }
        MTBDD f0 = mtbddnode_getlow(f);
        size_t f0_index = f0 & SYLVAN_TABLE_MASK_INDEX;
        if (f0 != sylvan_invalid && (f0_index) != 0 && (f0_index) != 1 && mtbdd_isnode(f0)) {
            mrc_ref_nodes_add(self, f0_index, -1);
            mrc_ref_vars_add(self, mtbdd_getvar(f0), -1);
//            if(mrc_is_node_dead(self, f0_index)) {
//                mrc_delete_node(self, f0_index);
//            }
        }
    }
#if !SYLVAN_USE_LINEAR_PROBING
    llmsset_clear_one_hash(nodes, index);
    llmsset_clear_one_data(nodes, index);
#endif
}

MTBDD mrc_make_node(mrc_t *self, BDDVAR var, MTBDD low, MTBDD high, int *created)
{
    MTBDD new = mtbdd_varswap_makenode(var, low, high, created);
    if (new == mtbdd_invalid) {
        return mtbdd_invalid;
    }
    if (*created) {
        mrc_nnodes_add(self, 1);
        mrc_var_nnodes_add(self, var, 1);
        mrc_ref_nodes_set(self, new & SYLVAN_TABLE_MASK_INDEX, 1);
        mrc_ref_nodes_add(self, high & SYLVAN_TABLE_MASK_INDEX, 1);
        mrc_ref_nodes_add(self, low & SYLVAN_TABLE_MASK_INDEX, 1);
    } else {
        mrc_ref_nodes_add(self, new & SYLVAN_TABLE_MASK_INDEX, 1);
    }
    return new;
}

MTBDD mrc_make_mapnode(mrc_t *self, BDDVAR var, MTBDD low, MTBDD high, int *created)
{
    (void) self;
    MTBDD new = mtbdd_varswap_makemapnode(var, low, high, created);
    if (new == mtbdd_invalid) {
        return mtbdd_invalid;
    }
    if (*created) {
        mrc_nnodes_add(self, 1);
        mrc_var_nnodes_add(self, var, 1);
        mrc_ref_nodes_set(self, new & SYLVAN_TABLE_MASK_INDEX, 1);
        mrc_ref_nodes_add(self, high & SYLVAN_TABLE_MASK_INDEX, 1);
        mrc_ref_nodes_add(self, low & SYLVAN_TABLE_MASK_INDEX, 1);
    } else {
        mrc_ref_nodes_add(self, new & SYLVAN_TABLE_MASK_INDEX, 1);
    }
    return new;
}