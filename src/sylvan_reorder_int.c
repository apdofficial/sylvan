#include <roaring.h>
#include <errno.h>
#include <assert.h>
#include <sylvan_int.h>
#include "sylvan_align.h"

static inline int is_db_available()
{
    if (reorder_db == NULL) return 0;
    if (reorder_db->node_ids == NULL) return 0;
    return 1;
}

void atomic_counter_init(atomic_counters_t* self, size_t new_size)
{
    assert(self != NULL);
    atomic_counter_deinit(self);
    self->container = (_Atomic(uint8_t) *) alloc_aligned(sizeof(_Atomic(uint8_t)[new_size]));
    if (self->container == NULL) {
        fprintf(stderr, "atomic_counter_realloc: Unable to allocate memory: %s!\n", strerror(errno));
        exit(1);
    }
    self->size = new_size;
}

void atomic_counter_deinit(atomic_counters_t *self)
{
    if (self->container != NULL && self->size > 0) {
        free_aligned(self->container, self->size);
    }
    self->size = 0;
    self->container = NULL;
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

    return db;
}

void reorder_db_deinit(reorder_db_t* self)
{
    if (is_db_available() == 0) return;
    roaring_bitmap_free((*self)->node_ids);
    atomic_counter_deinit(&(*self)->mrc.ref_nodes);
    atomic_counter_deinit(&(*self)->mrc.ref_vars);
    atomic_counter_deinit(&(*self)->mrc.var_nnodes);
    free(reorder_db);
}


