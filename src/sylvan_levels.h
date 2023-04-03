#ifndef SYLVAN_SYLVAN_LEVELS_H
#define SYLVAN_SYLVAN_LEVELS_H

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

/**
 * 4096, because that is not very small, and not very large
 * typical kind of parameter that is open to tweaking, though I don't expect it matters so much
 * too small is bad for the atomic operations, too large is bad for work-stealing
 * with 2^20 - 2^25 nodes table size, this is 256 - 8192 tasks
 */
#define COUNT_NODES_BLOCK_SIZE 4096

VOID_TASK_DECL_3(mtbdd_count_levels, size_t*, size_t, size_t);
/**
 * @brief Count the number of nodes per real variable level in parallel.
 * @details Results are stored atomically in arr. To make this somewhat scalable, we use a
 * standard binary reduction pattern with local arrays...
 * Fortunately, we only do this once per call to dynamic variable reordering.
 * \param level_counts array into which the result is stored
 */
#define mtbdd_count_levels(level_counts) RUN(mtbdd_count_levels, level_counts, 0, nodes->table_size)

VOID_TASK_DECL_2(mtbdd_count_sort_levels, int*, size_t);
/**
 * @brief Count and sort all variable levels (parallel...)
 *
 * \details Order all the variables using gnome sort according to the number of entries in each level.
 *
 * \param level_counts - array of size mtbdd_levels_size()
 * \param threshold - only count levels which have at least threshold number of variables.
 * If level is skipped assign it -1.
 *
 */
#define mtbdd_count_sort_levels(levels, threshold) RUN(mtbdd_count_sort_levels, levels, threshold)

/**
 * @brief Create the next <amount> levels
 * @details The BDDs representing managed levels are always kept during garbage collection. Not currently thread-safe.
 * \param amount number of levels to create
 */
 __attribute__((unused))
void mtbdd_newlevels(size_t amount);

/**
 * \brief  Reset all levels.
 */
__attribute__((unused))
void mtbdd_levels_reset(void);

__attribute__((unused))
int mtbdd_getorderlock(uint32_t level);

__attribute__((unused))
void mtbdd_setorderlock(uint32_t level, int is_locked);

/**
 * \brief  Get the BDD node representing "if level then true else false"
 * \details  Order a node does not change after a swap, meaning it is in the same level,
 * however, after a swap they can point to a different variable
 * \param level for which the BDD needs to be returned
 */
MTBDD mtbdd_ithlevel(uint32_t level);

/**
 * \brief  Get the current level of the given internal variable <var>
 * \param var for which the level needs to be returned
 */
uint32_t mtbdd_var_to_level(uint32_t var);

/**
 * @brief Get the current internal variable of the given level
 * \param level for which the variable needs be returned
 */
uint32_t mtbdd_level_to_var(uint32_t level);

/**
 * \brief  Get the number of created levels
 */
size_t mtbdd_levels_size(void);

/**
 * \brief  Return the level of the given internal node.
 * \param node for which the level needs to be returned
 */
uint32_t mtbdd_node_to_level(MTBDD node);

/**
 * \brief  Add callback to mark managed references during garbage collection.
 * \details This is used for the dynamic variable reordering.
 */
void mtbdd_levels_gc_add_mark_managed_refs(void);

/**
 * \brief Swap the levels of two variables var and var+1
 * \details This is used for the dynamic variable reordering.
 * <ul>
 * <li>swap the level_to_var of var and var+1
 * <li>swap the var_to_level of level var and level var+1
 * </ul>
 * \param var variable to be swapped with var+1
 */
void mtbdd_varswap(uint32_t var);

void mtbdd_varswap_adj(uint32_t x, uint32_t y);

__attribute__((unused))
size_t mtbdd_nextlow(uint32_t var);

__attribute__((unused))
size_t mtbdd_nexthigh(uint32_t var);

/**
 * \brief Clean up the resourced allocated for managing levels
 */
void sylvan_levels_destroy(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_LEVELS_H
