#include <sylvan_mrc.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>

void atomic_counters_add(atomic_counters_t* self, size_t idx, int val)
{
    assert(self->container != NULL);
    assert(self->size != 0);
    assert(val < UINT8_MAX);
    _Atomic(uint8_t) *ptr = self->container + idx;
    uint64_t curr = atomic_load_explicit(ptr, memory_order_relaxed);
    if (curr == 0 && val < 0) return; // underflow
    if ((curr + val) >= UINT8_MAX) return; // overflow
    atomic_fetch_add(ptr, val);
}

void atomic_counters_set(atomic_counters_t* self, size_t idx, uint8_t val)
{
    assert(self->container != NULL);
    assert(self->size != 0);
    if (val >= UINT8_MAX) exit(-1); // overflow, sorry really not allowed
    _Atomic(uint8_t) *ptr = self->container + idx;
    atomic_store(ptr, val);
}

uint8_t atomic_counters_get(atomic_counters_t* self, size_t idx)
{
    assert(self->container != NULL);
    assert(self->size != 0);
    _Atomic(uint8_t) *ptr = self->container + idx;
    return atomic_load_explicit(ptr, memory_order_relaxed);
}

void mrc_isolated_count_set(mrc_t* self, int val)
{
    self->isolated_count = val;
}

void mrc_ref_nodes_add(mrc_t* self, size_t idx, int val)
{
    atomic_counters_add(&self->ref_nodes, idx, val);
}

void mrc_ref_vars_add(mrc_t* self, size_t idx, int val)
{
    atomic_counters_add(&self->ref_vars, idx, val);
}

void mrc_var_nodes_add(mrc_t* self, size_t idx, int val)
{
    atomic_counters_add(&self->var_nnodes, idx, val);
}

void mrc_nnodes_add(mrc_t* self, int val)
{
    assert(val < UINT8_MAX);
    assert(self->nnodes != 0);
    uint64_t curr = mrc_nnodes_get(self);
    if (curr == 0 && val < 0) return; // avoid underflow
    if ((curr + val) >= UINT8_MAX) return;// avoid overflow
    self->nnodes += val;
}

uint8_t mrc_ref_nodes_get(mrc_t* self, size_t idx)
{
    return atomic_counters_get(&self->ref_nodes, idx);
}

uint8_t mrc_ref_vars_get(mrc_t* self, size_t idx)
{
    return atomic_counters_get(&self->ref_vars, idx);
}

uint8_t mrc_var_nodes_get(mrc_t* self, size_t idx)
{
    return atomic_counters_get(&self->var_nnodes, idx);
}

int mrc_node_count_get(mrc_t* self)
{
    return self->isolated_count;
}

int mrc_is_var_isolated(mrc_t* self, size_t idx)
{
    if (self->ref_vars.size == 0) return 0;
    return mrc_ref_vars_get(self, idx) == 1;
}

int mrc_is_node_dead(mrc_t* self, size_t idx)
{
    if (self->ext_ref_nodes.size == 0 || self->ref_nodes.size == 0) return 0;
    return mrc_var_nodes_get(self, idx) == 0;
}
