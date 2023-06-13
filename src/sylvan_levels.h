#ifndef SYLVAN_SYLVAN_LEVELS_H
#define SYLVAN_SYLVAN_LEVELS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * When using dynamic variable reordering, it is strongly recommended to use
 * "levels" instead of working directly with the internal variables.
 *
 * Dynamic variable reordering requires that variables are consecutive.
 * Initially, variables are assigned linearly, starting with 0.
 */
typedef struct levels_db {
    _Atomic(uint64_t)*      table;                   // array holding the 1-node BDD for each level
    size_t                  count;                   // number of created levels
    _Atomic(uint32_t)*      level_to_order;          // current level wise var permutation (level to variable label)
    _Atomic(uint32_t)*      order_to_level;          // current variable wise level permutation (variable label to level)
    _Atomic(uint64_t)*      bitmap_p2;               // bitmap used to store reordering phase 2 mark
    size_t                  bitmap_p2_size;          // size of bitmap_p2
} *levels_t;

/**
 * Efficient phase 2 mark iterator implemented using bitmaps and using GCC built-in bit counting functions. (thread-safe)
 *
 * Returns node index to the unique table.
 */
#define levels_nindex npos

#define levels_p2_next(idx) bitmap_atomic_next(levels->bitmap_p2, levels->bitmap_p2_size, idx)
#define levels_p2_set(idx) bitmap_atomic_set(levels->bitmap_p2, idx)
#define levels_p2_clear_all() clear_aligned(levels->bitmap_p2, levels->bitmap_p2_size)

/**
 * @brief Create a new levels_t object
 */
levels_t mtbdd_levels_create();

/**
 * @brief Free a levels_t object
 */
void levels_free(levels_t dbs);

void levels_bitmap_p2_malloc(size_t new_size);

void levels_bitmap_p2_realloc(size_t new_size);

void levels_bitmap_p2_free();

/**
 * @brief Get the number of levels
 */
size_t mtbdd_levelscount(void);

/**
 * @brief Create the next level and return the BDD representing the variable (ithlevel)
 * @details The BDDs representing managed levels are always kept during garbage collection.
 * NOTE: not currently thread-safe.
 */
uint64_t mtbdd_newlevel(void);

/**
 * @brief Create the next <amount> levels
 * @details The BDDs representing managed levels are always kept during garbage collection. Not currently thread-safe.
 * \param amount number of levels to create
 */
int mtbdd_newlevels(size_t amount);

/**
 * @brief Insert a node at given level with given low and high nodes
 */
int mtbdd_levels_makenode(uint32_t level, uint64_t low, uint64_t high);

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
uint64_t mtbdd_ithlevel(uint32_t level);

/**
 * @brief  Get the level of the given variable
 */
uint32_t mtbdd_order_to_level(uint32_t var);

/**
 * @brief  Get the variable of the given level
 */
uint32_t mtbdd_level_to_order(uint32_t level);

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
