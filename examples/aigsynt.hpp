#ifndef SYLVAN_AIGSYNT_H
#define SYLVAN_AIGSYNT_H

#include <sys/time.h>
#include <sys/mman.h>
#include <boost/graph/sloan_ordering.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <span>
#include <utility>
#include <getopt.h>
#include "sylvan_mtbdd.h"

double t_start;
#define INFO(s, ...) fprintf(stdout, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__)
#define Abort(s, ...) { fprintf(stderr, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); exit(-1); }

/* Obtain current wallclock time */
static double
wctime()
{
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

struct Configs
{
private:
    static void print_usage()
    {
        printf("Usage: aigsynt [-w <workers>] [--d --dynamic-reordering] [--s --static-reordering]\n");
        printf("               [--v --verbose] [--help] [--usage] <model>\n");
    }

    static void print_help()
    {
        printf("Usage: aigsynt [OPTION...] <model> \n\n");
        printf("                             Strategy for reachability (default=par)\n");
        printf("  --d,                       Dynamic variable ordering\n");
        printf("  --w, --workers=<workers>   Number of workers (default=0: autodetect)\n");
        printf("  --v,                       Dynamic variable ordering\n");
        printf("  --s,                       Reorder with Sloan\n");
        printf("  --h, --help                Give this help list\n");
        printf("       --usage               Give a short usage message\n");
    }

public:
    int workers;
    bool verbose;
    bool static_reorder;
    bool dynamic_reorder;
    int sloan_w1;
    int sloan_w2;
    std::optional<std::string> model_path;

    Configs(int argc, char **argv)
    {
        workers = 0; // autodetect
        verbose = false;
        static_reorder = false;
        dynamic_reorder = false;
        sloan_w1 = 0;
        sloan_w2 = 0;
        model_path = std::nullopt;

        static const option longopts[] = {
                {"workers",            required_argument, (int *)   'w', 1  },
                {"dynamic-reordering", no_argument,       nullptr,  'd'     },
                {"static-reordering",  no_argument,       nullptr,  's'     },
                {"verbose",            no_argument,       nullptr,  'v'     },
                {"help",               no_argument,       nullptr,  'h'     },
                {"usage",              no_argument,       nullptr,   99     },
                {nullptr,              no_argument,       nullptr,   0      },
        };
        int key = 0;
        int long_index = 0;
        while ((key = getopt_long(argc, argv, "w:s:h", longopts, &long_index)) != -1) {
            switch (key) {
                case 'w':
                    workers = atoi(optarg);
                    break;
                case 's':
                    static_reorder = true;
                    break;
                case 'd':
                    dynamic_reorder = true;
                    break;
                case 'v':
                    verbose = true;
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
        model_path = argv[optind];
    }

};

class AagFileBuffer
{
public:
    explicit AagFileBuffer(const char *filename = nullptr)
    {
        file_descriptor = open(filename, O_RDONLY);
        if (file_descriptor < 0) {
            std::ostringstream oss;
            oss << "failed to open file";
            if (filename) oss << " '" << filename << '\'';
            oss << ": " << strerror(errno);
            throw std::runtime_error(oss.str());
        }

        struct stat filestat{};
        if (fstat(file_descriptor, &filestat) != 0) Abort("cannot stat file\n");
        size_t size = filestat.st_size;

        auto *buf = (uint8_t *) mmap(nullptr, size, PROT_READ, MAP_SHARED, file_descriptor, 0);
        if (buf == MAP_FAILED) Abort("mmap failed\n");
        buffer = std::span{buf, size};
    }

    virtual ~AagFileBuffer()
    {
        munmap((void *) buffer.data(), buffer.size());
        close(file_descriptor);
    }

    [[nodiscard]] size_t size() const
    {
        return buffer.size();
    }

    int read()
    {
        if (pos == size()) return EOF;
        return (int) buffer[pos++];
    }

    void read_token(const char *str)
    {
        while (*str != 0) if (read() != (int) (uint8_t) (*str++)) err();
    }

    std::string read_string()
    {
        std::string s;
        while (true) {
            int c = peek();
            if (c == EOF || c == '\n') return s;
            s += (char) c;
            skip();
        }
    }

    uint64_t read_uint()
    {
        uint64_t r = 0;
        while (true) {
            int c = peek();
            if (c < '0' || c > '9') return r;
            r *= 10;
            r += c - '0';
            skip();
        }
    }

    void read_ws()
    {
        while (true) {
            int c = peek();
            if (c != ' ' && c != '\t') return;
            skip();
        }
    }

    void read_wsnl()
    {
        while (true) {
            int c = peek();
            if (c != ' ' && c != '\n' && c != '\t') return;
            skip();
        }
    }

    void skip()
    {
        pos++;
    }


    [[nodiscard]] int peek() const
    {
        if (pos == size()) return EOF;
        return (int) buffer[pos];
    }

    static void err()
    {
        Abort("File read error.");
    }

private:
    std::span<uint8_t> buffer;
    size_t pos = 0;
    int file_descriptor = -1;
};

class Aag
{
public:
    typedef struct header
    {
        uint64_t m; // maximum variable index
        uint64_t i; // number of inputs
        uint64_t l; // number of latches
        uint64_t o; // number of outputs
        uint64_t a; // number of AND gates
        uint64_t b; // number of bad state properties
        uint64_t c; // number of invariant constraints
        uint64_t j; // number of justice properties
        uint64_t f; // number of fairness constraints
    } header_t;

    header_t header{};
    std::map<uint64_t, uint64_t> inputs;
    std::map<uint64_t, uint64_t> outputs;
    std::map<uint64_t, uint64_t> latches;
    std::map<uint64_t, uint64_t> l_next;
    std::map<uint64_t, int> lookup;
    std::map<uint64_t, uint64_t> gatelhs;
    std::map<uint64_t, uint64_t> gatelft;
    std::map<uint64_t, uint64_t> gatergt;
    Configs configs;
    sylvan::MTBDD controllable_inputs = sylvan::sylvan_set_empty();
    sylvan::MTBDD uncontrollable_inputs = sylvan::sylvan_set_empty();

    explicit Aag(AagFileBuffer &aag_file, Configs configs) : configs(std::move(configs)), aag_file(aag_file)
    {
        read_aag();
        sylvan::sylvan_protect(&controllable_inputs);
        sylvan::sylvan_protect(&uncontrollable_inputs);
    }

    virtual ~Aag()
    {
        std::cout << "Aag::~Aag()" << std::endl;
        aag_file.~AagFileBuffer();
        sylvan::sylvan_unprotect(&controllable_inputs);
        sylvan::sylvan_unprotect(&uncontrollable_inputs);
    }

    void read_labels(std::span<int> level_to_order)
    {
        if (configs.verbose) INFO("Now reading optional labels to find controllable/uncontrollable vars\n");
        while (true) {
            int c = aag_file.peek();
            if (c != 'l' and c != 'i' and c != 'o') break;
            aag_file.skip();
            int pos = (int) aag_file.read_uint();
            aag_file.read_token(" ");
            auto s = aag_file.read_string();
            aag_file.read_wsnl();
            if (c == 'i') {
                if (s.starts_with("controllable_")) {
                    controllable_inputs = sylvan::sylvan_set_add(controllable_inputs, level_to_order[inputs[pos] / 2]);
                } else {
                    uncontrollable_inputs = sylvan::sylvan_set_add(uncontrollable_inputs, level_to_order[inputs[pos] / 2]);
                }
            }
        }
    }

private:
    AagFileBuffer aag_file;

    void read_aag()
    {
        read_header();
        if (configs.verbose) {
            INFO("----------header----------\n");
            INFO("# of variables            \t %lu\n", (size_t) header.m);
            INFO("# of inputs               \t %lu\n", (size_t) header.i);
            INFO("# of latches              \t %lu\n", (size_t) header.l);
            INFO("# of outputs              \t %lu\n", (size_t) header.o);
            INFO("# of AND gates            \t %lu\n", (size_t) header.a);
            INFO("# of bad state properties \t %lu\n", (size_t) header.b);
            INFO("# of invariant constraints\t %lu\n", (size_t) header.c);
            INFO("# of justice properties   \t %lu\n", (size_t) header.j);
            INFO("# of fairness constraints \t %lu\n", (size_t) header.f);
            INFO("--------------------------\n");
        }

        if (configs.verbose) INFO("Now reading %lu inputs\n", (size_t) header.i);
        for (uint64_t i = 0; i < header.i; i++) {
            inputs[i] = aag_file.read_uint();
            aag_file.read_wsnl();
        }

        if (configs.verbose) INFO("Now reading %lu latches\n", (size_t) header.l);
        for (uint64_t l = 0; l < header.l; l++) {
            latches[l] = (int)aag_file.read_uint();
            aag_file.read_ws();
            l_next[l] = aag_file.read_uint();
            aag_file.read_wsnl();
        }

        if (configs.verbose) INFO("Now reading %lu outputs\n", (size_t) header.o);
        for (uint64_t o = 0; o < header.o; o++) {
            outputs[o] = aag_file.read_uint();
            aag_file.read_wsnl();
        }

        if (configs.verbose) INFO("Now reading %lu and-gates\n", (size_t) header.a);
        for (uint64_t i = 0; i <= header.m; i++) lookup[i] = -1; // not an and-gate
        for (uint64_t a = 0; a < header.a; a++) {
            gatelhs[a] = aag_file.read_uint();
            lookup[(int)gatelhs[a] / 2] = (int) a;
            aag_file.read_ws();
            gatelft[a] = aag_file.read_uint();
            aag_file.read_ws();
            gatergt[a] = aag_file.read_uint();
            aag_file.read_wsnl();
        }
    }

    void read_header()
    {
        aag_file.read_wsnl();
        aag_file.read_token("aag");
        aag_file.read_wsnl();
        header.m = aag_file.read_uint();
        aag_file.read_ws();
        header.i = aag_file.read_uint();
        aag_file.read_ws();
        header.l = aag_file.read_uint();
        aag_file.read_ws();
        header.o = aag_file.read_uint();
        aag_file.read_ws();
        header.a = aag_file.read_uint();
        aag_file.read_ws();
        // optional
        header.b = 0;
        header.c = 0;
        header.j = 0;
        header.f = 0;
        aag_file.read_ws();
        if (aag_file.peek() != '\n') {
            header.b = aag_file.read_uint();
            aag_file.read_ws();
        }
        if (aag_file.peek() != '\n') {
            header.c = aag_file.read_uint();
            aag_file.read_ws();
        }
        if (aag_file.peek() != '\n') {
            header.j = aag_file.read_uint();
            aag_file.read_ws();
        }
        if (aag_file.peek() != '\n') {
            header.f = aag_file.read_uint();
        }
        aag_file.read_wsnl();

        // we expect one output
        if (header.o != 1) Abort("expecting 1 output\n");
        if (header.b != 0 or header.c != 0 or header.j != 0 or header.f != 0) Abort("no support for new format\n");
    }
};


#endif //SYLVAN_AIGSYNT_H
