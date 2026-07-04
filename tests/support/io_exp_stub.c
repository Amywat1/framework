/**
 * @file    io_exp_stub.c
 * @brief   io_exp SDK 函数桩实现（用于 drv_io 单元测试）
 * @note    drv_io 的后台线程在单元测试中不启动（不调用 drv_io_start），
 *          这些函数不会被实际执行，仅用于满足链接器需求。
 */

#include "io_exp/slave.h"
#include "io_exp/demo.h"

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

int io_pluse_read(int board_id, int pin_id)
{
    (void)board_id;
    (void)pin_id;
    return -1;
}

int io_SDO_write(int board_id, int index, int sub_index, int *data)
{
    (void)board_id;
    (void)index;
    (void)sub_index;
    (void)data;
    return 0;
}

int io_init(int can_bus, int can_baud, int self_node, int board_count)
{
    (void)can_bus;
    (void)can_baud;
    (void)self_node;
    (void)board_count;
    return 0;
}

void io_logApi_set(int (*cb)(const char *fmt, ...))
{
    (void)cb;
}
