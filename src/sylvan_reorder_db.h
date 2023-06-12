#ifndef SYLVAN_VAR_REORDER_DB_H
#define SYLVAN_VAR_REORDER_DB_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct reorder_db_s
{
    roaring_bitmap_t*       node_ids;                   // compressed roaring bitmap holding node indices of the unique table nodes
    mrc_t                   mrc;                        // reference counters used for the unique table nodes
    size_t                  size_threshold;             // reorder if this size is reached
    size_t                  call_count;                 // number of reordering calls
} *reorder_db_t;

reorder_db_t reorder_db_init();

void reorder_db_deinit(reorder_db_t self);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif //SYLVAN_VAR_REORDER_DB_H