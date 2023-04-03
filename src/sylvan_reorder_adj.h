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

TASK_DECL_5(varswap_res_t, sift_down_adj, size_t, size_t, size_t*, size_t*, size_t*);
#define sift_down_adj(var, high_lvl, cur_size, best_size, best_lvl) RUN(sift_down_adj, var, high_lvl, cur_size, best_size, best_lvl)

TASK_DECL_5(varswap_res_t, sift_up_adj, size_t, size_t, size_t*, size_t*, size_t*);
#define sift_up_adj(var, low_lvl, cur_size, best_size, best_lvl) RUN(sift_up_adj, var, low_lvl, cur_size, best_size, best_lvl)

TASK_DECL_2(varswap_res_t, sift_to_lvl, size_t, size_t);
#define sift_to_lvl(var, lvl) RUN(sift_to_lvl, var, lvl)

VOID_TASK_DECL_2(sylvan_reorder_adj, uint32_t, uint32_t);
#define sylvan_reorder_adj(low_lvl, high_lvl)  CALL(sylvan_reorder_adj, low_lvl, high_lvl)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_SYLVAN_REORDER_ADJ_H
