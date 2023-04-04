//
// Created by Andrej Pistek on 03/04/2023.
//

#ifndef SYLVAN_SYLVAN_REORDER_ADJ_H
#define SYLVAN_SYLVAN_REORDER_ADJ_H

#include "sylvan_reorder.h"
#include "sylvan_varswap_adj.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

__attribute__((unused))
void sylvan_init_reorder_adj(void);

__attribute__((unused))
void sylvan_quit_reorder_adj(void);

typedef int (*reorder_termination_cb)();
__attribute__((unused))
void sylvan_set_reorder_terminationcb_adj(reorder_termination_cb callback);

__attribute__((unused))
void sylvan_set_reorder_threshold_adj(size_t threshold);

__attribute__((unused))
void sylvan_set_reorder_maxgrowth_adj(float max_growth);

__attribute__((unused))
void sylvan_set_reorder_maxswap_adj(size_t max_swap);

__attribute__((unused))
void sylvan_set_reorder_maxvar_adj(size_t max_var);

__attribute__((unused))
void sylvan_set_reorder_timelimit_adj(size_t time_limit);

TASK_DECL_5(varswap_res_t, sift_down_adj, BDDVAR, LEVEL, uint64_t*, uint64_t*, LEVEL*);
#define sift_down_adj(var, high_lvl, cur_size, best_size, best_lvl) CALL(sift_down_adj, var, high_lvl, cur_size, best_size, best_lvl)

TASK_DECL_5(varswap_res_t, sift_up_adj, BDDVAR, LEVEL, uint64_t*, uint64_t*, LEVEL*);
#define sift_up_adj(var, low_lvl, cur_size, best_size, best_lvl) CALL(sift_up_adj, var, low_lvl, cur_size, best_size, best_lvl)

TASK_DECL_2(varswap_res_t, sift_to_lvl, BDDVAR, LEVEL);
#define sift_to_lvl(var, target_lvl) CALL(sift_to_lvl, var, target_lvl)

VOID_TASK_DECL_2(sylvan_reorder_adj, LEVEL, LEVEL);
#define sylvan_reorder_adj(low_lvl, high_lvl)  CALL(sylvan_reorder_adj, low_lvl, high_lvl)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_REORDER_ADJ_H
