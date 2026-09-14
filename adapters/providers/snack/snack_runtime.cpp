/**
 * @file    snack_runtime.cpp
 * @brief   Snack 进程运行时细粒度接口实现
 */

#include "framework/adapters/providers/snack/snack_sdk.h"

extern void set_log_level(int type);

void snack_runtime_set_log_level(int type)
{
    set_log_level(type);
}
