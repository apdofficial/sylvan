/*
   Extension header to provide unit test support to LACE using UTEST.

   Created by Andrej Pištek.
   Date: 21-02-2023

   The latest version of the UTEST library is available on GitHub;
   https://github.com/sheredom/utest.h
*/

#include "utest.h"

#ifndef UTEST_LACE_H_INCLUDED
#define UTEST_LACE_H_INCLUDED

static UTEST_INLINE int utest_lace_main(int argc, const char *const argv[]);
int utest_lace_main(int argc, const char *const argv[]) {
  utest_uint64_t failed = 0;
  size_t index = 0;
  size_t *failed_testcases = UTEST_NULL;
  size_t failed_testcases_length = 0;
  const char *filter = UTEST_NULL;
  utest_uint64_t ran_tests = 0;
  int enable_mixed_units = 0;
  int random_order = 0;
  utest_uint32_t seed = 0;

  enum colours { RESET, GREEN, RED };

  const int use_colours = UTEST_COLOUR_OUTPUT();
  const char *colours[] = {"\033[0m", "\033[32m", "\033[31m"};

  if (!use_colours) {
    for (index = 0; index < sizeof colours / sizeof colours[0]; index++) {
      colours[index] = "";
    }
  }
  /* loop through all arguments looking for our options */
  for (index = 1; index < UTEST_CAST(size_t, argc); index++) {
    /* Informational switches */
    const char help_str[] = "--help";
    const char list_str[] = "--list-tests";
    /* Test config switches */
    const char filter_str[] = "--filter=";
    const char output_str[] = "--output=";
    const char enable_mixed_units_str[] = "--enable-mixed-units";
    const char random_order_str[] = "--random-order";
    const char random_order_with_seed_str[] = "--random-order=";

    if (0 == UTEST_STRNCMP(argv[index], help_str, strlen(help_str))) {
      printf("utest.h - the single file unit testing solution for C/C++!\n"
             "Command line Options:\n"
             "  --help                  Show this message and exit.\n"
             "  --filter=<filter>       Filter the test cases to run (EG. "
             "MyTest*.a would run MyTestCase.a but not MyTestCase.b).\n"
             "  --list-tests            List testnames, one per line. Output "
             "names can be passed to --filter.\n");
      printf("  --output=<output>       Output an xunit XML file to the file "
             "specified in <output>.\n"
             "  --enable-mixed-units    Enable the per-test output to contain "
             "mixed units (s/ms/us/ns).\n"
             "  --random-order[=<seed>] Randomize the order that the tests are "
             "ran in. If the optional <seed> argument is not provided, then a "
             "random starting seed is used.\n");
      goto cleanup;
    } else if (0 ==
               UTEST_STRNCMP(argv[index], filter_str, strlen(filter_str))) {
      /* user wants to filter what test cases run! */
      filter = argv[index] + strlen(filter_str);
    } else if (0 ==
               UTEST_STRNCMP(argv[index], output_str, strlen(output_str))) {
      utest_state.output = utest_fopen(argv[index] + strlen(output_str), "w+");
    } else if (0 == UTEST_STRNCMP(argv[index], list_str, strlen(list_str))) {
      for (index = 0; index < utest_state.tests_length; index++) {
        UTEST_PRINTF("%s\n", utest_state.tests[index].name);
      }
      /* when printing the test list, don't actually run the tests */
      return 0;
    } else if (0 == UTEST_STRNCMP(argv[index], enable_mixed_units_str,
                                  strlen(enable_mixed_units_str))) {
      enable_mixed_units = 1;
    } else if (0 == UTEST_STRNCMP(argv[index], random_order_with_seed_str,
                                  strlen(random_order_with_seed_str))) {
      seed =
          UTEST_CAST(utest_uint32_t,
                     strtoul(argv[index] + strlen(random_order_with_seed_str),
                             UTEST_NULL, 10));
      random_order = 1;
    } else if (0 == UTEST_STRNCMP(argv[index], random_order_str,
                                  strlen(random_order_str))) {
      const utest_int64_t ns = utest_ns();

      // Some really poor pseudo-random using the current time. I do this
      // because I really want to avoid using C's rand() because that'd mean our
      // random would be affected by any srand() usage by the user (which I
      // don't want).
      seed = UTEST_CAST(utest_uint32_t, ns >> 32) * 31 +
             UTEST_CAST(utest_uint32_t, ns & 0xffffffff);
      random_order = 1;
    }
  }

  if (random_order) {
    // Use Fisher-Yates with the Durstenfield's version to randomly re-order the
    // tests.
    for (index = utest_state.tests_length; index > 1; index--) {
      // For the random order we'll use PCG.
      const utest_uint32_t state = seed;
      const utest_uint32_t word =
          ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
      const utest_uint32_t next = ((word >> 22u) ^ word) % index;

      // Swap the randomly chosen element into the last location.
      const struct utest_test_state_s copy = utest_state.tests[index - 1];
      utest_state.tests[index - 1] = utest_state.tests[next];
      utest_state.tests[next] = copy;

      // Move the seed onwards.
      seed = seed * 747796405u + 2891336453u;
    }
  }

  for (index = 0; index < utest_state.tests_length; index++) {
    if (utest_should_filter_test(filter, utest_state.tests[index].name)) {
      continue;
    }

    ran_tests++;
  }

  printf("%s[==========]%s Running %" UTEST_PRIu64 " test cases.\n",
         colours[GREEN], colours[RESET], UTEST_CAST(utest_uint64_t, ran_tests));

  if (utest_state.output) {
    fprintf(utest_state.output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(utest_state.output,
            "<testsuites tests=\"%" UTEST_PRIu64 "\" name=\"All\">\n",
            UTEST_CAST(utest_uint64_t, ran_tests));
    fprintf(utest_state.output,
            "<testsuite name=\"Tests\" tests=\"%" UTEST_PRIu64 "\">\n",
            UTEST_CAST(utest_uint64_t, ran_tests));
  }

  for (index = 0; index < utest_state.tests_length; index++) {
    int result = 0;
    utest_int64_t ns = 0;

    if (utest_should_filter_test(filter, utest_state.tests[index].name)) {
      continue;
    }

    printf("%s[ RUN      ]%s %s\n", colours[GREEN], colours[RESET],
           utest_state.tests[index].name);

    if (utest_state.output) {
      fprintf(utest_state.output, "<testcase name=\"%s\">",
              utest_state.tests[index].name);
    }

    ns = utest_ns();
    errno = 0;
    utest_state.tests[index].func(&result, utest_state.tests[index].index);
    ns = utest_ns() - ns;

    if (utest_state.output) {
      fprintf(utest_state.output, "</testcase>\n");
    }

    // Record the failing test.
    if (0 != result) {
      const size_t failed_testcase_index = failed_testcases_length++;
      failed_testcases = UTEST_PTR_CAST(
          size_t *, utest_realloc(UTEST_PTR_CAST(void *, failed_testcases),
                                  sizeof(size_t) * failed_testcases_length));
      if (UTEST_NULL != failed_testcases) {
        failed_testcases[failed_testcase_index] = index;
      }
      failed++;
    }

    {
      const char *const units[] = {"ns", "us", "ms", "s", UTEST_NULL};
      unsigned int unit_index = 0;
      utest_int64_t time = ns;

      if (enable_mixed_units) {
        for (unit_index = 0; UTEST_NULL != units[unit_index]; unit_index++) {
          if (10000 > time) {
            break;
          }

          time /= 1000;
        }
      }

      if (0 != result) {
        printf("%s[  FAILED  ]%s %s (%" UTEST_PRId64 "%s)\n", colours[RED],
               colours[RESET], utest_state.tests[index].name, time,
               units[unit_index]);
      } else {
        printf("%s[       OK ]%s %s (%" UTEST_PRId64 "%s)\n", colours[GREEN],
               colours[RESET], utest_state.tests[index].name, time,
               units[unit_index]);
      }
    }
  }

  printf("%s[==========]%s %" UTEST_PRIu64 " test cases ran.\n", colours[GREEN],
         colours[RESET], ran_tests);
  printf("%s[  PASSED  ]%s %" UTEST_PRIu64 " tests.\n", colours[GREEN],
         colours[RESET], ran_tests - failed);

  if (0 != failed) {
    printf("%s[  FAILED  ]%s %" UTEST_PRIu64 " tests, listed below:\n",
           colours[RED], colours[RESET], failed);
    for (index = 0; index < failed_testcases_length; index++) {
      printf("%s[  FAILED  ]%s %s\n", colours[RED], colours[RESET],
             utest_state.tests[failed_testcases[index]].name);
    }
  }

  if (utest_state.output) {
    fprintf(utest_state.output, "</testsuite>\n</testsuites>\n");
  }

cleanup:
  for (index = 0; index < utest_state.tests_length; index++) {
    free(UTEST_PTR_CAST(void *, utest_state.tests[index].name));
  }

  free(UTEST_PTR_CAST(void *, failed_testcases));
  free(UTEST_PTR_CAST(void *, utest_state.tests));

  if (utest_state.output) {
    fclose(utest_state.output);
  }

  sylvan_quit();
  lace_stop();

  return UTEST_CAST(int, failed);
}


#define UTEST_TASK_0(SET, NAME)                                         \
  VOID_TASK_DECL_1(NAME, int*);                                         \
  UTEST(SET, NAME) {                                                    \
    RUN(NAME, utest_result);                                            \
  };                                                                    \
  VOID_TASK_IMPL_1(NAME, int*, utest_result)

#endif /* UTEST_LACE_H_INCLUDED */
