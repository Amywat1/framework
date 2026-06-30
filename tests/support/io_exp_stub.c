/**
 * @file    io_exp_stub.c
 * @brief   io_exp SDK 函数桩实现（用于 drv_io 单元测试）
 * @note    drv_io 的后台线程在单元测试中不启动（不调用 drv_io_start），
 *          这些函数不会被实际执行，仅用于满足链接器需求。
 */

#include "io_exp/slave.h"

int io_online_get(int id)
{
    (void)id;
    return 0;
}

int io_read_input_s(int id)
{
    (void)id;
    return 0;
}

void io_write_all_s(int id, int val)
{
    (void)id;
    (void)val;
}
