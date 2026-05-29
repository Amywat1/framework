#ifndef SHARED_RESULT_TYPES_H
#define SHARED_RESULT_TYPES_H

#include "shared/error_codes.h"
#include <stdbool.h>

/**
 * @file result_types.h
 * @brief 定义通用结果结构。
 */

typedef struct
{
    bool ok;
    error_code_t error_code;
} operation_result_t;

typedef struct
{
    bool accepted;
    char reject_reason[64];
} request_decision_t;

static inline operation_result_t operation_result_ok(void)
{
    operation_result_t result = {true, ERROR_CODE_OK};
    return result;
}

static inline operation_result_t operation_result_fail(error_code_t error_code)
{
    operation_result_t result = {false, error_code};
    return result;
}

#endif
