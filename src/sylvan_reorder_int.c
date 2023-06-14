#include <sylvan_int.h>

#include <errno.h>
#include <sys/time.h>

#define STATS 0 // useful information w.r.t. dynamic reordering for debugging
#define INFO 1  // useful information w.r.t. dynamic reordering

static inline int is_db_available()
{
    if (reorder_db == NULL) return 0;
    if (reorder_db->node_ids == NULL) return 0;
    return 1;
}

static double wctime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

static inline double wctime_sec_elapsed(double t_start)
{
    return wctime() - t_start;
}

static inline double wctime_ms_elapsed(double start)
{
    return wctime_sec_elapsed(start) * 1000;
}

reorder_db_t reorder_db_init()
{
    if (reorder_db != NULL && reorder_db->is_initialised) return reorder_db;

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
            .isolated_count = 0,
            .nnodes = 0,
            .ref_nodes = (atomic_counters_t) {
                    .container = NULL,
                    .size = 0,
            },
            .ref_vars = (atomic_counters_t) {
                    .container = NULL,
                    .size = 0,
            },
            .var_nnodes = (atomic_counters_t) {
                    .container = NULL,
                    .size = 0,
            },
            .ext_ref_nodes = (atomic_bitmap_t) {
                    .container = NULL,
                    .size = 0,
            },
    };

    db->call_count = 0;

    db->matrix = (interact_t) {
            .container = NULL,
            .size = 0,
    };

    db->levels = (levels_t) {
            .table = NULL,
            .count = 0,
            .level_to_order = NULL,
            .order_to_level = NULL,
    };


    db->is_initialised = 1;
    db->config = (reorder_config_t) {};
    reorder_set_default_config(&db->config);

    sylvan_register_quit(&sylvan_quit_reorder);
    levels_gc_add_mark_managed_refs();

    return db;
}

void reorder_db_deinit(reorder_db_t self)
{
    if (!self->is_initialised) return;
    self->is_initialised = 0;
    if (is_db_available() == 0) return;
    roaring_bitmap_free(self->node_ids);
    mrc_deinit(&self->mrc);
    interact_deinit(&self->matrix);
    free(reorder_db);
}

static re_hook_entry_t prere_list;
static re_hook_entry_t postre_list;
static re_hook_entry_t progre_list;
static re_term_entry_t termre_list;

void sylvan_re_hook_prere(re_hook_cb callback)
{
    re_hook_entry_t e = (re_hook_entry_t) malloc(sizeof(struct re_hook_entry));
    e->cb = callback;
    e->next = prere_list;
    prere_list = e;
}

void sylvan_re_hook_postre(re_hook_cb callback)
{
    re_hook_entry_t e = (re_hook_entry_t) malloc(sizeof(struct re_hook_entry));
    e->cb = callback;
    e->next = postre_list;
    postre_list = e;
}

void sylvan_re_hook_progre(re_hook_cb callback)
{
    re_hook_entry_t e = (re_hook_entry_t) malloc(sizeof(struct re_hook_entry));
    e->cb = callback;
    e->next = progre_list;
    progre_list = e;
}

void sylvan_re_hook_termre(re_term_cb callback)
{
    re_term_entry_t e = (re_term_entry_t) malloc(sizeof(struct re_term_entry));
    e->cb = callback;
    e->next = termre_list;
    termre_list = e;
}

VOID_TASK_IMPL_0(reorder_db_call_progress_hooks)
{
    for (re_hook_entry_t e = progre_list; e != NULL; e = e->next) {
        WRAP(e->cb);
    }
}

inline uint64_t get_nodes_count()
{
    return llmsset_count_marked(nodes) + 2;
#if SYLVAN_USE_LINEAR_PROBING
    return llmsset_count_marked(nodes) + 2;
#else
    return mrc_nnodes_get(&reorder_db->mrc) + 2;
#endif
}

