/**
 * @file    snack_runtime.cpp
 * @brief   Snack 进程运行时细粒度接口实现
 */

#include "framework/adapters/runtime/snack/snack_sdk.h"

extern void set_log_level(int type);
extern void set_remote_port(int port);

void snack_runtime_set_log_level(int type)
{
    set_log_level(type);
}

void snack_runtime_set_remote_port(int port)
{
    set_remote_port(port);
}
