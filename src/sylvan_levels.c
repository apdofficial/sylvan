#include <sylvan_int.h>
#include <sylvan_mtbdd_int.h>
#include <sylvan_align.h>
#include <errno.h>      // for errno
#include "sylvan_reorder.h"
#include "sylvan_interact.h"

static size_t levels_size; // size of the arrays in levels_t used to realloc memory

levels_t mtbdd_levels_create()
{
    levels_t dbs = (struct levels_db *) alloc_aligned(sizeof(struct levels_db));
    if (dbs == 0) {
        fprintf(stderr, "mtbdd_levels_create: Unable to allocate memory: %s!\n", strerror(errno));
        exit(1);
    }

    dbs->table = NULL;

    dbs->level_to_order = NULL;
    dbs->order_to_level = NULL;

    dbs->var_count = NULL;
    dbs->var_count_size = 0;

    dbs->ref_count = NULL;
    dbs->ref_count_size = 0;

    dbs->node_ref_count = NULL;
    dbs->node_ref_count_size = 0;

    dbs->bitmap_i = NULL;
    dbs->bitmap_i_size = 0;
    dbs->bitmap_i_nrows = 0;

    dbs->bitmap_p2 = NULL;
    dbs->bitmap_p2_size = 0;

    dbs->bitmap_p3 = NULL;
    dbs->bitmap_p3_size = 0;

    dbs->bitmap_ext = NULL;
    dbs->bitmap_ext_size = 0;

    dbs->isolated_count = 0;
    dbs->reorder_count = 0;
    dbs->reorder_size_threshold = SYLVAN_REORDER_FIRST_REORDER;

    levels_size = 0;
    dbs->count = 0;
    dbs->nodes_count = 0;

    return dbs;
}

counter_t
levels_ref_count_load(levels_t dbs, size_t idx)
{
    if (dbs->ref_count_size == 0) return 0;
    return atomic_load(&dbs->ref_count[idx]);
}

void
levels_ref_count_add(levels_t dbs, size_t idx, int val)
{
    if (dbs->ref_count_size == 0) return;
    counter_t curr = levels_ref_count_load(dbs, idx);
    if (curr == 0 && val < 0) return; // avoid underflow
    if (dbs->ref_count_size == 0 || (curr + val) >= counter_t_max) return;// avoid overflow
    atomic_fetch_add(&dbs->ref_count[idx], val);
}

int
levels_is_isolated(levels_t dbs, size_t idx)
{
    if (dbs->ref_count_size == 0) return 0;
    return levels_ref_count_load(dbs, idx) == 1;
}

counter_t
levels_var_count_load(levels_t dbs, size_t idx)
{
    if (dbs->var_count_size == 0) return 0;
    return atomic_load(&dbs->var_count[idx]);
}

void
levels_var_count_add(levels_t dbs, size_t idx, int val)
{
    if (dbs->var_count_size == 0) return;
    counter_t curr = levels_var_count_load(dbs, idx);
    if (curr == 0 && val < 0)  return; // avoid underflow
    if ((curr + val) >= counter_t_max) return;// avoid overflow
    if (idx >= dbs->var_count_size) return;// avoid out of bounds access
    atomic_fetch_add(&dbs->var_count[idx], val);
}

int
levels_is_node_dead(levels_t dbs, size_t idx)
{
    if (dbs->bitmap_ext_size == 0 || dbs->node_ref_count_size == 0) return 0;
    counter_t int_count = levels_node_ref_count_load(dbs, idx);
    counter_t ext_count = bitmap_atomic_get(dbs->bitmap_ext, idx);
    return int_count == 0 && ext_count == 0;
}

uint64_t
levels_nodes_count_load(levels_t dbs)
{
    return atomic_load_explicit(&dbs->nodes_count, memory_order_relaxed);
}

void
levels_nodes_count_add(levels_t dbs, int val)
{
    if (dbs->node_ref_count_size == 0) return;
    uint64_t curr = atomic_load(&dbs->nodes_count);
    if (curr == 0 && val < 0) return; // avoid underflow
    if ((curr + val) >= atomic_uint_t_max) return;// avoid overflow
    atomic_fetch_add(&dbs->nodes_count, val);
}

void
levels_nodes_count_set(levels_t dbs, int val)
{
    if (dbs->node_ref_count_size == 0) return;
    if (val >= counter_t_max) exit(-1); // overflow, sorry really not allowed
    if (val < 0) exit(-1); // underflow, sorry really not allowed
    atomic_store(&dbs->nodes_count, val);
}

