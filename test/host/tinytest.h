#pragma once

// A minimal, dependency-free test harness for the host-compiled tests in
// this directory. No vendored test framework -- this project has none,
// and pulling one in just for a handful of pure-logic functions was not
// worth it. Each test_*.c file includes this, defines TEST(name){...}
// blocks, and calls RUN_TEST(name) for each from its own main(). See
// README.md.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;
static int g_checks_failed_in_current_test = 0;
static const char *g_current_test = "";

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name)                             \
    do {                                           \
        g_current_test = #name;                    \
        g_checks_failed_in_current_test = 0;       \
        g_tests_run++;                             \
        if (getenv("TINYTEST_VERBOSE")) {          \
            printf("RUN %s\n", #name);             \
            fflush(stdout);                        \
        }                                          \
        test_##name();                             \
        if (g_checks_failed_in_current_test > 0) { \
            g_tests_failed++;                      \
        }                                          \
    } while (0)

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            printf("  FAIL  %s: %s:%d: %s\n", g_current_test, __FILE__, __LINE__, #cond); \
            g_checks_failed_in_current_test++;                                            \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)    CHECK((a) == (b))
#define CHECK_STREQ(a, b) CHECK(strcmp((a), (b)) == 0)

// Call at the end of main(). Prints a summary and returns the process
// exit code (0 = every test passed, 1 = at least one failed).
static int tinytest_summary(void)
{
    printf("%d/%d tests passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
