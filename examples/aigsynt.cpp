#include <getopt.h>

#include <sys/time.h>
#include <sys/mman.h>
#include <boost/graph/sloan_ordering.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <span>

#include <sylvan.h>
#include <sylvan_int.h>
#include <sylvan_table.h>
#include <sylvan_reorder.h>
#include "aag.hpp"

using namespace sylvan;

/* Configuration */
static int workers = 4;
static int verbose = 0;
static char *model_filename = nullptr; // filename of the aag file
static int static_reorder = 0;
static int dynamic_reorder = 1;

static int sloan_w1 = 1;
static int sloan_w2 = 8;

static int terminate_reordering = 0;

static void
print_usage()
{
    printf("Usage: aigsynt [-w <workers>] [-d --dynamic-reordering] [-s --static-reordering]\n");
    printf("               [-v --verbose] [--help] [--usage] <model> [<output-bdd>]\n");
}

static void
print_help()
{
    printf("Usage: aigsynt [OPTION...] <model> [<output-bdd>]\n\n");
    printf("                             Strategy for reachability (default=par)\n");
    printf("  -d,                        Dynamic variable ordering\n");
    printf("  -w, --workers=<workers>    Number of workers (default=0: autodetect)\n");
    printf("  -v,                        Dynamic variable ordering\n");
    printf("  -s,                        Reorder with Sloan\n");
    printf("  -h, --help                 Give this help list\n");
    printf("      --usage                Give a short usage message\n");
}

static void
parse_args(int argc, char **argv)
{
    static const option longopts[] = {
            {"workers",            required_argument, (int *) 'w', 1},
            {"dynamic-reordering", no_argument,       nullptr,     'v'},
            {"static-reordering",  no_argument,       nullptr,     'v'},
            {"verbose",            no_argument,       nullptr,     'v'},
            {"help",               no_argument,       nullptr,     'h'},
            {"usage",              no_argument,       nullptr,     99},
            {nullptr,              no_argument,       nullptr,     0},
    };
    int key = 0;
    int long_index = 0;
    while ((key = getopt_long(argc, argv, "w:s:h", longopts, &long_index)) != -1) {
        switch (key) {
            case 'w':
                workers = atoi(optarg);
                break;
            case 's':
                static_reorder = 1;
                break;
            case 'd':
                dynamic_reorder = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 99:
                print_usage();
                exit(0);
            case 'h':
                print_help();
                exit(0);
        }
    }
    if (optind >= argc) {
        print_usage();
        exit(0);
    }
    model_filename = argv[optind];
}


#define make_gate(a, b, c, d) CALL(make_gate, a, b, c, d)
VOID_TASK_4(make_gate, int, a, MTBDD*, gates, aag_t &, aag, std::span<int>, level_to_var)
{
    if (gates[a] != sylvan_invalid) return;
    int lft = (int) aag.gatelft[a] / 2;
    int rgt = (int) aag.gatergt[a] / 2;

    MTBDD l, r;
    if (lft == 0) {
        l = sylvan_false;
    } else if (aag.lookup[lft] != -1) {
        make_gate(aag.lookup[lft], gates, aag, level_to_var);
        l = gates[aag.lookup[lft]];
    } else {
        l = sylvan_ithlevel(level_to_var[lft]); // always use even variables (prime is odd)
    }
    if (rgt == 0) {
        r = sylvan_false;
    } else if (aag.lookup[rgt] != -1) {
        make_gate(aag.lookup[rgt], gates, aag, level_to_var);
        r = gates[aag.lookup[rgt]];
    } else {
        r = sylvan_ithlevel(level_to_var[rgt]); // always use even variables (prime is odd)
    }
    if (aag.gatelft[a] & 1) l = sylvan_not(l);
    if (aag.gatergt[a] & 1) r = sylvan_not(r);
    gates[a] = sylvan_and(l, r);
    mtbdd_protect(&gates[a]);
    sylvan_test_reduce_heap();
}

