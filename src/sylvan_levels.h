#ifndef SYLVAN_SYLVAN_LEVELS_H
#define SYLVAN_SYLVAN_LEVELS_H

#include "sylvan_bitmap.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define COUNT_NODES_BLOCK_SIZE 4096


/**
 * When using dynamic variable reordering, it is strongly recommended to use
 * "levels" instead of working directly with the internal variables.
 *
 * Dynamic variable reordering requires that variables are consecutive.
 * Initially, variables are assigned linearly, starting with 0.
 */
typedef struct levels_db {
    _Atomic(uint64_t)*  table;                   // array holding the 1-node BDD for each level
    size_t              count;                   // number of created levels
    _Atomic(uint32_t)*  level_to_order;          // current level wise var permutation (level to variable label)
    _Atomic(uint32_t)*  order_to_level;          // current variable wise level permutation (variable label to level)
    _Atomic(uint32_t)*  var_count;               // number of nodes per variable
    _Atomic(uint32_t)*  ref_count;               // number of internal references per variable
    _Atomic(size_t)     isolated_count;          // number of isolated projection functions
    atomic_word_t*      bitmap_i;                // bitmap used for storing the square variable interaction matrix
    size_t              bitmap_i_nrows;          // number of rows and columns
    size_t              bitmap_i_size;           // size of the bitmaps
    size_t              reorder_size_threshold;  // reorder if this size is reached
    size_t              reorder_count;           // number of reordering calls
} *levels_t;

typedef struct bounds_state
{
    int                 isolated;               // flag to indicate if the current <var> is isolated projection function (<var>.ref.count <= 1)
    int                 bound;                  // lower/ upper bound on the number of nodes
    int                 limit;                  // limit on the number of nodes
} bounds_state_t;

/**
 * @brief Check if a variable is isolated. ( isolated => var.ref.count == 1)
 */
#define levels_is_isolated(lvl, var) (levels_ref_count_load(lvl, var) == 1)
#define levels_isolated_count_load(lvl) atomic_load_explicit(&lvl->isolated_count, memory_order_relaxed)
#define levels_isolated_count_incr(lvl) atomic_fetch_add(&lvl->isolated_count, 1)
#define levels_isolated_count_add(lvl, val) atomic_fetch_add(&lvl->isolated_count, val)
#define levels_isolated_count_set(lvl, new_v) size_t old_v__ = levels_isolated_count_load(lvl); \
                                                atomic_compare_exchange_strong(&lvl->isolated_count, &old_v__, new_v)

#define levels_var_count_add(lvl, val) atomic_fetch_add(&lvl->var_count[var], val)
#define levels_var_count_load(lvl, var) atomic_load(&lvl->var_count[var])
#define levels_var_count_incr(lvl, var) atomic_fetch_add(&lvl->var_count[var], 1)
#define levels_var_count_decr(lvl, var) atomic_fetch_add(&lvl->var_count[var], -1)

#define levels_ref_count_add(lvl, val) atomic_fetch_add(&lvl->ref_count[index], val)
#define levels_ref_count_load(lvl, index) atomic_load(&lvl->ref_count[index])
#define levels_ref_count_incr(lvl, index) atomic_fetch_add(&lvl->ref_count[index], 1)
#define levels_ref_count_decr(lvl, index) atomic_fetch_add(&lvl->ref_count[index], -1)


/**
 * Index to first node in phase 2 mark bitmap
 */
#define bitmap_p2_first() bitmap_atomic_first(levels->bitmap_p2, levels->bitmap_p2_size)

/**
 * Index of the next node relative to the provided index in th phase 2 bitmap.
 */
#define bitmap_p2_next(index) bitmap_atomic_next(levels->bitmap_p2, levels->bitmap_p2_size, index)

/**
 * @brief Create a new levels_t object
 */
levels_t mtbdd_levels_create();

/**
 * @brief Free a levels_t object
 */
void mtbdd_levels_free(levels_t dbs);

VOID_TASK_DECL_4(sylvan_count_levelnodes, _Atomic(size_t)*, _Atomic(size_t)*, size_t, size_t);
/**
 * @brief Count the number of nodes per real variable level in parallel.
 * @details Results are stored atomically in arr. To make this somewhat scalable, we use a
 * standard binary reduction pattern with local arrays...
 * Fortunately, we only do this once per call to dynamic variable reordering.
 * \param level_counts array into which the result is stored
 */
#define sylvan_count_levelnodes(level_counts, leaf_count) RUN(sylvan_count_levelnodes, level_counts, leaf_count, 0, nodes->table_size)

TASK_DECL_3(size_t, sylvan_count_nodes, BDDVAR, size_t, size_t);
/**
 * @brief Count the number of nodes for a given variable label.
 */
#define sylvan_count_nodes(var) RUN(sylvan_count_levelnodes, level_counts, 0, nodes->table_size)

VOID_TASK_DECL_3(sylvan_init_subtables, atomic_word_t*, size_t, size_t);
#define sylvan_init_subtables(bitmap_t) RUN(sylvan_init_subtables, bitmap_t, 0, nodes->table_size)

/**
 * @brief Get the number of levels
 */
size_t mtbdd_levelscount(void);

/**
 * @brief Create the next level and return the BDD representing the variable (ithlevel)
 * @details The BDDs representing managed levels are always kept during garbage collection.
 * NOTE: not currently thread-safe.
 */
MTBDD mtbdd_newlevel(void);

/**
 * @brief Create the next <amount> levels
 * @details The BDDs representing managed levels are always kept during garbage collection. Not currently thread-safe.
 * \param amount number of levels to create
 */
int mtbdd_newlevels(size_t amount);

/**
 * \brief  Reset all levels.
 */
void mtbdd_resetlevels(void);

/**
 * \brief  Get the BDD node representing "if level then true else false"
 * \details  Order a node does not change after a swap, meaning it is in the same level,
 * however, after a swap they can point to a different variable
 * \param level for which the BDD needs to be returned
 */
MTBDD mtbdd_ithlevel(uint32_t level);

/**
 * @brief  Get the level of the given variable
 */
uint32_t mtbdd_order_to_level(BDDVAR var);

/**
 * @brief  Get the variable of the given level
 */
BDDVAR mtbdd_level_to_order(uint32_t level);

/**
 * \brief  Add callback to mark managed references during garbage collection.
 * \details This is used for the dynamic variable reordering.
 */
void mtbdd_levels_gc_add_mark_managed_refs(void);

/**
 * @brief  Mark each level_count -1 which is below the threshold.
 */
void mtbdd_mark_threshold(int* level, const _Atomic(size_t)* level_counts, uint32_t threshold);

/**
 * @brief  Sort the levels in descending order according to the number of nodes.
 */
void gnome_sort(int *levels, const _Atomic(size_t) *level_counts);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_LEVELS_H
