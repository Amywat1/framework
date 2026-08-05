#ifndef TESTS_SUPPORT_WDF_TEST_SPEC_H
#define TESTS_SUPPORT_WDF_TEST_SPEC_H

#include "unity.h"

#include <stdio.h>
#include <unistd.h>

#define WDF_TEST_SPEC_LINE_CAPACITY 512U

/**
 * @brief 运行测试并输出结构化行为说明。
 *
 * @param test_func Unity 测试函数。
 * @param behaviour 行为契约编号；非契约测试可传空字符串。
 * @param summary 中文测试目的。
 */
#define WDF_RUN_TEST(test_func, behaviour, summary)                                                                    \
    do {                                                                                                               \
        const unsigned int wdf_failures_before = Unity.TestFailures;                                                   \
        const unsigned int wdf_ignores_before  = Unity.TestIgnores;                                                    \
        const char        *wdf_result          = "passed";                                                             \
        char               wdf_line[WDF_TEST_SPEC_LINE_CAPACITY];                                                      \
        int                wdf_length;                                                                                 \
        RUN_TEST(test_func);                                                                                           \
        if (Unity.TestFailures > wdf_failures_before) {                                                                \
            wdf_result = "failed";                                                                                     \
        } else if (Unity.TestIgnores > wdf_ignores_before) {                                                           \
            wdf_result = "skipped";                                                                                    \
        }                                                                                                              \
        wdf_length = snprintf(                                                                                         \
            wdf_line, sizeof(wdf_line), "WDFSPEC\t%s\t%s\t%s\t%s\n", (behaviour), (summary), #test_func, wdf_result);  \
        if ((wdf_length > 0) && ((size_t)wdf_length < sizeof(wdf_line))) {                                             \
            const ssize_t wdf_written = write(STDERR_FILENO, wdf_line, (size_t)wdf_length);                            \
            if (wdf_written != (ssize_t)wdf_length) {                                                                  \
                fprintf(stderr, "WDFSPEC_ERROR\t%s\twrite failed\n", #test_func);                                      \
            }                                                                                                          \
        } else {                                                                                                       \
            fprintf(stderr, "WDFSPEC_ERROR\t%s\tline too long\n", #test_func);                                         \
        }                                                                                                              \
    } while (0)

#endif