std::vector<int> order_statically(const aag_t &aag)
{
    std::vector<int> matrix;
    for (unsigned m = 0; m < aag.header.m * aag.header.m; m++) matrix[m] = 0;
    for (unsigned m = 0; m < aag.header.m; m++) matrix[m * aag.header.m + m] = 1;

    for (uint64_t i = 0; i < aag.header.i; i++) {
        int v = (int) aag.inputs[i] / 2 - 1;
        matrix[v * aag.header.m + v] = 1;
    }

    for (uint64_t l = 0; l < aag.header.l; l++) {
        int v = (int) aag.latches[l] / 2 - 1;
        int n = (int) aag.l_next[l] / 2 - 1;
        matrix[v * aag.header.m + v] = 1; // l -> l
        if (n >= 0) {
            matrix[v * aag.header.m + n] = 1; // l -> n
            matrix[n * aag.header.m + v] = 1; // make symmetric
        }
    }

    for (uint64_t a = 0; a < aag.header.a; a++) {
        int v = (int) aag.gatelhs[a] / 2 - 1;
        int x = (int) aag.gatelft[a] / 2 - 1;
        int y = (int) aag.gatergt[a] / 2 - 1;
        matrix[v * aag.header.m + v] = 1;
        if (x >= 0) {
            matrix[v * aag.header.m + x] = 1;
            matrix[x * aag.header.m + v] = 1;
        }
        if (y >= 0) {
            matrix[v * aag.header.m + y] = 1;
            matrix[y * aag.header.m + v] = 1;
        }
    }
#if 0
    printf("Matrix\n");
    for (unsigned row = 0; row < aag.header.m; row++) {
        for (unsigned col = 0; col < aag.header.m; col++) {
            printf("%c", matrix[row * aag.header.m + col] ? '+' : '-');
        }
        printf("\n");
    }
#endif
    typedef boost::adjacency_list<boost::setS, boost::vecS, boost::undirectedS,
            boost::property<boost::vertex_color_t, boost::default_color_type,
                    boost::property<boost::vertex_degree_t, int,
                            boost::property<boost::vertex_priority_t, double>>>> Graph;

    typedef boost::graph_traits<Graph>::vertex_descriptor Vertex;

    Graph g = Graph(aag.header.m);

    for (unsigned row = 0; row < aag.header.m; row++) {
        for (unsigned col = 0; col < aag.header.m; col++) {
            if (matrix[row * aag.header.m + col]) boost::add_edge(row, col, g);
        }
    }

    std::vector<Vertex> inv_perm(boost::num_vertices(g));

    boost::sloan_ordering(g, inv_perm.begin(), boost::get(boost::vertex_color, g), boost::make_degree_map(g),
                          boost::get(boost::vertex_priority, g), sloan_w1, sloan_w2);

    std::vector<int> level_to_var;

    for (uint64_t i = 0; i <= aag.header.m; i++) level_to_var[i] = -1;

    int r = 0;
#if 0
    boost::property_map<Graph, boost::vertex_index_t>::type index_map = boost::get(boost::vertex_index, g);
    for (unsigned long & i : inv_perm) {
        uint64_t j = index_map[i];
        printf("%d %zu\n", r++, (size_t)j);
    }
#endif

    r = 0;
    for (unsigned long &i: inv_perm) {
        uint64_t j = i + 1;
        if (level_to_var[j] != -1) {
            printf("ERROR: level_to_var of %zu is already %d (%d)\n", (size_t) j, level_to_var[j], r);
            for (uint64_t k = 1; k <= aag.header.m; k++) {
                if (level_to_var[k] == -1) printf("%zu is still -1\n", (size_t) k);
                level_to_var[k] = r++;
            }
        } else {
            level_to_var[j] = r++;
        }
    }

    printf("r=%d M=%d\n", r, (int) aag.header.m);
#if 1
    for (unsigned m = 0; m < aag.header.m * aag.header.m; m++) matrix[m] = 0;

    for (uint64_t i = 0; i < aag.header.i; i++) {
        int v = level_to_var[aag.inputs[i] / 2];
        matrix[v * aag.header.m + v] = 1;
    }

    for (uint64_t l = 0; l < aag.header.l; l++) {
        int v = level_to_var[aag.latches[l] / 2];
        int n = level_to_var[aag.l_next[l] / 2];
        matrix[v * aag.header.m + v] = 1; // l -> l
        if (n >= 0) {
            matrix[v * aag.header.m + n] = 1; // l -> n
        }
    }

    for (uint64_t a = 0; a < aag.header.a; a++) {
        int v = level_to_var[aag.gatelhs[a] / 2];
        int x = level_to_var[aag.gatelft[a] / 2];
        int y = level_to_var[aag.gatergt[a] / 2];
        matrix[v * aag.header.m + v] = 1;
        if (x >= 0) {
            matrix[v * aag.header.m + x] = 1;
        }
        if (y >= 0) {
            matrix[v * aag.header.m + y] = 1;
        }
    }

    printf("Matrix\n");
    for (unsigned row = 0; row < aag.header.m; row++) {
        for (unsigned col = 0; col < aag.header.m; col++) {
            printf("%c", matrix[row * aag.header.m + col] ? '+' : '-');
        }
        printf("\n");
    }
#endif

    return level_to_var;
}