TASK_IMPL_1(reorder_result_t, sylvan_siftdown, sifting_state_t *, s_state)
{
    if (!reorder_db->is_initialised) return SYLVAN_REORDER_NOT_INITIALISED;
#if STATS
    printf("\n");
#endif
    reorder_result_t res;
    int R;  // upper bound on node decrease
    int limitSize;
    BDDVAR xIndex;
    BDDVAR yIndex;
    BDDVAR x;
    BDDVAR y;

    s_state->size = (int) get_nodes_count();
    xIndex = reorder_db->levels.level_to_order[s_state->pos];

    limitSize = s_state->size = s_state->size - reorder_db->mrc.isolated_count;
    R = 0;

    // Let <x> be the variable at level <pos>. (s_state->pos)
    // Let <y> be the variable at level <pos+1>. (s_state->pos+1)
    // Let Ni be the number of nodes at level i.
    // Let n be the number of levels. (levels->count)

    // Then the size of DD can not be reduced below:
    // LB(DN) = Nj + ∑ Ni | 0<i<pos

    // The part of the DD above <x> will not change.
    // The part of the DD below <x> that does not interact with <x> will not change.
    // The rest may vanish in the best case, except for
    // the nodes at level <high>, which will not vanish, regardless.
#if STATS
    printf("R: %d, limitSize: %d, all_nodes: %d, all_isolated: %d\n",
           R, limitSize, s_state->size, reorder_db->mrc.isolated_count);
#endif
    // Initialize the upper bound
    for (y = s_state->high; y > s_state->pos; y--) {
        yIndex = reorder_db->levels.level_to_order[y];
        if (interact_test(&reorder_db->matrix, xIndex, yIndex)) {
            R += (int) mrc_var_nnodes_get(&reorder_db->mrc, y) - mrc_is_var_isolated(&reorder_db->mrc, yIndex);
#if STATS
            printf("R: %d, xindex: %d, yindex: %d\n", R, xIndex, yIndex);
#endif
        } else {
#if STATS
            printf("R: %d, xindex: %d, yindex: %d (no interaction)\n", R, xIndex, yIndex);
#endif
        }
    }

#if STATS
    x = s_state->pos;
    y = s_state->pos + 1;
    printf("sift down pos: x: %d (R: %d, size: %d, limitSize: %d) (xNodes: %zu, yNodes: %zu)\n",
           x, R, s_state->size, limitSize,
           mrc_var_nnodes_get(&reorder_db->mrc, x),
           mrc_var_nnodes_get(&reorder_db->mrc, y)
    );
#endif
    for (; s_state->pos < s_state->high && s_state->size - R < limitSize; ++s_state->pos) {
        x = s_state->pos;
        y = s_state->pos + 1;
        //  Update the upper bound on node decrease
        yIndex = reorder_db->levels.level_to_order[y];
        if (interact_test(&reorder_db->matrix, xIndex, yIndex)) {
            R -= (int) mrc_var_nnodes_get(&reorder_db->mrc, y) - mrc_is_var_isolated(&reorder_db->mrc, yIndex);
        }
        res = sylvan_varswap(x);
        s_state->size = (int) get_nodes_count();
        if (!sylvan_reorder_issuccess(res)) return res;
        reorder_db->config.varswap_count++;

        // check the max allowed size growth
        if ((double) (s_state->size) > (double) s_state->best_size * reorder_db->config.max_growth) {
            ++s_state->pos;
            break;
        }

        // update best position
        if (s_state->size <= s_state->best_size) {
            s_state->best_size = s_state->size;
            s_state->best_pos = s_state->pos;
        }

        if (s_state->size < limitSize) limitSize = s_state->size;
#if STATS
        printf("sift down pos: x: %d (R: %d, size: %d, limitSize: %d) (xNodes: %zu, yNodes: %zu)\n",
               x, R, s_state->size, limitSize,
               mrc_var_nnodes_get(&reorder_db->mrc, x),
               mrc_var_nnodes_get(&reorder_db->mrc, y)
        );
//        printf("\n");
//        for (size_t i = 0; i < levels->count; i++) {
//            printf("level %zu (%d) \t has %u nodes\n", i, levels->order_to_level[i], levels_var_count_load(levels, i));
//        }
//        printf("\n");
#endif
        if (should_terminate_sifting(&reorder_db->config)) break;
    }

    if (s_state->size <= s_state->best_size) {
        s_state->best_size = s_state->size;
        s_state->best_pos = s_state->pos;
    }

#if STATS
    printf("\n");
    for (size_t i = 0; i < levels_count_get(&reorder_db->levels); i++) {
        printf("level %zu (%d) \t has %zu nodes\n", i, reorder_db->levels.order_to_level[i], mrc_var_nnodes_get(&reorder_db->mrc, i));
    }
    printf("\n");
#endif
    return SYLVAN_REORDER_SUCCESS;
}

