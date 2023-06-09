#include <roaring.h>

typedef roaring_uint32_iterator_t nodes_iterator_t;

typedef struct reorder_db_s
{
    roaring_bitmap_t *node_ids; // compressed roaring bitmap holding node indices of the unique table nodes
} *reorder_db_t;

reorder_db_t reorder_db_create();

void reorder_db_destroy();