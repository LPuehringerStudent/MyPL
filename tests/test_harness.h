#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static const char* current_test_name = NULL;
static int current_test_failed = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name)                                          \
    do {                                                        \
        current_test_name = #name;                              \
        current_test_failed = 0;                                \
        tests_run++;                                            \
        name();                                                 \
        if (!current_test_failed) {                             \
            tests_passed++;                                     \
            printf("  PASS: %s\n", #name);                      \
        }                                                       \
    } while (0)

#define FAIL(message)                                           \
    do {                                                        \
        current_test_failed = 1;                                \
        tests_failed++;                                         \
        fprintf(stderr,                                         \
                "  FAIL: %s at %s:%d: %s\n",                   \
                current_test_name, __FILE__, __LINE__, message); \
        return;                                                 \
    } while (0)

#define ASSERT(condition)                                       \
    do {                                                        \
        if (!(condition)) {                                     \
            FAIL(#condition);                                   \
        }                                                       \
    } while (0)

#define ASSERT_INT_EQ(expected, actual)                         \
    do {                                                        \
        int _expected = (expected);                             \
        int _actual = (actual);                                 \
        if (_expected != _actual) {                             \
            current_test_failed = 1;                            \
            tests_failed++;                                     \
            fprintf(stderr,                                     \
                    "  FAIL: %s at %s:%d: expected %d, got %d\n", \
                    current_test_name, __FILE__, __LINE__,      \
                    _expected, _actual);                        \
            return;                                             \
        }                                                       \
    } while (0)

#define ASSERT_FLOAT_EQ(expected, actual)                       \
    do {                                                        \
        double _expected = (expected);                          \
        double _actual = (actual);                              \
        if (_expected != _actual) {                             \
            current_test_failed = 1;                            \
            tests_failed++;                                     \
            fprintf(stderr,                                     \
                    "  FAIL: %s at %s:%d: expected %f, got %f\n", \
                    current_test_name, __FILE__, __LINE__,      \
                    _expected, _actual);                        \
            return;                                             \
        }                                                       \
    } while (0)

#define ASSERT_STRING_EQ(expected, actual)                      \
    do {                                                        \
        const char* _expected = (expected);                     \
        const char* _actual = (actual);                         \
        if (strcmp(_expected, _actual) != 0) {                  \
            current_test_failed = 1;                            \
            tests_failed++;                                     \
            fprintf(stderr,                                     \
                    "  FAIL: %s at %s:%d: expected '%s', got '%s'\n", \
                    current_test_name, __FILE__, __LINE__,      \
                    _expected, _actual);                        \
            return;                                             \
        }                                                       \
    } while (0)

#define ASSERT_PTR_NULL(ptr)                                    \
    do {                                                        \
        if ((ptr) != NULL) {                                    \
            FAIL(#ptr " should be NULL");                       \
        }                                                       \
    } while (0)

#define ASSERT_PTR_NOT_NULL(ptr)                                \
    do {                                                        \
        if ((ptr) == NULL) {                                    \
            FAIL(#ptr " should not be NULL");                   \
        }                                                       \
    } while (0)

#define ASSERT_PTR_EQ(expected, actual)                         \
    do {                                                        \
        const void* _expected = (expected);                     \
        const void* _actual = (actual);                         \
        if (_expected != _actual) {                             \
            current_test_failed = 1;                            \
            tests_failed++;                                     \
            fprintf(stderr,                                     \
                    "  FAIL: %s at %s:%d: expected pointer %p, got %p\n", \
                    current_test_name, __FILE__, __LINE__,      \
                    _expected, _actual);                        \
            return;                                             \
        }                                                       \
    } while (0)

#define TEST_SUMMARY()                                          \
    do {                                                        \
        printf("\n%d run, %d passed, %d failed\n",              \
               tests_run, tests_passed, tests_failed);          \
        return tests_failed == 0 ? 0 : 1;                       \
    } while (0)

#endif