TASK_IMPL_1(reorder_result_t, sylvan_siftup, sifting_state_t *, s_state)
{
    if (!reorder_db->is_initialised) return SYLVAN_REORDER_NOT_INITIALISED;
#if STATS
    printf("\n");
#endif

    reorder_result_t res;
    int L;  // lower bound on DD size
    int limitSize;
    BDDVAR xIndex;
    BDDVAR yIndex;
    BDDVAR x;
    BDDVAR y;

    s_state->size = (int) get_nodes_count();
    yIndex = reorder_db->levels.level_to_order[s_state->pos];

    // Let <x> be the variable at level <pos-1>. (s_state->pos-1)
    // Let <y> be the variable at level <pos>. (s_state->pos)
    // Let Ni be the number of nodes at level i.
    // Let n be the number of levels. (levels->count)

    // Then the size of DD can not be reduced below:
    // LB(UP) = N0 + ∑ Ni | i<pos<n

    // The part of the DD below <y> will not change.
    // The part of the DD above <y> that does not interact with <y> will not change.
    // The rest may vanish in the best case, except for
    // the nodes at level <low>, which will not vanish, regardless.

    limitSize = L = (int) (s_state->size - reorder_db->mrc.isolated_count);
#if STATS
    printf("L: %d, limitSize: %d, all_nodes: %d, all_isolated: %d\n",
           L, limitSize, s_state->size, reorder_db->mrc.isolated_count);
#endif
    for (x = s_state->low + 1; x < s_state->pos; x++) {
        xIndex = reorder_db->levels.level_to_order[x];
        if (interact_test(&reorder_db->matrix, xIndex, yIndex)) {
            L -= (int) mrc_var_nnodes_get(&reorder_db->mrc, x) - mrc_is_var_isolated(&reorder_db->mrc, xIndex);
#if STATS
            printf("L: %d, xindex: %d, yindex: %d\n", L, xIndex, yIndex);
#endif
        } else {
#if STATS
            printf("L: %d, xindex: %d, yindex: %d(no interaction)\n", L, xIndex, yIndex);
#endif
        }
    }

    y = s_state->pos;

    L -= (int) mrc_var_nnodes_get(&reorder_db->mrc, y) - mrc_is_var_isolated(&reorder_db->mrc, yIndex);
#if STATS
    x = s_state->pos - 1;
    printf("sift up pos: x: %d (L: %d, size: %d, limitSize: %d) (xNodes: %zu, yNodes: %zu)\n",
           x, L, s_state->size, limitSize,
           mrc_var_nnodes_get(&reorder_db->mrc, x),
           mrc_var_nnodes_get(&reorder_db->mrc, y)
    );
#endif
    for (; s_state->pos > s_state->low && L <= limitSize; --s_state->pos) {
        x = s_state->pos - 1;
        y = s_state->pos;
        xIndex = reorder_db->levels.level_to_order[x];

        res = sylvan_varswap(x);
        if (!sylvan_reorder_issuccess(res)) return res;

        s_state->size = (int) get_nodes_count();
        reorder_db->config.varswap_count++;

        // check the max allowed size growth
        if ((double) (s_state->size) > (double) s_state->best_size * reorder_db->config.max_growth) {
            --s_state->pos;
            break;
        }

        // update the best position
        if (s_state->size <= s_state->best_size) {
            s_state->best_size = s_state->size;
            s_state->best_pos = s_state->pos;
        }

        // Update the lower bound on DD size
        if (interact_test(&reorder_db->matrix, xIndex, yIndex)) {
            L += (int) mrc_var_nnodes_get(&reorder_db->mrc, y) - mrc_is_var_isolated(&reorder_db->mrc, xIndex);
        }

        if ((int) s_state->size < limitSize) limitSize = (int) s_state->size;
#if STATS
        printf("sift up pos: x: %d (L: %d, size: %d, limitSize: %d) (xNodes: %zu, yNodes: %zu)\n",
               x, L, s_state->size, limitSize,
               mrc_var_nnodes_get(&reorder_db->mrc, x),
               mrc_var_nnodes_get(&reorder_db->mrc, y)
        );
#endif
#if 0
        printf("\n");
        for (size_t i = 0; i < levels->count; i++) {
            printf("level %zu (%d) \t has %u nodes\n", i, levels->order_to_level[i], levels_var_count_load(levels, i));
        }
        printf("\n");
#endif
        if (should_terminate_sifting(&reorder_db->config)) break;
    }

    if (s_state->size <= s_state->best_size) {
        s_state->best_size = s_state->size;
        s_state->best_pos = s_state->pos;
    }

#if STATS
    printf("\n");
    for (size_t i = 0; i < reorder_db->levels.count; i++) {
        printf("level %zu (%d) \t has %zu nodes\n", i, reorder_db->levels.order_to_level[i], mrc_var_nnodes_get(&reorder_db->mrc, i));
    }
    printf("\n");
#endif
    return SYLVAN_REORDER_SUCCESS;
}

