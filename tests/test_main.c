/**
 * WASI Test Suite Main Runner
 *
 * This file provides the main entry point for running all WASI tests.
 *
 * Build: make test
 * Run:   ./build/test_wasi
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* External test suite declarations */
/* Each test file should provide a run_*_tests() function */
extern int run_io_tests(void);
extern int run_random_tests(void);
extern int run_clocks_tests(void);
extern int run_filesystem_tests(void);
extern int run_sockets_tests(void);
extern int run_cli_tests(void);

/* Test suite registration */
typedef struct {
    const char *name;
    int (*run)(void);
} test_suite_t;

static test_suite_t test_suites[] = {
    /* Uncomment as test suites are implemented */
    // {"io",         run_io_tests},
    // {"random",     run_random_tests},
    // {"clocks",     run_clocks_tests},
    // {"filesystem", run_filesystem_tests},
    // {"sockets",    run_sockets_tests},
    // {"cli",        run_cli_tests},
    {NULL, NULL}  /* Sentinel */
};

static void print_usage(const char *prog) {
    printf("Usage: %s [suite...]\n", prog);
    printf("\n");
    printf("Run WASI implementation tests.\n");
    printf("\n");
    printf("Available test suites:\n");
    for (int i = 0; test_suites[i].name != NULL; i++) {
        printf("  %s\n", test_suites[i].name);
    }
    printf("\n");
    printf("If no suite is specified, all tests are run.\n");
}

static int run_suite(const char *name) {
    for (int i = 0; test_suites[i].name != NULL; i++) {
        if (strcmp(test_suites[i].name, name) == 0) {
            printf("\n=== Running %s tests ===\n", name);
            return test_suites[i].run();
        }
    }
    fprintf(stderr, "Unknown test suite: %s\n", name);
    return -1;
}

int main(int argc, char *argv[]) {
    printf("WASI Implementation Test Suite\n");
    printf("==============================\n");

    int total_failures = 0;

    if (argc > 1) {
        /* Run specific suites */
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        for (int i = 1; i < argc; i++) {
            int result = run_suite(argv[i]);
            if (result < 0) {
                return 1;  /* Unknown suite */
            }
            total_failures += result;
        }
    } else {
        /* Run all suites */
        int suites_run = 0;
        for (int i = 0; test_suites[i].name != NULL; i++) {
            printf("\n=== Running %s tests ===\n", test_suites[i].name);
            total_failures += test_suites[i].run();
            suites_run++;
        }

        if (suites_run == 0) {
            printf("\nNo test suites are currently enabled.\n");
            printf("Implement tests and register them in test_main.c.\n");
            return 0;
        }
    }

    printf("\n==============================\n");
    if (total_failures == 0) {
        printf("All tests passed!\n");
    } else {
        printf("Tests failed: %d\n", total_failures);
    }

    return total_failures > 0 ? 1 : 0;
}
