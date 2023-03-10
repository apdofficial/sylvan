#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <sylvan_int.h>

static char* to_h(double size, char *buf)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    int i = 0;
    for (;size>1024;size/=1024) i++;
    sprintf(buf, "%.*f %s", i, size, units[i]);
    return buf;
}

int compare_float(const void * a, const void * b)
{
  return ( *(float*)a - *(float*)b );
}


TASK_0(int, run)
{
    size_t rounds = 10;
    size_t samples_per_round = 400; // expected max

    float runtimes[rounds][samples_per_round];
    float usages[rounds][samples_per_round];

    for (size_t round = 0; round < rounds; ++round){
        printf("round %zu\n", round);
        size_t sample = 0;
        for (size_t j = 0; j < 50000000; ++j) {
            clock_t start = clock();
            for (size_t k = 0; k < 40000; ++k) {
                mtbdd_ithvar(j);
                j++;
            }

            float used = llmsset_count_marked(nodes);
            float all = llmsset_get_size(nodes);
            float usage = (used/all)*100;
            if (usage >= 99.5) break;

            if (round == 0) continue; // warm up round
            
            usages[round-1][sample] = usage;
            runtimes[round-1][sample] = (float)clock_ms_elapsed(start);
            // printf("r %zu | s %zu | table usage %.2f%% | runtime: %.2fns\n", round, sample,  usages[round][sample], runtimes[round][sample]);
            sample++;
        }
        sylvan_quit();

        sylvan_set_limits(2LL*1024LLU*1024LLU*1024LLU, 1, 2);
        sylvan_init_package();
        sylvan_init_mtbdd();
        
        sylvan_gc_enable();
        sylvan_gc();
        sylvan_gc_disable();

    }

    FILE *fpt1;
    fpt1 = fopen("./../../hashmap_cmp_raw.csv", "w+"); // assume cwd is examples 
    for (size_t round = 0; round < rounds; round++){
        for (size_t sample = 0; sample < samples_per_round; sample++) {
            if(runtimes[round][sample] == 0.0f) continue;
            fprintf(fpt1, "%zu, %.5f, %.5f\n",round, usages[round][sample], runtimes[round][sample]);
        }
    }
    fclose(fpt1);

    float usage_medians[samples_per_round];

    for (size_t sample = 0; sample < samples_per_round; sample++){
        // collect all rounds for a given sample
        float usage_flatten[samples_per_round];
        for (size_t round = 0; round < rounds; round++){
            if(usages[round][sample] == 0.0f) continue;
            usage_flatten[round] = usages[round][sample];
        }
        
        // sort the array
        qsort(usage_flatten, rounds, sizeof(float), compare_float);

        printf("s %zu at %zu | median %.4f\n", sample, rounds/2,  usage_flatten[rounds/2]);
        // pick sample from the middle of the distribution
        usage_medians[sample] = usage_flatten[rounds/2];
    }

    float runtime_medians[samples_per_round];

    for (size_t sample = 0; sample < samples_per_round; sample++){
        // collect all rounds for a given sample
        float runtime_flatten[samples_per_round];
        
        for (size_t round = 0; round < rounds; round++){
            if(usages[round][sample] == 0.0f) continue;
            runtime_flatten[round] = runtimes[round][sample];
        }
        // sort the array
        qsort(runtime_flatten, rounds, sizeof(float), compare_float);

        // pick sample from the middle of the distribution
        runtime_medians[sample] = runtime_flatten[rounds/2];
    }

    FILE *fpt2;
    fpt2 = fopen("./../../hashmap_cmp_medians.csv", "w+"); // assume cwd is examples 
    for (size_t sample = 0; sample < samples_per_round; sample++) {
        if(usage_medians[sample] == 0.0f) continue;
        fprintf(fpt2, "%.5f, %.5f\n", usage_medians[sample], runtime_medians[sample]);
    }
    fclose(fpt2);

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    // Init Lace
    lace_start(4, 1000000); // auto-detect number of workers, use a 1,000,000 size task queue

    uint64_t memoryCap = 2LL*1024LLU*1024LLU*1024LLU;
    char buf[32];
    to_h(memoryCap, buf);
    printf("Setting Sylvan main tables memory to %s\n", buf);

    sylvan_set_limits(memoryCap, 1, 2);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();
    sylvan_gc_disable();

    int res = RUN(run);

    sylvan_stats_report(stdout);

    lace_stop();

    return res;
}