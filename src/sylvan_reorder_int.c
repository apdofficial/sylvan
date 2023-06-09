#include <roaring.h>
#include <errno.h>
#include <assert.h>
#include <sylvan_int.h>

static inline int is_db_available()
{
    if (reorder_db == NULL) return 0;
    if (reorder_db->node_ids == NULL) return 0;
    return 1;
}

reorder_db_t reorder_db_create()
{
    reorder_db_t db = (reorder_db_t) malloc(sizeof(struct reorder_db_s));
    if (db == NULL) {
        fprintf(stderr, "reorder_db_create: Unable to allocate memory: %s!\n", strerror(errno));
        exit(1);
    }
    db->node_ids = roaring_bitmap_create();
    return db;
}

void reorder_db_destroy()
{
    if (!is_db_available()) return;
    roaring_bitmap_free(reorder_db->node_ids);
}