VOID_TASK_1(parse, std::span<uint8_t>, buffer)
{
    aag_t aag = read_aag(buffer);

    sylvan_newlevels(aag.header.m + 1);

    std::vector<int> level_to_order;
    level_to_order.reserve(aag.header.m + 1);

    if (static_reorder) {
        level_to_order = order_statically(aag);
    } else {
        for (int i = 0; i <= (int) aag.header.m; i++) {
            level_to_order[i] = i;
        }
    }

    MTBDD Xc = sylvan_set_empty();
    MTBDD Xu = sylvan_set_empty();
    mtbdd_protect(&Xc);
    mtbdd_protect(&Xu);

    // Now read the [[optional]] labels to find controllable vars
    while (true) {
        int c = parser_peek(buffer);
        if (c != 'l' and c != 'i' and c != 'o') break;
        parser_skip();
        int pos_ = (int) read_uint(buffer);
        read_token(" ", buffer);
        std::string s;
        read_string(s, buffer);
        read_wsnl(buffer);
        if (c == 'i') {
            if (strncmp(s.c_str(), "controllable_", 13) == 0) {
                Xc = sylvan_set_add(Xc, level_to_order[aag.inputs[pos_] / 2]);
            } else {
                Xu = sylvan_set_add(Xu, level_to_order[aag.inputs[pos_] / 2]);
            }
        }
    }

#if 0
    sylvan_print(Xc);
    printf("\n");
    sylvan_print(Xu);
    printf("\n");
#endif

    INFO("There are %zu controllable and %zu uncontrollable inputs.\n", sylvan_set_count(Xc), sylvan_set_count(Xu));

    INFO("Making the gate BDDs...\n");

    MTBDD gates[aag.header.a];
    for (uint64_t a = 0; a < aag.header.a; a++) gates[a] = sylvan_invalid;
    for (uint64_t a = 0; a < aag.header.a; a++) make_gate(a, gates, aag, level_to_order);
    if (verbose) INFO("Gates have size %zu\n", mtbdd_nodecount_more(gates, aag.header.a));

#if 0
    for (uint64_t g=0; g<A; g++) {
        INFO("gate %d has size %zu\n", (int)g, sylvan_nodecount(gates[g]));
    }
#endif

#if 0
    for (uint64_t g=0; g<A; g++) {
        MTBDD supp = sylvan_support(gates[g]);
        while (supp != sylvan_set_empty()) {
            printf("%d ", sylvan_set_first(supp));
            supp = sylvan_set_next(supp);
        }
        printf("\n");
    }
#endif

#if 0
    MTBDD lnext[L];
    for (uint64_t l=0; l<L; l++) {
        MTBDD nxt;
        if (lookup[l_next[l]/2] == -1) {
            nxt = sylvan_ithlevel(l_next[l]&1);
        } else {
            nxt = gates[lookup[l_next[l]]];
        }
        if (l_next[l]&1) nxt = sylvan_not(nxt);
        lnext[l] = sylvan_equiv(sylvan_ithvar(latches[l]+1), nxt);
    }
    INFO("done making latches\n");
#endif

    MTBDD Lvars = sylvan_set_empty();
    mtbdd_protect(&Lvars);

    for (uint64_t l = 0; l < aag.header.l; l++) {
        Lvars = sylvan_set_add(Lvars, level_to_order[aag.latches[l] / 2]);
    }

#if 0
    MTBDD LtoPrime = sylvan_map_empty();
    for (uint64_t l=0; l<L; l++) {
        LtoPrime = sylvan_map_add(LtoPrime, latches[l], latches[l]+1);
    }
#endif

    // Actually just make the compose vector
    MTBDD CV = sylvan_map_empty();
    mtbdd_protect(&CV);

    for (uint64_t l = 0; l < aag.header.l; l++) {
        MTBDD nxt;
        if (aag.lookup[aag.l_next[l] / 2] == -1) {
            nxt = sylvan_ithlevel(level_to_order[aag.l_next[l] / 2]);
        } else {
            nxt = gates[aag.lookup[aag.l_next[l] / 2]];
        }
        if (aag.l_next[l] & 1) nxt = sylvan_not(nxt);
        CV = sylvan_map_add(CV, level_to_order[aag.latches[l] / 2], nxt);
    }

    // now make output
    INFO("output is %zu (lookup: %d)\n", (size_t) aag.outputs[0], aag.lookup[aag.outputs[0] / 2]);
    MTBDD Unsafe;
    mtbdd_protect(&Unsafe);
    if (aag.lookup[aag.outputs[0] / 2] == -1) {
        Unsafe = sylvan_ithlevel(aag.outputs[0] / 2);
    } else {
        Unsafe = gates[aag.lookup[aag.outputs[0] / 2]];
    }
    if (aag.outputs[0] & 1) Unsafe = sylvan_not(Unsafe);
    Unsafe = sylvan_forall(Unsafe, Xc);
    Unsafe = sylvan_exists(Unsafe, Xu);


#if 0
    MTBDD supp = sylvan_support(Unsafe);
    while (supp != sylvan_set_empty()) {
        printf("%d ", sylvan_set_first(supp));
        supp = sylvan_set_next(supp);
    }
    printf("\n");
    INFO("exactly %.0f states are bad\n", sylvan_satcount(Unsafe, Lvars));
#endif

    MTBDD OldUnsafe = sylvan_false; // empty set
    MTBDD Step = sylvan_false;
    mtbdd_protect(&OldUnsafe);
    mtbdd_protect(&Step);

    while (Unsafe != OldUnsafe) {
        OldUnsafe = Unsafe;

        Step = sylvan_compose(Unsafe, CV);
        Step = sylvan_forall(Step, Xc);
        Step = sylvan_exists(Step, Xu);

        // check if initial state in Step (all 0)
        MTBDD Check = Step;
        while (Check != sylvan_false) {
            if (Check == sylvan_true) {
                INFO("UNREALIZABLE\n");
                return;
            } else {
                Check = sylvan_low(Check);
            }
        }

        Unsafe = sylvan_or(Unsafe, Step);
    }

    INFO("REALIZABLE\n");
}

