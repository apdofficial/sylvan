#ifndef SYLVAN_BENCHMARKS_SYLVAN_MRC_H
#define SYLVAN_BENCHMARKS_SYLVAN_MRC_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define COUNTER_T_MAX UINT64_MAX

typedef size_t counter_t;
typedef _Atomic(counter_t) atomic_counter_t;

/**
 * Atomic array containing counts.
 * Used for tracking the number of references to the unique table nodes.
 * Incrementing has UINT8_MAX as ceiling. Decrementing has 0 as floor.
 * The reason is to use  as little as possible emmoru. It is unlikely a node referenced
 * UINT8_MAX times would be ever removed from the unique table. If you encounter this,
 * change uint16_t to a bigger type.
 */
typedef struct atomic_counters_s
{
    atomic_counter_t *container;
    size_t size;
} atomic_counters_t;

void atomic_counters_init(atomic_counters_t* self, size_t new_size);

void atomic_counters_deinit(atomic_counters_t *self);

void atomic_counters_add(atomic_counters_t* self, size_t idx, int val);

void atomic_counters_set(atomic_counters_t* self, size_t idx, counter_t val);

counter_t atomic_counters_get(const atomic_counters_t* self, size_t idx);


/**
 * Manual Reference Counter (MRC) for the unique table nodes.
 * Used for tracking dead nodes during dynamic variable reordering and
 * performing selective garbage collection.
 */
typedef struct mrc_s
{
    int                     isolated_count;         // number of isolated projection functions
    _Atomic(size_t)         nnodes;                 // number of nodes all nodes in DD
    atomic_counters_t       ref_nodes;              // number of internal references per node (use node unique table index)
    atomic_counters_t       ref_vars;               // number of internal references per variable (use variable order)
    atomic_counters_t       var_nnodes;             // number of nodes per variable (use variable order)
    atomic_bitmap_t         ext_ref_nodes;          // bitmap of nodes with external references (1 -> has some, 0 -> has none)
} mrc_t;

/**
 * init/ deinit functions.
 */
void mrc_init(mrc_t* self, size_t nvars, size_t nnodes, roaring_bitmap_t* node_ids);

void mrc_deinit(mrc_t* self);

/**
 * setters
 */
void mrc_isolated_count_set(mrc_t* self, int val);

void mrc_ref_nodes_set(mrc_t* self, size_t idx, int val);

void mrc_ref_vars_set(mrc_t* self, size_t idx, int val);

void mrc_var_nodes_set(mrc_t* self, size_t idx, int val);

void mrc_nnodes_set(mrc_t* self, int val);

/**
 * adders
 */
void mrc_ref_nodes_add(mrc_t* self, size_t idx, int val);

void mrc_ref_vars_add(mrc_t* self, size_t idx, int val);

void mrc_var_nnodes_add(mrc_t* self, size_t idx, int val);

void mrc_nnodes_add(mrc_t* self, int val);

/**
 * getters
 */
counter_t mrc_isolated_count_get(const mrc_t* self);

counter_t mrc_ext_ref_nodes_get(const mrc_t* self, size_t idx);

counter_t mrc_ref_nodes_get(const mrc_t* self, size_t idx);

counter_t mrc_ref_vars_get(const mrc_t* self, size_t idx);

size_t mrc_var_nnodes_get(const mrc_t* self, size_t idx);

size_t mrc_nnodes_get(const mrc_t* self);

/**
 * @brief Perform selective garbage collection.
 *
 * @details This function performs selective garbage collection on the unique table nodes.
 * For every node with <node>.ref_count == 0 perform delete and decrease ref count of its children.
 * If the children become dead, delete them as well, repeat until no more dead nodes exist.
 */
void mrc_gc(mrc_t* self, roaring_bitmap_t* node_ids);

/**
 * utility functions
 */
int mrc_is_var_isolated(const mrc_t* self, size_t idx);

int mrc_is_node_dead(const mrc_t* self, size_t idx);

void mrc_delete_node(mrc_t *self, size_t index);

MTBDD mrc_make_node(mrc_t *self, BDDVAR var, MTBDD low, MTBDD high, int* created);

MTBDD mrc_make_mapnode(mrc_t *self, BDDVAR var, MTBDD low, MTBDD high, int *created);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_BENCHMARKS_SYLVAN_MRC_H
