#include <roaring.h>
#include <sylvan_bitmap.h>
#include <sylvan_mrc.h>

typedef roaring_uint32_iterator_t nodes_iterator_t;

typedef struct reorder_db_s
{
    roaring_bitmap_t*       node_ids;               // compressed roaring bitmap holding node indices of the unique table nodes
    mrc_t                   mrc;                    // reference counters for the unique table nodes
} *reorder_db_t;

reorder_db_t reorder_db_init();

void reorder_db_ddeinit();