counter_t
levels_node_ref_count_load(levels_t dbs, size_t idx)
{
    if (dbs->node_ref_count_size == 0) return 0;
    return atomic_load(&dbs->node_ref_count[idx]);
}

void
levels_node_ref_count_add(levels_t dbs, size_t idx, int val)
{
    if (dbs->node_ref_count_size == 0) return;
    counter_t curr = levels_node_ref_count_load(dbs, idx);
    if (curr == 0 && val < 0) return; // avoid underflow
    if ((curr + val) >= counter_t_max) return;// avoid overflow
    atomic_fetch_add(&dbs->node_ref_count[idx], val);
}

void
levels_node_ref_count_set(levels_t dbs, size_t idx, int val)
{
    if (dbs->node_ref_count_size == 0) return;
    if (val >= counter_t_max) exit(-1); // overflow, sorry really not allowed
    if (val < 0) exit(-1); // underflow, sorry really not allowed
    atomic_store(&dbs->node_ref_count[idx], val);
}

void
levels_var_count_malloc(size_t new_size)
{
    levels_var_count_free();
    levels->var_count = (atomic_counter_t *) alloc_aligned(sizeof(atomic_counter_t[new_size]));
    if (levels->var_count != NULL) levels->var_count_size = new_size;
    else levels->var_count_size = 0;
}

void
levels_var_count_realloc(size_t new_size)
{
    if (levels->var_count_size == new_size) return;
    levels_var_count_free();
    levels_var_count_malloc(new_size);
}

void
levels_var_count_free()
{
    if (levels->var_count != NULL) free_aligned(levels->var_count, levels->var_count_size);
    levels->var_count_size = 0;
    levels->var_count = NULL;
}

void
levels_ref_count_malloc(size_t new_size)
{
    levels_ref_count_free();
    levels->ref_count = (atomic_counter_t *) alloc_aligned(sizeof(atomic_counter_t[new_size]));
    if (levels->ref_count != NULL) levels->ref_count_size = new_size;
    else levels->ref_count_size = 0;
}

void
levels_ref_count_realloc(size_t new_size)
{
    if (levels->ref_count_size == new_size) return;
    levels_ref_count_free();
    levels_ref_count_malloc(new_size);
}

void
levels_ref_count_free()
{
    if (levels->ref_count != NULL) free_aligned(levels->ref_count, levels->ref_count_size);
    levels->ref_count_size = 0;
    levels->ref_count = NULL;
}

void
levels_node_ref_count_malloc(size_t new_size)
{
    levels_node_ref_count_free();
    levels->node_ref_count = (atomic_counter_t *) alloc_aligned(sizeof(atomic_counter_t[new_size]));
    if (levels->node_ref_count != NULL) levels->node_ref_count_size = new_size;
    else levels->node_ref_count_size = 0;
}

void
levels_node_ref_count_realloc(size_t new_size)
{
    if (levels->node_ref_count_size == new_size) return;
    levels_node_ref_count_free();
    levels_node_ref_count_malloc(new_size);
}

void
levels_node_ref_count_free()
{
    if (levels->node_ref_count != NULL) free_aligned(levels->node_ref_count, levels->node_ref_count_size);
    levels->node_ref_count_size = 0;
    levels->node_ref_count = NULL;
}

void
levels_bitmap_ext_malloc(size_t new_size)
{
    levels_bitmap_ext_free();
    levels->bitmap_ext = (atomic_word_t *) alloc_aligned(new_size);
    if (levels->bitmap_ext != NULL) levels->bitmap_ext_size = new_size;
    else levels->bitmap_ext_size = 0;
}

void
levels_bitmap_ext_realloc(size_t new_size)
{
    if (levels->bitmap_ext_size == new_size) return;
    levels_bitmap_ext_free();
    levels_bitmap_ext_malloc(new_size);
}

void
levels_bitmap_ext_free()
{
    if (levels->bitmap_ext != NULL) free_aligned(levels->bitmap_ext, levels->bitmap_ext_size);
    levels->bitmap_ext_size = 0;
    levels->bitmap_ext = NULL;
}

void
levels_bitmap_p2_malloc(size_t new_size)
{
    levels_bitmap_p2_free();
    levels->bitmap_p2 = (atomic_word_t *) alloc_aligned(new_size);
    if (levels->bitmap_p2 != NULL) levels->bitmap_p2_size = new_size;
    else levels->bitmap_p2_size = 0;
}