TASK_IMPL_1(reorder_result_t, sylvan_siftback, sifting_state_t *, s_state)
{
    reorder_result_t res = SYLVAN_REORDER_SUCCESS;
#if STATS
    printf("\n");
#endif
    if (!reorder_db->is_initialised) return SYLVAN_REORDER_NOT_INITIALISED;
    if (s_state->pos == s_state->best_pos) return res;
    for (; s_state->pos <= s_state->best_pos; s_state->pos++) {
        if (s_state->size == s_state->best_size) return res;
        if (s_state->pos == UINT32_MAX) return res;
#if STATS
        printf("sift back: x: %d \t y: %d (size: %d)\n", s_state->pos, s_state->pos + 1, s_state->size);
#endif
        res = sylvan_varswap(s_state->pos);
        s_state->size = (int) get_nodes_count();
        if (!sylvan_reorder_issuccess(res)) return res;
        reorder_db->config.varswap_count++;
    }
    for (; s_state->pos >= s_state->best_pos; s_state->pos--) {
        if (s_state->pos == 0) break;
        if (s_state->size == s_state->best_size) return res;
#if STATS
        printf("sift back: x: %d \t y: %d (size: %d)\n", s_state->pos - 1, s_state->pos, s_state->size);
#endif
        res = sylvan_varswap(s_state->pos - 1);
        s_state->size = (int) get_nodes_count();
        if (!sylvan_reorder_issuccess(res)) return res;
        reorder_db->config.varswap_count++;
    }
    return res;
}

