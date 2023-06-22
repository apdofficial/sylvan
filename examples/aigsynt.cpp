#include <getopt.h>

#include <sys/time.h>
#include <sys/mman.h>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <span>
#include <cstring>

#include <sylvan.h>
#include <clocale>
#include "aag.h"

using namespace sylvan;

typedef struct safety_game
{
    MTBDD *gates;           // and gates
    MTBDD c_inputs;         // controllable inputs
    MTBDD u_inputs;         // uncontrollable inputs
    int *level_to_order;    // mapping from variable level to static variable order
} safety_game_t;

double t_start;
#define INFO(s, ...) fprintf(stdout, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__)
#define Abort(s, ...) { fprintf(stderr, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); exit(-1); }


/* Configuration */
static int workers = 1;
static int verbose = 0;
static char *filename = nullptr; // filename of the aag file

/* Global variables */
static aag_file_t aag{
        .header = {
                .m = 0,
                .i = 0,
                .l = 0,
                .o = 0,
                .a = 0,
                .b = 0,
                .c = 0,
                .j = 0,
                .f = 0
        },
        .inputs = nullptr,
        .outputs = nullptr,
        .latches = nullptr,
        .l_next = nullptr,
        .lookup = nullptr,
        .gatelhs = nullptr,
        .gatelft = nullptr,
        .gatergt = nullptr
};
static aag_buffer_t aag_buffer{
        .content = NULL,
        .size = 0,
        .pos = 0,
        .file_descriptor = -1,
        .filestat = {}
};
static safety_game_t game{
        .gates = nullptr,
        .c_inputs = sylvan_set_empty(),
        .u_inputs = sylvan_set_empty(),
        .level_to_order = nullptr
};

/* Obtain current wallclock time */
static double
wctime()
{
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

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
    printf("  -w, --workers=<workers>    Number of workers (default=0: autodetect)\n");
    printf("  -h, --help                 Give this help list\n");
    printf("      --usage                Give a short usage message\n");
}

