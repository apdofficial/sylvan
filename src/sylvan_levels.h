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
    atomic_word_t*          table;                   // array holding the 1-node BDD for each level
    size_t                  count;                   // number of created levels
    atomic_half_word_t*     level_to_order;          // current level wise var permutation (level to variable label)
    atomic_half_word_t*     order_to_level;          // current variable wise level permutation (variable label to level)
    atomic_half_word_t*     var_count;               // number of nodes per variable (it expects order wise variable index) needs to be initialized before every use
    atomic_half_word_t*     ref_count;               // number of internal references per variable (it expects order wise variable index)
    int                     isolated_count;          // number of isolated projection functions
    atomic_word_t*          bitmap_i;                // bitmap used for storing the square variable interaction matrix (use variable order)
    size_t                  bitmap_i_nrows;          // number of rows/ columns
    size_t                  bitmap_i_size;           // size of bitmap_i
    atomic_word_t*          bitmap_p2;               // bitmap used to store reordering phase 2 mark
    size_t                  bitmap_p2_size;          // size of bitmap_p2
    size_t                  reorder_size_threshold;  // reorder if this size is reached
    size_t                  reorder_count;           // number of reordering calls
} *levels_t;

/**
 * Efficient phase 2 mark iterator implemented using bitmaps and using GCC built-in bit counting functions. (thread-safe)
 *
 * Returns node index to the unique table.
 */
#define levels_nindex npos

#define levels_p2_first() bitmap_atomic_first(levels->bitmap_p2, levels->bitmap_p2_size)
#define levels_p2_next(idx) bitmap_atomic_next(levels->bitmap_p2, levels->bitmap_p2_size, idx)
#define levels_p2_set(idx) bitmap_atomic_set(levels->bitmap_p2, idx)
#define levels_p2_clear(idx) bitmap_atomic_clear(levels->bitmap_p2, idx)
#define levels_p2_is_marked(idx) bitmap_atomic_get(levels->bitmap_p2, idx)
#define levels_p2_clear_all() clear_aligned(levels->bitmap_p2, levels->bitmap_p2_size)

#define levels_is_isolated(idx) (levels_ref_count_load(idx) <= 1)
#define levels_ref_count_load(idx) atomic_load_explicit(&levels->ref_count[idx], memory_order_relaxed)
#define levels_ref_count_dec(idx) atomic_fetch_add_explicit(&levels->ref_count[idx], -1, memory_order_relaxed)
#define levels_ref_count_inc(idx) atomic_fetch_add_explicit(&levels->ref_count[idx], 1, memory_order_relaxed)

#define levels_var_count_dec(idx) atomic_fetch_add_explicit(&levels->var_count[idx], -1, memory_order_relaxed)
#define levels_var_count_inc(idx) atomic_fetch_add_explicit(&levels->var_count[idx], 1, memory_order_relaxed)
#define levels_var_count_load(idx) atomic_load_explicit(&levels->var_count[idx],memory_order_relaxed)

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
#define sylvan_count_levelnodes(level_counts, leaf_count) CALL(sylvan_count_levelnodes, level_counts, leaf_count, 0, nodes->table_size)

TASK_DECL_3(size_t, sylvan_count_nodes, BDDVAR, size_t, size_t);
/**
 * @brief Count the number of nodes for a given variable label.
 */
#define sylvan_count_nodes(var) CALL(sylvan_count_nodes, level_counts, 0, nodes->table_size)

VOID_TASK_DECL_3(sylvan_init_subtables, atomic_word_t*, size_t, size_t);
#define sylvan_init_subtables(bitmap_t) CALL(sylvan_init_subtables, bitmap_t, 0, nodes->table_size)

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
 * @brief Insert a node at given level with given low and high nodes
 */
int mtbdd_levels_makenode(uint32_t level, MTBDD low, MTBDD high);

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
