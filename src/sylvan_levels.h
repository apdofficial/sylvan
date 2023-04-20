#ifndef SYLVAN_SYLVAN_LEVELS_H
#define SYLVAN_SYLVAN_LEVELS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#include "sylvan_bitset.h"

/**
 * When using dynamic variable reordering, it is strongly recommended to use
 * "levels" instead of working directly with the internal variables.
 *
 * Dynamic variable reordering requires that variables are consecutive.
 * Initially, variables are assigned linearly, starting with 0.
 */
typedef struct levels_db {
    _Atomic(uint32_t)* table;                   // array holding the 1-node BDD for each level
    size_t count;                               // number of created levels
    _Atomic(uint32_t)* level_to_order;          // current level wise var permutation (level to variable label)
    _Atomic(uint32_t)* order_to_level;          // current variable wise level permutation (variable label to level)
} *levels_t;

/**
 * 4096, because that is not very small, and not very large
 * typical kind of parameter that is open to tweaking, though I don't expect it matters so much
 * too small is bad for the atomic operations, too large is bad for work-stealing
 * with 2^20 - 2^25 nodes table size, this is 256 - 8192 tasks
 */
#define COUNT_NODES_BLOCK_SIZE 4096

/**
 * @brief Create a new levels_t object
 */
levels_t mtbdd_levels_create();

/**
 * @brief Free a levels_t object
 */
void mtbdd_levels_free(levels_t dbs);

VOID_TASK_DECL_3(sylvan_count_levelnodes, _Atomic(size_t)*, size_t, size_t);
/**
 * @brief Count the number of nodes per real variable level in parallel.
 * @details Results are stored atomically in arr. To make this somewhat scalable, we use a
 * standard binary reduction pattern with local arrays...
 * Fortunately, we only do this once per call to dynamic variable reordering.
 * \param level_counts array into which the result is stored
 */
#define sylvan_count_levelnodes(level_counts) RUN(sylvan_count_levelnodes, level_counts, 0, nodes->table_size)

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
