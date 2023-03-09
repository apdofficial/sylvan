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

TASK_0(int, run)
{
    for (size_t i = 0; i < 100000000; i += 1) {
        clock_t start = clock();
        for (size_t j = 0; j < 100000; ++j) {
            mtbdd_ithvar(i);
            i += 1;
        }
        double usedBuckets = llmsset_count_marked(nodes);
        size_t allBuckets = llmsset_get_size(nodes);
        double usage = (usedBuckets/allBuckets)*100;
        printf("table usage %.1f%% | runtime: %.2zuns\n", usage, clock_ns_elapsed(start));
    }
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    // Init Lace
    lace_start(4, 1000000); // auto-detect number of workers, use a 1,000,000 size task queue

    long long memoryCap = 1LL*1024LLU*1024LLU*1024LLU;
    char buf[32];
    to_h(memoryCap, buf);
    printf("Setting Sylvan main tables memory to %s\n", buf);

    sylvan_set_limits(memoryCap, 1, 0);
    sylvan_init_package();
    sylvan_init_mtbdd();
    sylvan_init_reorder();
    sylvan_gc_disable();

    int res = RUN(run);

    sylvan_stats_report(stdout);

    lace_stop();

    return res;
}