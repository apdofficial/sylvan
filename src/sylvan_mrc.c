#include <sylvan_int.h>
#include <sylvan_align.h>

#include <errno.h>

VOID_TASK_DECL_4(mrc_collect_node_ids_par, uint64_t, uint64_t, atomic_bitmap_t*, roaring_bitmap_t*)

/**
 * Atomic counters
 */
void atomic_counters_init(atomic_counters_t *self, size_t new_size)
{
    assert(self != NULL);
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
    counter_t curr = atomic_counters_get(self, idx);
    if (curr == 0 && val < 0) return;               // underflow
    if ((curr + val) >= COUNTER_T_MAX) return;      // overflow
    if (idx >= self->size) return;                  // out of bounds
    atomic_fetch_add_explicit(self->container + idx, val, memory_order_relaxed);
}

void atomic_counters_set(atomic_counters_t *self, size_t idx, counter_t val)
{
    if (val >= COUNTER_T_MAX) return;               // overflow
    if (idx >= self->size) return;                  // out of bounds
    atomic_store(self->container + idx, val);
}

counter_t atomic_counters_get(const atomic_counters_t *self, size_t idx)
{
    return atomic_load_explicit(self->container + idx, memory_order_relaxed);
}

/**
 * @brief MRC initialization.
 *
 * @details Traverse the forest and count the number of nodes and variables and their internal and external references.
 *
 * @preconditions
 * - The forest must be initialized.
 */
void mrc_init(mrc_t *self, size_t nvars, size_t nnodes)
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

    roaring_uint32_iterator_t it;
    roaring_init_iterator(self->node_ids, &it);
    roaring_move_uint32_iterator_equalorlarger(&it, 2);

    while (it.has_value) {
        size_t index = it.current_value;
        roaring_advance_uint32_iterator(&it);
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

    roaring_init_iterator(self->node_ids, &it);
    roaring_move_uint32_iterator_equalorlarger(&it, 2);

    // set all nodes with ref == 0 to 1
    // every node nees to have at least 1 reference otherwise it will be garbage collected
    while (it.has_value) {
        size_t index = it.current_value;
        roaring_advance_uint32_iterator(&it);
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
    if (self->node_ids == NULL) roaring_bitmap_free(self->node_ids);
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
    atomic_fetch_add_explicit(&self->nnodes, val, memory_order_relaxed);
}

counter_t mrc_ext_ref_nodes_get(const mrc_t *self, size_t idx)
{
    return atomic_bitmap_get(&self->ext_ref_nodes, idx, memory_order_relaxed);
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
    return atomic_load_explicit(&self->nnodes, memory_order_relaxed);
}

int mrc_is_var_isolated(const mrc_t *self, size_t idx)
{
    if (self->ref_vars.size == 0) return 0;
    return mrc_ref_vars_get(self, idx) == 1;
}

int mrc_is_node_dead(const mrc_t *self, size_t idx)
{
    counter_t int_count = mrc_ref_nodes_get(self, idx);
    if (int_count > 0) return 0;
    // mrc_ext_ref_nodes_get is an atomic bitmap call which is much more expensive than mrc_ref_nodes_get
    // thus, invoke it only if really necessary
    counter_t ext_count = mrc_ext_ref_nodes_get(self, idx);
    if (ext_count > 0) return 0;
    return 1;
}

VOID_TASK_IMPL_1(mrc_gc, mrc_t*, self)
{
    roaring_bitmap_t old_ids;
    roaring_bitmap_init_cleared(&old_ids);

    roaring_uint32_iterator_t it;
    roaring_init_iterator(self->node_ids, &it);

    // we visit childrens of every node if they become dead
    // there might be up to 7 atomic writes and 4 atomic reads for every node deletion
    // for now it seems to be the limiting factor when it is parallelized, thus mrc_delete_node is invoked sequentially
    while (it.has_value) {
        if (mrc_is_node_dead(self, it.current_value)) {
            mrc_delete_node(self, it.current_value, &old_ids);
        }
        roaring_advance_uint32_iterator(&it);
    }

    // calling bitmap remove per each node is more expensive than calling it once with many ids
    // thus, we group the ids into <arr> and let the bitmap delete them in one go
    roaring_uint32_iterator_t it_old;
    roaring_init_iterator(&old_ids, &it_old);
    size_t size = roaring_bitmap_get_cardinality(&old_ids);
    uint32_t arr[size];
    size_t x = 0;
    while (it_old.has_value) {
        arr[x] = it_old.current_value;
        roaring_advance_uint32_iterator(&it_old);
        x++;
    }
    roaring_bitmap_remove_many(self->node_ids, size, arr);

#if SYLVAN_USE_LINEAR_PROBING
    sylvan_clear_and_mark();
    sylvan_rehash_all();
#endif

}

void mrc_delete_node(mrc_t *self, size_t index, roaring_bitmap_t *local_old_ids)
{
    mtbddnode_t f = MTBDD_GETNODE(index);
    mrc_var_nnodes_add(self, mtbddnode_getvariable(f), -1);
    mrc_nnodes_add(self, -1);
    roaring_bitmap_add(local_old_ids, index);
    if (!mtbddnode_isleaf(f)) {
        MTBDD f1 = mtbddnode_gethigh(f);
        size_t f1_index = f1 & SYLVAN_TABLE_MASK_INDEX;
        if (f1 != sylvan_invalid && (f1_index) != 0 && (f1_index) != 1 && mtbdd_isnode(f1)) {
            mrc_ref_nodes_add(self, f1_index, -1);
            mrc_ref_vars_add(self, mtbdd_getvar(f1), -1);
            if (mrc_is_node_dead(self, f1_index)) {
                mrc_delete_node(self, f1_index, local_old_ids);
            }
        }
        MTBDD f0 = mtbddnode_getlow(f);
        size_t f0_index = f0 & SYLVAN_TABLE_MASK_INDEX;
        if (f0 != sylvan_invalid && (f0_index) != 0 && (f0_index) != 1 && mtbdd_isnode(f0)) {
            mrc_ref_nodes_add(self, f0_index, -1);
            mrc_ref_vars_add(self, mtbdd_getvar(f0), -1);
            if (mrc_is_node_dead(self, f0_index)) {
                mrc_delete_node(self, f0_index, local_old_ids);
            }
        }
    }
#if !SYLVAN_USE_LINEAR_PROBING
    llmsset_clear_one_hash(nodes, index);
    llmsset_clear_one_data(nodes, index);
#endif
}

VOID_TASK_IMPL_2(mrc_collect_node_ids, mrc_t*, self, llmsset_t, dbs)
{
    atomic_bitmap_t bitmap = {
            .container = dbs->bitmap2,
            .size = dbs->table_size
    };
    roaring_bitmap_clear(self->node_ids);
    CALL(mrc_collect_node_ids_par, 0, bitmap.size, &bitmap, self->node_ids);
}

VOID_TASK_IMPL_4(mrc_collect_node_ids_par, uint64_t, first, uint64_t, count, atomic_bitmap_t*, bitmap, roaring_bitmap_t *, collected_ids)
{
    if (count > NBITS_PER_BUCKET * 8) {
        // standard reduction pattern with local roaring bitmaps collecting new node indices
        size_t split = count / 2;
        roaring_bitmap_t a;
        roaring_bitmap_init_cleared(&a);
        SPAWN(mrc_collect_node_ids_par, first, split, bitmap, &a);
        roaring_bitmap_t b;
        roaring_bitmap_init_cleared(&b);
        CALL(mrc_collect_node_ids_par, first + split, count - split, bitmap, &b);
        roaring_bitmap_or_inplace(collected_ids, &b);
        SYNC(mrc_collect_node_ids_par);
        roaring_bitmap_or_inplace(collected_ids, &a);
        return;
    }

    // skip buckets 0 and 1
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }

    const size_t end = first + count;
    for (first = atomic_bitmap_next(bitmap, first - 1); first < end; first = atomic_bitmap_next(bitmap, first)) {
        roaring_bitmap_add(collected_ids, first);
    }
}

