#include <errno.h>
#include <sylvan_int.h>

static inline int is_db_available()
{
    if (reorder_db == NULL) return 0;
    if (reorder_db->node_ids == NULL) return 0;
    return 1;
}

reorder_db_t reorder_db_init()
{
    reorder_db_t db = (reorder_db_t) malloc(sizeof(struct reorder_db_s));
    if (db == NULL) {
        fprintf(stderr, "reorder_db_init: Unable to allocate memory: %s!\n", strerror(errno));
        exit(1);
    }
    db->node_ids = roaring_bitmap_create();
    if (db->node_ids == NULL) {
        fprintf(stderr, "reorder_db_init: Unable to allocate memory: %s!\n", strerror(errno));
        exit(1);
    }
    db->mrc = (mrc_t) {
        .ref_nodes = (atomic_counters_t) {
            .container = NULL,
        },
        .ref_vars = (atomic_counters_t) {
            .container = NULL,
        },
        .var_nnodes = (atomic_counters_t) {
            .container = NULL,
        }
    };

    db->size_threshold = SYLVAN_REORDER_FIRST_REORDER;
    db->call_count = 0;

    return db;
}

void reorder_db_deinit(reorder_db_t self)
{
    if (is_db_available() == 0) return;
    roaring_bitmap_free(self->node_ids);
    mrc_deinit(&self->mrc);
    free(reorder_db);
}