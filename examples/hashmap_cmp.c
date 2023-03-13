#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "sylvan_int.h"

void sylvan_setup(uint64_t memoryCap){
    sylvan_set_limits(memoryCap, 1, 2);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_gc_disable();
}

int compare_float(const void * a, const void * b)
{
  return ( *(float*)a - *(float*)b );
}

void calculate_medians(size_t samples_per_round, size_t rounds, float medians[samples_per_round], float data[rounds][samples_per_round]){
    for (size_t sample = 0; sample < samples_per_round; sample++){
        // collect all rounds for a given sample
        float flatten[samples_per_round];
        
        for (size_t round = 0; round < rounds; round++){
            if(data[round][sample] == 0.0f) continue; // skip empty rows
            flatten[round] = data[round][sample];
        }
        // sort the array
        qsort(flatten, rounds, sizeof(float), compare_float);

        // pick sample from the middle of the distribution
        medians[sample] = flatten[rounds/2];
    }
}


TASK_0(int, run)
{
    size_t rounds = 10;
    size_t samples_per_round = 500; // expected max

    float runtimes[rounds][samples_per_round];
    float usages[rounds][samples_per_round];
    float runtime_medians[samples_per_round];
    float usage_medians[samples_per_round];

    // run the benchmark
    for (size_t round = 0; round < rounds; ++round){
        printf("round %zu\n", round);
        sylvan_setup(1LL*1024LLU*1024LLU*1024LLU);
        size_t sample = 0;

        for (size_t j = 0; j < 50000000; ++j) {       

            clock_t start = clock();
            for (size_t k = 0; k < 10000; ++k) {
                MTBDD v = mtbdd_ithvar(j);
                if (v == mtbdd_invalid) {
                    printf("table is full\n");
                    break;
                }
                j++;
            }
            float runtime = clock_ms_elapsed(start);

            float used = llmsset_count_marked(nodes);
            float all = llmsset_get_size(nodes);
            float usage = (used/all)*100;
            if (usage >= 99.5) break;
            
            // printf("r %zu | s %zu | table usage %.2f%% | runtime: %.2fns\n", round, sample, usage, runtime);

            if (round == 0) continue; // warm up round

            usages[round-1][sample] = usage;
            runtimes[round-1][sample] = runtime;
            sample++;
        }

        sylvan_quit();
    }

    // write the raw data into a csv
    FILE *file;
    file = fopen("./hashmap_chaining_raw.csv", "w+");
    fprintf(file, "round, usages, runtimes_ms\n");
    for (size_t round = 0; round < rounds; round++){
        for (size_t sample = 0; sample < samples_per_round; sample++) {
            if(usages[round][sample] == 0.0f) break;
            fprintf(file, "%zu, %.5f, %.5f\n", round, usages[round][sample], runtimes[round][sample]);
        }
    }
    fclose(file);

    calculate_medians(samples_per_round, rounds, runtime_medians, usages);
    calculate_medians(samples_per_round, rounds, usage_medians, runtimes);

    // write the medians into a csv
    file = fopen("./hashmap_chaining_medians.csv", "w+");
    fprintf(file, "usages, runtimes_ms\n");
    for (size_t sample = 0; sample < samples_per_round; sample++) {
         if(usage_medians[sample] == 0.0f) break;
        fprintf(file, "%.5f, %.5f\n", usage_medians[sample], runtime_medians[sample]);
    }
    fclose(file);

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    // Init Lace
    lace_start(4, 1000000); // auto-detect number of workers, use a 1,000,000 size task queue

    int res = RUN(run);

    lace_stop();

    return res;
}