void
levels_bitmap_p2_realloc(size_t new_size)
{
    if (levels->bitmap_p2_size == new_size) return;
    levels_bitmap_p2_free();
    levels_bitmap_p2_malloc(new_size);
}

void
levels_bitmap_p2_free()
{
    if (levels->bitmap_p2 != NULL) free_aligned(levels->bitmap_p2, levels->bitmap_p2_size);
    levels->bitmap_p2_size = 0;
    levels->bitmap_p2 = NULL;
}

void
levels_bitmap_p3_malloc(size_t new_size)
{
    levels_bitmap_p2_free();
    levels->bitmap_p3 = (atomic_word_t *) alloc_aligned(new_size);
    if (levels->bitmap_p3 != NULL) levels->bitmap_p3_size = new_size;
    else levels->bitmap_p3_size = 0;
}

void
levels_bitmap_p3_realloc(size_t new_size)
{
    if (levels->bitmap_p3_size == new_size) return;
    levels_bitmap_p2_free();
    levels_bitmap_p2_malloc(new_size);
}

void
levels_bitmap_p3_free()
{
    if (levels->bitmap_p3 != NULL) free_aligned(levels->bitmap_p3, levels->bitmap_p3_size);
    levels->bitmap_p3_size = 0;
    levels->bitmap_p3 = NULL;
}


void
mtbdd_levels_free(levels_t dbs)
{
    mtbdd_resetlevels();
    free_aligned(dbs, sizeof(struct levels_db));
}

size_t
mtbdd_levelscount(void)
{
    return levels->count;
}

MTBDD
mtbdd_newlevel(void)
{
    mtbdd_newlevels(1);
    return levels->table[levels->count - 1];
}

int mtbdd_newlevels(size_t amount)
{
    if (levels->count + amount >= levels_size) {
#if 0
        if (levels_size == 0) levels_size = 1; // start here
        while (levels_count + amount >= levels_size) levels_size *= 2;
#else
        // just round up to the next multiple of 64 value
        // probably better than doubling anyhow...
        levels_size = (levels->count + amount + 63) & (~63LL);
#endif

        levels->table = (atomic_word_t *) realloc(levels->table, sizeof(uint64_t[levels_size]));
        levels->level_to_order = (atomic_half_word_t *) realloc(levels->level_to_order, sizeof(uint32_t[levels_size]));
        levels->order_to_level = (atomic_half_word_t *) realloc(levels->order_to_level, sizeof(uint32_t[levels_size]));

        if (levels->table == 0 ||
            levels->level_to_order == 0 ||
            levels->order_to_level == 0) {
            fprintf(stderr, "mtbdd_newlevels failed to allocate new memory!");
            return 0;
        }
    }
    for (size_t i = 0; i < amount; i++) {
        levels->table[levels->count] = mtbdd_makenode(levels->count, mtbdd_false, mtbdd_true);
        levels->level_to_order[levels->count] = levels->count;
        levels->order_to_level[levels->count] = levels->count;
        levels->count++;
    }
    return 1;
}

int mtbdd_levels_makenode(uint32_t level, MTBDD low, MTBDD high)
{
    if (level >= levels->count) {
        fprintf(stderr, "mtbdd_levels_makenode failed. Out of bounds level.");
        return 0;
    }

    BDDVAR order = levels->level_to_order[level];
    levels->table[order] = mtbdd_makenode(order, low, high);

    return 1;
}

void mtbdd_resetlevels(void)
{
    if (levels_size != 0) {

        if (!levels->table) free(levels->table);
        levels->table = NULL;

        if (!levels->level_to_order) free(levels->level_to_order);
        levels->level_to_order = NULL;

        if (!levels->order_to_level) free(levels->order_to_level);
        levels->order_to_level = NULL;

        levels_var_count_free();
        levels_ref_count_free();
        levels_node_ref_count_free();

        interact_free(levels);

        levels_bitmap_ext_free();
        levels_bitmap_p2_free();

        levels->count = 0;
        levels_size = 0;
        levels->isolated_count = 0;
    }
}

MTBDD mtbdd_ithlevel(uint32_t level)
{
    if (level < levels->count) return levels->table[levels->level_to_order[level]];
    else return mtbdd_invalid;
}

uint32_t mtbdd_order_to_level(BDDVAR var)
{
    if (var < levels->count) return levels->order_to_level[var];
    else return var;
}

BDDVAR mtbdd_level_to_order(uint32_t level)
{
    if (level < levels->count) return levels->level_to_order[level];
    else return level;
}

