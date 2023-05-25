#ifndef SYLVAN_AAG_H
#define SYLVAN_AAG_H

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <boost/graph/sloan_ordering.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <span>

/**
 * Global stuff
 */
size_t pos, size;

double t_start;
#define INFO(s, ...) fprintf(stdout, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__)
#define Abort(s, ...) { fprintf(stderr, "\r[% 8.2f] " s, wctime()-t_start, ##__VA_ARGS__); exit(-1); }

/* Obtain current wallclock time */
static double
wctime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

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

typedef struct aag
{
    header_t header;
    std::vector<uint64_t> inputs;
    std::vector<uint64_t> outputs;
    std::vector<uint64_t> latches;
    std::vector<uint64_t> l_next;
    std::vector<int> lookup;
    std::vector<uint64_t> gatelhs;
    std::vector<uint64_t> gatelft;
    std::vector<uint64_t> gatergt;
} aag_t;

int parser_peek(std::span<uint8_t> buffer)
{
    if (pos == size) return EOF;
    return (int) buffer[pos];
}

void parser_skip()
{
    pos++;
}

void read_wsnl(std::span<uint8_t> buffer)
{
    while (1) {
        int c = parser_peek(buffer);
        if (c != ' ' && c != '\n' && c != '\t') return;
        parser_skip();
    }
}

void read_ws(std::span<uint8_t> buffer)
{
    while (1) {
        int c = parser_peek(buffer);
        if (c != ' ' && c != '\t') return;
        parser_skip();
    }
}

void err()
{
    Abort("File read error.");
}

int parser_read(std::span<uint8_t> buffer)
{
    if (pos == size) return EOF;
    return (int) buffer[pos++];
}

void read_token(const char *str, std::span<uint8_t> buffer)
{
    while (*str != 0) if (parser_read(buffer) != (int) (uint8_t) (*str++)) err();
}

uint64_t read_uint(std::span<uint8_t> buffer)
{
    uint64_t r = 0;
    while (1) {
        int c = parser_peek(buffer);
        if (c < '0' || c > '9') return r;
        r *= 10;
        r += c - '0';
        parser_skip();
    }
}

void read_string(std::string &s, std::span<uint8_t> buffer)
{
    s = "";
    while (true) {
        int c = parser_peek(buffer);
        if (c == EOF || c == '\n') return;
        s += (char) c;
        parser_skip();
    }
}

header_t read_header(std::span<uint8_t> buffer)
{
    header_t header{};
    read_wsnl(buffer);
    read_token("aag", buffer);
    read_ws(buffer);
    header.m = read_uint(buffer);
    read_ws(buffer);
    header.i = read_uint(buffer);
    read_ws(buffer);
    header.l = read_uint(buffer);
    read_ws(buffer);
    header.o = read_uint(buffer);
    read_ws(buffer);
    header.a = read_uint(buffer);
    read_ws(buffer);
    // optional
    header.b = 0;
    header.c = 0;
    header.j = 0;
    header.f = 0;
    read_ws(buffer);
    if (parser_peek(buffer) != '\n') {
        header.b = read_uint(buffer);
        read_ws(buffer);
    }
    if (parser_peek(buffer) != '\n') {
        header.c = read_uint(buffer);
        read_ws(buffer);
    }
    if (parser_peek(buffer) != '\n') {
        header.j = read_uint(buffer);
        read_ws(buffer);
    }
    if (parser_peek(buffer) != '\n') {
        header.f = read_uint(buffer);
    }
    read_wsnl(buffer);

    // we expect one output
    if (header.o != 1) Abort("expecting 1 output\n");
    if (header.b != 0 or header.c != 0 or header.j != 0 or header.f != 0) Abort("no support for new format\n");
    return header;
}

aag_t read_aag(std::span<uint8_t> buffer)
{
    header_t header = read_header(buffer);

    INFO("Created %lu variables\n", (size_t) header.m + 1);
    INFO("Preparing %zu inputs, %zu latches and %zu AND-gates\n", (size_t) header.i, (size_t) header.l,
         (size_t) header.a);

    aag_t aag{};
    aag.header = header;
    aag.inputs.reserve(header.i);
    aag.latches.reserve(header.l);
    aag.l_next.reserve(header.l);
    aag.outputs.reserve(header.o);
    aag.gatelhs.reserve(header.a);
    aag.gatelft.reserve(header.a);
    aag.gatergt.reserve(header.a);
    aag.lookup.reserve(header.m + 1);


    INFO("Now reading %lu inputs\n", (size_t) header.i);
    for (uint64_t i = 0; i < aag.header.i; i++) {
        aag.inputs[i] = read_uint(buffer);
        read_wsnl(buffer);
    }

    INFO("Now reading %lu latches\n", (size_t) aag.header.l);
    for (uint64_t l = 0; l < aag.header.l; l++) {
        aag.latches[l] = read_uint(buffer);
        read_ws(buffer);
        aag.l_next[l] = read_uint(buffer);
        read_wsnl(buffer);
    }

    INFO("Now reading %lu outputs\n", (size_t) aag.header.o);
    for (uint64_t o = 0; o < aag.header.o; o++) {
        aag.outputs[o] = read_uint(buffer);
        read_wsnl(buffer);
    }

    INFO("Now reading %lu and-gates\n", (size_t) aag.header.a);
    for (uint64_t i = 0; i <= aag.header.m; i++) aag.lookup[i] = -1; // not an and-gate
    for (uint64_t a = 0; a < aag.header.a; a++) {
        aag.gatelhs[a] = read_uint(buffer);
        aag.lookup[aag.gatelhs[a] / 2] = (int)a;
        read_ws(buffer);
        aag.gatelft[a] = read_uint(buffer);
        read_ws(buffer);
        aag.gatergt[a] = read_uint(buffer);
        read_wsnl(buffer);
    }

    return aag;
}


#endif //SYLVAN_AAG_H