static void
parse_args(int argc, char **argv)
{
    static const option longopts[] = {
            {"workers",            required_argument, (int *) 'w', 1},
            {"dynamic-reordering", no_argument,       nullptr,     'd'},
            {"static-reordering",  no_argument,       nullptr,     's'},
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
    filename = argv[optind];
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

#define make_gate(gate) CALL(make_gate, gate)
VOID_TASK_1(make_gate, int, gate)
{
    if (game.gates[gate] != sylvan_invalid) {
        return;
    }

    int lft = (int) aag.gatelft[gate] / 2;
    int rgt = (int) aag.gatergt[gate] / 2;

    printf("gate %d\t lhs %d (%d) \t rhs %d (%d)\t ", gate, lft, aag.lookup[lft], rgt, aag.lookup[rgt]);

//    size_t filled, total;
//    sylvan_table_usage(&filled, &total);
//    printf("(%zu/%zu)\t ", filled, total);

    MTBDD l, r;
    if (lft == 0) {
        l = sylvan_false;
    } else if (aag.lookup[lft] != -1) {
        make_gate(aag.lookup[lft]);
        l = game.gates[aag.lookup[lft]];
    } else {
        l = sylvan_ithvar(game.level_to_order[lft]); // always use even variables (prime is odd)
    }

    if (rgt == 0) {
        r = sylvan_false;
    } else if (aag.lookup[rgt] != -1) {
        make_gate(aag.lookup[rgt]);
        r = game.gates[aag.lookup[rgt]];
    } else {
        r = sylvan_ithvar(game.level_to_order[rgt]); // always use even variables (prime is odd)
    }

    if (aag.gatelft[gate] & 1) l = sylvan_not(l);
    if (aag.gatergt[gate] & 1) r = sylvan_not(r);
    game.gates[gate] = sylvan_and(l, r);
    mtbdd_protect(&game.gates[gate]);

    printf("nodecount: %zu\n", sylvan_nodecount(game.gates[gate]));

}

#define solve_game() RUN(solve_game)
TASK_0(int, solve_game)
{
    game.level_to_order = new int[aag.header.m + 1];
    for (size_t i = 0; i <= aag.header.m; i++) {
        game.level_to_order[i] = (int)i;
    }

    INFO("Making the gate BDDs...\n");

    game.gates = new MTBDD[aag.header.a];
    for (uint64_t a = 0; a < aag.header.a; a++) {
        game.gates[a] = sylvan_invalid;
    }

    for (uint64_t gate = 0; gate < aag.header.a; gate++) {
        make_gate(gate);
    }

    INFO("Gates have size %zu\n", mtbdd_nodecount_more(game.gates, aag.header.a));
    sylvan_stats_report(stdout);

    exit(0);

    game.c_inputs = sylvan_set_empty();
    game.u_inputs = sylvan_set_empty();
    mtbdd_protect(&game.c_inputs);
    mtbdd_protect(&game.u_inputs);

    // Now read the [[optional]] labels to find controllable vars
    while (true) {
        int c = aag_buffer_peek(&aag_buffer);
        if (c != 'l' and c != 'i' and c != 'o') break;
        aag_buffer_skip(&aag_buffer);
        int pos = (int) aag_buffer_read_uint(&aag_buffer);
        aag_buffer_read_token(" ", &aag_buffer);
        std::string s;
        aag_buffer_read_string(s, &aag_buffer);
        aag_buffer_read_wsnl(&aag_buffer);
        if (c == 'i') {
            if (strncmp(s.c_str(), "controllable_", 13) == 0) {
                game.c_inputs = sylvan_set_add(game.c_inputs, game.level_to_order[aag.inputs[pos] / 2]);
            } else {
                game.u_inputs = sylvan_set_add(game.u_inputs, game.level_to_order[aag.inputs[pos] / 2]);
            }
        }
    }

    INFO("There are %zu controllable and %zu uncontrollable inputs.\n", sylvan_set_count(game.c_inputs),
         sylvan_set_count(game.u_inputs));

    // Actually just make the compose vector
    MTBDD CV = sylvan_map_empty();
    mtbdd_protect(&CV);

    for (uint64_t l = 0; l < aag.header.l; l++) {
        MTBDD nxt;
        if (aag.lookup[aag.l_next[l] / 2] == -1) {
            nxt = sylvan_ithvar(game.level_to_order[aag.l_next[l] / 2]);
        } else {
            nxt = game.gates[aag.lookup[aag.l_next[l] / 2]];
        }
        if (aag.l_next[l] & 1) nxt = sylvan_not(nxt);
        CV = sylvan_map_add(CV, game.level_to_order[aag.latches[l] / 2], nxt);
    }

    // now make output
    INFO("output is %zu (lookup: %d)\n", (size_t) aag.outputs[0], aag.lookup[aag.outputs[0] / 2]);
    MTBDD Unsafe;
    mtbdd_protect(&Unsafe);
    if (aag.lookup[aag.outputs[0] / 2] == -1) {
        Unsafe = sylvan_ithvar(aag.outputs[0] / 2);
    } else {
        Unsafe = game.gates[aag.lookup[aag.outputs[0] / 2]];
    }
    if (aag.outputs[0] & 1) Unsafe = sylvan_not(Unsafe);
    Unsafe = sylvan_forall(Unsafe, game.c_inputs);
    Unsafe = sylvan_exists(Unsafe, game.u_inputs);

    MTBDD OldUnsafe = sylvan_false; // empty set
    MTBDD Step = sylvan_false;
    mtbdd_protect(&OldUnsafe);
    mtbdd_protect(&Step);

    while (Unsafe != OldUnsafe) {
        OldUnsafe = Unsafe;

        Step = sylvan_compose(Unsafe, CV);
        Step = sylvan_forall(Step, game.c_inputs);
        Step = sylvan_exists(Step, game.u_inputs);

        // check if initial state in Step (all 0)
        MTBDD Check = Step;
        while (Check != sylvan_false) {
            if (Check == sylvan_true) {
                return 0;
            } else {
                Check = sylvan_low(Check);
            }
        }

        Unsafe = sylvan_or(Unsafe, Step);
    }
    return 1;
}

int main(int argc, char **argv)
{
    setlocale(LC_NUMERIC, "en_US.utf-8");

    t_start = wctime();
    parse_args(argc, argv);
    INFO("Model: %s\n", filename);
    if (filename == nullptr) {
        Abort("Invalid file name.\n");
    }

    aag_buffer_open(&aag_buffer, filename, O_RDONLY);
    aag_file_read(&aag, &aag_buffer);

    if (verbose) {
        INFO("----------header----------\n");
        INFO("# of variables            \t %lu\n", aag.header.m);
        INFO("# of inputs               \t %lu\n", aag.header.i);
        INFO("# of latches              \t %lu\n", aag.header.l);
        INFO("# of outputs              \t %lu\n", aag.header.o);
        INFO("# of AND gates            \t %lu\n", aag.header.a);
        INFO("# of bad state properties \t %lu\n", aag.header.b);
        INFO("# of invariant constraints\t %lu\n", aag.header.c);
        INFO("# of justice properties   \t %lu\n", aag.header.j);
        INFO("# of fairness constraints \t %lu\n", aag.header.f);
        INFO("--------------------------\n");
    }

    lace_start(workers, 0);

    // 1LL<<21: 16384 nodes
    // 1LL<<22: 32768 nodes
    // 1LL<<23: 65536 nodes
    // 1LL<<24: 131072 nodes
    // 1LL<<25: 262144 nodes
    sylvan_set_limits(1LL<<22, 1, 0);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_gc_enable();

    // Set hooks for logging garbage collection & dynamic variable reordering
    if (verbose) {
        sylvan_gc_hook_pregc(TASK(gc_start));
        sylvan_gc_hook_postgc(TASK(gc_end));
    }

    int is_realizable = solve_game();
    if (is_realizable) {
        INFO("REALIZABLE\n");
    } else {
        INFO("UNREALIZABLE\n");
    }

    // Report Sylvan statistics (if SYLVAN_STATS is set)
    if (verbose) sylvan_stats_report(stdout);

    aag_buffer_close(&aag_buffer);
    sylvan_quit();
    lace_stop();

    return 0;
}