MTBDD mrc_make_node(mrc_t *self, BDDVAR var, MTBDD low, MTBDD high, int *created, int add_id)
{
    MTBDD new = mtbdd_varswap_makenode(var, low, high, created);
    if (new == mtbdd_invalid) {
        return mtbdd_invalid;
    }
    if (*created) {
        mrc_nnodes_add(self, 1);
        mrc_var_nnodes_add(self, var, 1);
        if (add_id) roaring_bitmap_add(self->node_ids, new & SYLVAN_TABLE_MASK_INDEX);
        mrc_ref_nodes_set(self, new & SYLVAN_TABLE_MASK_INDEX, 1);
        mrc_ref_nodes_add(self, high & SYLVAN_TABLE_MASK_INDEX, 1);
        mrc_ref_nodes_add(self, low & SYLVAN_TABLE_MASK_INDEX, 1);
    } else {
        mrc_ref_nodes_add(self, new & SYLVAN_TABLE_MASK_INDEX, 1);
    }
    return new;
}

MTBDD mrc_make_mapnode(mrc_t *self, BDDVAR var, MTBDD low, MTBDD high, int *created, int add_id)
{
    MTBDD new = mtbdd_varswap_makemapnode(var, low, high, created);
    if (new == mtbdd_invalid) {
        return mtbdd_invalid;
    }
    if (*created) {
        mrc_nnodes_add(self, 1);
        mrc_var_nnodes_add(self, var, 1);
        if (add_id) roaring_bitmap_add(self->node_ids, new & SYLVAN_TABLE_MASK_INDEX);
        mrc_ref_nodes_set(self, new & SYLVAN_TABLE_MASK_INDEX, 1);
        mrc_ref_nodes_add(self, high & SYLVAN_TABLE_MASK_INDEX, 1);
        mrc_ref_nodes_add(self, low & SYLVAN_TABLE_MASK_INDEX, 1);
    } else {
        mrc_ref_nodes_add(self, new & SYLVAN_TABLE_MASK_INDEX, 1);
    }
    return new;
}