/**
 * This function is called during garbage collection and
 * marks all managed level BDDs so they are kept.
 */
VOID_TASK_0(mtbdd_gc_mark_managed_refs)
{
    for (size_t i = 0; i < levels->count; i++) {
        llmsset_mark(nodes, MTBDD_STRIPMARK(levels->table[i]));
    }
}

void mtbdd_levels_gc_add_mark_managed_refs(void)
{
    sylvan_gc_add_mark(TASK(mtbdd_gc_mark_managed_refs));
}

/**
 * Sort level counts using gnome sort.
 * @param level
 * @param level_counts
 */
void gnome_sort(int *levels_arr, const _Atomic (size_t) *level_counts)
{
    unsigned int i = 1;
    unsigned int j = 2;
    while (i < levels->count) {
        long p = levels_arr[i - 1] == -1 ? -1 : (long) level_counts[levels->level_to_order[levels_arr[i - 1]]];
        long q = levels_arr[i] == -1 ? -1 : (long) level_counts[levels->level_to_order[levels_arr[i]]];
        if (p < q) {
            int t = levels_arr[i];
            levels_arr[i] = levels_arr[i - 1];
            levels_arr[i - 1] = t;
            if (--i) continue;
        }
        i = j++;
    }
}

VOID_TASK_IMPL_4(sylvan_count_levelnodes, _Atomic (size_t)*, arr, _Atomic (size_t)*, leaf_count, size_t, first, size_t,
                 count)
{
    // divide and conquer (if count above BLOCKSIZE)
    if (count > BLOCKSIZE) {
        size_t split = count / 2;
        SPAWN(sylvan_count_levelnodes, arr, leaf_count, first, split);
        CALL(sylvan_count_levelnodes, arr, leaf_count, first + split, count - split);
        SYNC(sylvan_count_levelnodes);
        return;
    }

    // skip buckets 0 and 1
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }

    size_t tmp[levels->count];
    size_t i;
    for (i = 0; i < levels->count; i++) tmp[i] = 0;

    const size_t end = first + count;
    for (first = llmsset_next(first - 1); first < end; first = llmsset_next(first)) {
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_isleaf(node)) continue; // a leaf
        tmp[mtbddnode_getvariable(node)]++; // update the variable
    }
    for (i = 0; i < levels->count; i++) atomic_fetch_add(&arr[i], tmp[i]);
}

TASK_IMPL_3(size_t, sylvan_count_nodes, BDDVAR, var, size_t, first, size_t, count)
{
    // divide and conquer (if count above BLOCKSIZE)
    if (count > BLOCKSIZE) {
        size_t split = count / 2;
        SPAWN(sylvan_count_nodes, var, first, split);
        size_t a = CALL(sylvan_count_nodes, var, first + split, count - split);
        size_t b = SYNC(sylvan_count_nodes);
        return a + b;
    }

    // skip buckets 0 and 1
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }

    size_t var_count = 0;
    const size_t end = first + count;
    for (first = llmsset_next(first - 1); first < end; first = llmsset_next(first)) {
        mtbddnode_t node = MTBDD_GETNODE(first);
        if (mtbddnode_getvariable(node) != var) continue; // not the right variable
        var_count++;
    }
    return var_count;
}

// set levels below the threshold to -1
void mtbdd_mark_threshold(int *level, const _Atomic (size_t) *level_counts, uint32_t threshold)
{
    for (unsigned int i = 0; i < levels->count; i++) {
        if (level_counts[levels->level_to_order[i]] < threshold) level[i] = -1;
        else level[i] = i;
    }
}

VOID_TASK_IMPL_3(sylvan_init_subtables, atomic_word_t*, bitmap, size_t, first, size_t, count)
{
    if (count > COUNT_NODES_BLOCK_SIZE) {
        size_t split = count / 2;
        SPAWN(sylvan_init_subtables, bitmap, first, split);
        CALL(sylvan_init_subtables, bitmap, first + split, count - split);
        SYNC(sylvan_init_subtables);
        return;
    }

    // skip buckets 0 and 1
    if (first < 2) {
        count = count + first - 2;
        first = 2;
    }

    const size_t end = first + count;
    for (first = llmsset_next(first - 1); first < end; first = llmsset_next(first)) {
        mtbddnode_t node = MTBDD_GETNODE(first);
        BDDVAR var = mtbddnode_getvariable(node);
        if (var < levels->count) {
            bitmap_atomic_set(bitmap, var * levels->count + first);
        }
    }
}