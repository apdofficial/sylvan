#ifndef SYLVAN_SYLVAN_LEVELS_H
#define SYLVAN_SYLVAN_LEVELS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Count the number of nodes per real variable level in parallel.
 * Results are stored atomically in arr.
 */
VOID_TASK_DECL_3(sylvan_count_nodes, size_t*, size_t, size_t);
#define sylvan_count_nodes(level_counts) RUN(sylvan_count_nodes, level_counts, 0, nodes->table_size);

/**
 * When using dynamic variable reordering, it is strongly recommended to use
 * "levels" instead of working directly with the internal variables.
 *
 * Dynamic variable reordering requires that variables are consecutive.
 * Initially, variables are assigned linearly, starting with 0.
 */

/**
 * Create the next level and return the BDD representing the variable (ithlevel)
 */
MTBDD mtbdd_newlevel(void);

/**
 * Create the next <amount> levels
 */
void mtbdd_newlevels(size_t amount);

/**
 * Reset all levels.
 */
void mtbdd_levels_reset(void);

/**
 * Create or get the BDD representing "if <level> then true else false"
 */
MTBDD mtbdd_ithlevel(uint32_t level);

/**
 * Get the current level of the given internal variable <var>
 */
uint32_t mtbdd_var_to_level(uint32_t var);

/**
 * Get the current internal variable of the given level <level>
 */
uint32_t mtbdd_level_to_var(uint32_t level);

/**
 * Return the level of the given internal node.
 */
uint32_t mtbdd_getlevel(MTBDD node);
#define sylvan_level mtbdd_getlevel

/**
 * Add callback to mark managed references during garbage collection.
 * This is used for the dynamic variable reordering.
 */
void sylvan_gc_add_mark_managed_refs(void);

/**
 * Update var_to_level and level_to_var
 */
void sylvan_var_level_update(uint32_t var);

/**
 * Get the number of created levels
 */
size_t sylvan_get_levels_count(void);

/**
 * Get the variable of the given level
 */
size_t sylvan_get_var(uint32_t level);

/**
 * Clean up the resourced allocated for managing levels
 */
void sylvan_levels_destroy(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_LEVELS_H