VOID_TASK_IMPL_1(sylvan_pre_reorder, reordering_type_t, type)
{
    sylvan_clear_cache();

    reorder_remark_node_ids(reorder_db, nodes);

    if (reorder_db->config.print_stat) {
        char buff[100];
        sylvan_reorder_type_description(type, buff, 100);
#if SYLVAN_USE_LINEAR_PROBING
        printf("BDD reordering with %s (probing): from %zu to ... ", buff, llmsset_count_marked(nodes));
#else
        printf("BDD reordering with %s (chaining): from %zu to ... ", buff, llmsset_count_marked(nodes));
#endif
    }

    mrc_init(&reorder_db->mrc, reorder_db->levels.count, nodes->table_size, reorder_db->node_ids);
    interact_init(&reorder_db->matrix, &reorder_db->levels, reorder_db->levels.count, nodes->table_size);

    reorder_db->call_count++;
    reorder_db->mrc.isolated_count = 0;

    sylvan_stats_count(SYLVAN_RE_COUNT);
    sylvan_timer_start(SYLVAN_RE);

    for (re_hook_entry_t e = prere_list; e != NULL; e = e->next) {
        WRAP(e->cb);
    }

    reorder_db->config.t_start_sifting = wctime();
    reorder_db->config.total_num_var = 0;
}

VOID_TASK_IMPL_0(sylvan_post_reorder)
{
    size_t after_size = llmsset_count_marked(nodes);

    // new size threshold for next reordering is double the size of non-terminal nodes + the terminal nodes
    size_t new_size_threshold = (after_size + 1) * SYLVAN_REORDER_SIZE_RATIO;
    if (reorder_db->call_count < SYLVAN_REORDER_LIMIT || new_size_threshold > reorder_db->config.size_threshold) {
        reorder_db->config.size_threshold = new_size_threshold;
    } else {
        reorder_db->config.size_threshold += SYLVAN_REORDER_LIMIT;
    }

    mrc_deinit(&reorder_db->mrc);
    interact_deinit(&reorder_db->matrix);

    double end = wctime() - reorder_db->config.t_start_sifting;
    if (reorder_db->config.print_stat) {
        printf("%zu nodes in %f sec ", after_size, end);
        size_t filled, total;
        sylvan_table_usage(&filled, &total);
        printf("\t (%zu / %zu (%.2f%%))\n", filled, total, (double) filled / (double) total * 100.0);
    }

    for (re_hook_entry_t e = postre_list; e != NULL; e = e->next) {
        WRAP(e->cb);
    }

    sylvan_timer_stop(SYLVAN_RE);
}

void sylvan_reorder_resdescription(reorder_result_t result, char *buf, size_t buf_len)
{
    (void) buf_len;
    assert(buf_len >= 100);
    switch (result) {
        case SYLVAN_REORDER_ROLLBACK:
            sprintf(buf, "SYLVAN_REORDER: the operation was aborted and rolled back (%d)", result);
            break;
        case SYLVAN_REORDER_SUCCESS:
            sprintf(buf, "SYLVAN_REORDER: success (%d)", result);
            break;
        case SYLVAN_REORDER_P0_CLEAR_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash in phase 0, no marked nodes remaining (%d)", result);
            break;
        case SYLVAN_REORDER_P1_REHASH_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash in phase 1, no marked nodes remaining (%d)", result);
            break;
        case SYLVAN_REORDER_P1_REHASH_FAIL_MARKED:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash in phase 1, marked nodes remaining (%d)", result);
            break;
        case SYLVAN_REORDER_P2_REHASH_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash in phase 2, no marked nodes remaining (%d)", result);
            break;
        case SYLVAN_REORDER_P2_CREATE_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot create node in phase 2, marked nodes remaining (%d)", result);
            break;
        case SYLVAN_REORDER_P2_MAPNODE_CREATE_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot create mapnode in phase 2, marked nodes remaining (%d)", result);
            break;
        case SYLVAN_REORDER_P2_REHASH_AND_CREATE_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash and cannot create node in phase 2 (%d)", result);
            break;
        case SYLVAN_REORDER_P3_REHASH_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot rehash in phase 3, maybe there are marked nodes remaining (%d)",
                    result);
            break;
        case SYLVAN_REORDER_P3_CLEAR_FAIL:
            sprintf(buf, "SYLVAN_REORDER: cannot clear in phase 3, maybe there are marked nodes remaining (%d)",
                    result);
            break;
        case SYLVAN_REORDER_NO_REGISTERED_VARS:
            sprintf(buf, "SYLVAN_REORDER: the operation failed fast because there are no registered variables (%d)",
                    result);
            break;
        case SYLVAN_REORDER_NOT_INITIALISED:
            sprintf(buf, "SYLVAN_REORDER: please make sure you first initialize reordering (%d)", result);
            break;
        case SYLVAN_REORDER_ALREADY_RUNNING:
            sprintf(buf, "SYLVAN_REORDER: cannot start reordering when it is already running (%d)", result);
            break;
        case SYLVAN_REORDER_NOT_ENOUGH_MEMORY:
            sprintf(buf, "SYLVAN_REORDER: not enough memory (%d)", result);
            break;
        default:
            sprintf(buf, "SYLVAN_REORDER: UNKNOWN ERROR (%d)", result);
            break;
    }
}

