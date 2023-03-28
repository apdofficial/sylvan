#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "sylvan_int.h"

#define WORKERS 4

void sylvan_setup(uint64_t memoryCap)
{
    sylvan_set_limits(memoryCap, 1, 2);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_gc_disable();
}

int compare_float(const void * a, const void * b)
{
  return ( *(float*)a - *(float*)b );
}

VOID_TASK_2(create_variables, size_t, start, size_t, end)
{
    for (; start < end; ++start) {
        MTBDD v = mtbdd_ithvar(start);
        if (v == mtbdd_invalid) {
            printf("table is full\n");
            break;
        }
    }
}

TASK_0(int, run)
{
    size_t rounds = 10;
    size_t samples_per_round = 50; // expected max

    float runtimes[rounds][samples_per_round];
    float usages[rounds][samples_per_round];
    float runtime_medians[samples_per_round];
    float usage_medians[samples_per_round];

    // run the benchmark
    for (size_t round = 0; round < rounds; ++round){
        printf("round %zu\n", round);
        sylvan_setup(2LL*1024LLU*1024LLU*1024LLU);
        size_t sample = 0;

        for (size_t index = 0; index < 50000000;) {
            size_t step = 50000; // number of variables created per LACE task

            clock_t start = clock();

            for (size_t i = 0; i < WORKERS; ++i){
                SPAWN(create_variables, index, index+step);
                index += step;
            }

            for (size_t i = 0; i < WORKERS; ++i) SYNC(create_variables);

            float runtime = clock_ms_elapsed(start);

            float used = llmsset_count_marked(nodes);
            float all = llmsset_get_size(nodes);
            float usage = (used/all)*100;
            if (usage >= 97.7) break;

            if (round == 0) continue; // warm up round
            if (sample > samples_per_round) break;

            printf("r %zu | s %zu | table usage %.2f%% | runtime: %.2fns\n", round, sample, usage, runtime);

            usages[round-1][sample] = usage;
            runtimes[round-1][sample] = runtime;
            sample++;
        }

        sylvan_quit();
    }

    // write the raw data into a csv
    FILE *file;
    file = fopen("./par_hashmap_chaining_raw.csv", "w+");
    fprintf(file, "round,usages,runtimes\n");
    for (size_t round = 0; round < rounds; round++){
        for (size_t sample = 0; sample < samples_per_round; sample++) {
            if (usages[round][sample] == 0.0f) break;
            fprintf(file, "%zu,%.2f,%.2f\n", round, usages[round][sample], runtimes[round][sample]);
        }
    }
    fclose(file);

    // calculate median for each sample
    for (size_t sample = 0; sample < samples_per_round; sample++){
        // collect all rounds for a given sample
        float flatten[samples_per_round];
        for (size_t round = 0; round < rounds; round++){
            if(runtimes[round][sample] == 0.0f) break;
            flatten[round] = runtimes[round][sample];
        }
        // sort the array
        qsort(flatten, rounds, sizeof(float), compare_float);
        // pick sample from the middle of the distribution
        runtime_medians[sample] = flatten[rounds/2];
    }

    // calculate median for each sample
    for (size_t sample = 0; sample < samples_per_round; sample++){
        // collect all rounds for a given sample
        float flatten[samples_per_round];
        for (size_t round = 0; round < rounds; round++){
            if(usages[round][sample] == 0.0f) continue; // skip empty rows
            flatten[round] = usages[round][sample];
        }
        // sort the array
        qsort(flatten, rounds, sizeof(float), compare_float);
        // pick sample from the middle of the distribution
        usage_medians[sample] = flatten[rounds/2];
    }

    // write the medians into a csv
    file = fopen("./par_hashmap_chaining_medians.csv", "w+");
    fprintf(file, "usages,runtimes\n");
    for (size_t sample = 0; sample < samples_per_round; sample++) {
        fprintf(file, "%.2f,%.2f\n", usage_medians[sample], runtime_medians[sample]);
    }
    fclose(file);

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    lace_start(WORKERS, 100000000);
    sylvan_gc_disable();
    int res = RUN(run);
    lace_stop();
    return res;
}