VOID_TASK_0(gc_start)
{
    size_t used, total;
    sylvan_table_usage(&used, &total);
    INFO("GC: str: %zu/%zu size\n", used, total);
}

VOID_TASK_0(gc_end)
{
    size_t used, total;
    sylvan_table_usage(&used, &total);
    INFO("GC: end: %zu/%zu size\n", used, total);
}

VOID_TASK_0(reordering_start)
{
    size_t table_size = llmsset_count_marked(nodes);
    INFO("RE: str: %zu size\n", table_size);
}

VOID_TASK_0(reordering_progress)
{
    size_t table_size = llmsset_count_marked(nodes);
    INFO("RE: prg: %zu size\n", table_size);
}

VOID_TASK_0(reordering_end)
{
    size_t table_size = llmsset_count_marked(nodes);
    INFO("RE: end: %zu size\n", table_size);
}

int should_reordering_terminate()
{
    return terminate_reordering;
}

int main(int argc, char **argv)
{
    /**
     * Parse command line, set locale, set startup time for INFO messages.
     */
    parse_args(argc, argv);
    setlocale(LC_NUMERIC, "en_US.utf-8");
    t_start = wctime();

    lace_start(workers, 0);

    sylvan_set_limits(8LL * 1024 * 1024 * 1024, 1, 8);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();
    sylvan_gc_enable();

    sylvan_set_reorder_nodes_threshold(32);
    sylvan_set_reorder_maxgrowth(1.2f);
    sylvan_set_reorder_timelimit_sec(30);

    // Set hooks for logging garbage collection & dynamic variable reordering
    if (verbose) {
        sylvan_re_hook_prere(TASK(reordering_start));
        sylvan_re_hook_postre(TASK(reordering_end));
        sylvan_re_hook_progre(TASK(reordering_progress));
        sylvan_re_hook_termre(should_reordering_terminate);
        sylvan_gc_hook_pregc(TASK(gc_start));
        sylvan_gc_hook_postgc(TASK(gc_end));
    }

    INFO("Model: %s\n", model_filename);
    if (model_filename == nullptr) {
        Abort("Invalid file name.\n");
    }

    int fd = open(model_filename, O_RDONLY);

    struct stat filestat{};
    if (fstat(fd, &filestat) != 0) Abort("cannot stat file\n");
    size = filestat.st_size;

    auto *buf = (uint8_t *) mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) Abort("mmap failed\n");

    RUN(parse, std::span{buf, size});

    // Report Sylvan statistics (if SYLVAN_STATS is set)
    if (verbose) sylvan_stats_report(stdout);

    lace_stop();

    return 0;
}
