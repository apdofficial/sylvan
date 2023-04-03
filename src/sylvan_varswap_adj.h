//
// Created by Andrej Pistek on 03/04/2023.
//

#ifndef SYLVAN_SYLVAN_VARSWAP_ADJ_H
#define SYLVAN_SYLVAN_VARSWAP_ADJ_H


TASK_DECL_2(varswap_res_t, sylvan_varswap_adj, uint32_t, uint32_t);
/**
  @brief Swaps two adjacent variables.

  @details Precondition is that x &lt; y.

*/
#define sylvan_varswap_adj(x, y) CALL(sylvan_varswap_adj, x, y)

#endif //SYLVAN_SYLVAN_VARSWAP_ADJ_H
