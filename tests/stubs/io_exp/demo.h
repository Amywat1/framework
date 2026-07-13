/**
 * @file    demo.h
 * @brief   io_exp SDK 脉冲计数接口测试桩（替代 io_exp/demo.h 库头文件）
 * @note    drv_io 单元测试不启动后台线程，这两个函数不会被实际调用，
 *          桩声明仅用于满足编译器。
 */

#ifndef IO_EXP_DEMO_STUB_H
#define IO_EXP_DEMO_STUB_H

int  io_pluse_read(int board_id, int pin_id);
int  io_SDO_write(int board_id, int index, int sub_index, int *data);
int  io_init(const char *can_bus, int can_baud, int self_node, int board_count);
void io_logApi_set(int (*cb)(const char *fmt, ...));

#endif /* IO_EXP_DEMO_STUB_H */
