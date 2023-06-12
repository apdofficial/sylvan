#ifndef SYLVAN_BENCHMARKS_SYLVAN_MRC_H
#define SYLVAN_BENCHMARKS_SYLVAN_MRC_H

#include <stdint.h>
#include <stddef.h>
#include <sylvan_bitmap.h>

/**
 * Atomic array containing counts.
 */
typedef struct atomic_counters_s
{
    _Atomic(uint8_t) *container;
    size_t size;
} atomic_counters_t;

void atomic_counters_add(atomic_counters_t* self, size_t idx, int val);

void atomic_counters_set(atomic_counters_t* self, size_t idx, uint8_t val);

uint8_t atomic_counters_get(atomic_counters_t* self, size_t idx);


/**
 * Manual Reference Counter (MRC) for the unique table nodes.
 * Used for tracking dead nodes during dynamic variable reordering and
 * performing selective garbage collection.
 */
typedef struct mrc_s
{
    int                     isolated_count;         // number of isolated projection functions
    size_t                  nnodes;                 // number of nodes all nodes in DD
    atomic_counters_t       ref_nodes;              // number of internal references per node (use node unique table index)
    atomic_counters_t       ref_vars;               // number of internal references per variable (use variable order)
    atomic_counters_t       var_nnodes;              // number of nodes per variable (use variable order)
    atomic_bitmap_t         ext_ref_nodes;          // bitmap of nodes with external references (1 -> has some, 0 -> no external references)
} mrc_t;

/**
 * init/ deinit functions.
 */
void atomic_counter_init(atomic_counters_t* self, size_t new_size);

void atomic_counter_deinit(atomic_counters_t *self);

/**
 * setter/ getter functions
 */
void mrc_isolated_count_set(mrc_t* self, int val);

void mrc_ref_nodes_add(mrc_t* self, size_t idx, int val);

void mrc_ref_vars_add(mrc_t* self, size_t idx, int val);

void mrc_var_nodes_add(mrc_t* self, size_t idx, int val);

void mrc_nnodes_add(mrc_t* self, int val);

uint8_t mrc_isolated_count_get(mrc_t* self);

uint8_t mrc_ref_nodes_get(mrc_t* self, size_t idx);

uint8_t mrc_ref_vars_get(mrc_t* self, size_t idx);

uint8_t mrc_var_nodes_get(mrc_t* self, size_t idx);

uint8_t mrc_nnodes_get(mrc_t* self);

/**
 * utility functions
 */
int mrc_is_var_isolated(mrc_t* self, size_t idx);

int mrc_is_node_dead(mrc_t* self, size_t idx);

#endif //SYLVAN_BENCHMARKS_SYLVAN_MRC_H