void sylvan_print_reorder_res(reorder_result_t result)
{
    char buff[100];
    sylvan_reorder_resdescription(result, buff, 100);
    if (!sylvan_reorder_issuccess(result)) fprintf(stderr, "%s\n", buff);
    else fprintf(stdout, "%s\n", buff);
}

void sylvan_reorder_type_description(reordering_type_t type, char *buf, size_t buf_len)
{
    (void) buf_len;
    assert(buf_len >= 100);
    switch (type) {
        case SYLVAN_REORDER_BOUNDED_SIFT:
            sprintf(buf, "bounded sifting");
            break;
        case SYLVAN_REORDER_SIFT:
            sprintf(buf, "sifting");
    }
}

int should_terminate_sifting(const struct reorder_config *reorder_config)
{
    for (re_term_entry_t e = termre_list; e != NULL; e = e->next) {
        if (e->cb()) {
#if INFO
            printf("sifting exit: termination_cb\n");
#endif
            return 1;
        }
    }
    if (reorder_config->varswap_count > reorder_config->max_swap) {
#if INFO
        printf("sifting exit: reached %u from the total_num_swap %u\n",
               reorder_config->varswap_count,
               reorder_config->max_swap);
#endif
        return 1;
    }

    double t_elapsed = wctime_ms_elapsed(reorder_config->t_start_sifting);
    if (t_elapsed > reorder_config->time_limit_ms && reorder_config->t_start_sifting != 0) {
#if INFO
        printf("sifting exit: reached %fms from the time_limit %.2fms\n",
               t_elapsed,
               reorder_config->time_limit_ms);
#endif
        return 1;
    }
    return 0;
}

int should_terminate_reordering(const struct reorder_config *reorder_config)
{
    for (re_term_entry_t e = termre_list; e != NULL; e = e->next) {
        if (e->cb()) {
#if INFO
            printf("reordering exit: termination_cb\n");
#endif
            return 1;
        }
    }

    if (reorder_config->total_num_var > reorder_config->max_var) {
#if INFO
        printf("reordering exit: reached %u from the total_num_var %u\n",
               reorder_config->total_num_var,
               reorder_config->max_var);
#endif
        return 1;
    }
    double t_elapsed = wctime_ms_elapsed(reorder_config->t_start_sifting);
    if (t_elapsed > reorder_config->time_limit_ms && reorder_config->t_start_sifting != 0) {
#if INFO
        printf("reordering exit: reached %fms from the time_limit %.2zums\n",
               t_elapsed,
               (size_t) reorder_config->time_limit_ms);
#endif
        return 1;
    }
    return 0;
}

void reorder_remark_node_ids(reorder_db_t self, llmsset_t dbs)
{
    roaring_bitmap_clear(self->node_ids);

    atomic_bitmap_t bitmap2 = {
            .container = dbs->bitmap2,
            .size = dbs->table_size
    };
    bitmap_container_t index = atomic_bitmap_next(&bitmap2, 1);
    for (; index != npos && index < dbs->table_size; index = atomic_bitmap_next(&bitmap2, index)) {
        roaring_bitmap_add(self->node_ids, index);
    }
}