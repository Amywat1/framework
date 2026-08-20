/**
 * @file    slave.h
 * @brief   io_exp SDK 测试桩头文件（替代 io_exp 库，仅用于单元测试构建）
 * @note    drv_io 的后台线程在单元测试中不会启动，这些函数不会被实际调用，
 *          桩实现仅用于满足链接。
 */

#ifndef IO_EXP_SLAVE_STUB_H
#define IO_EXP_SLAVE_STUB_H

int io_online_get(int id);
int io_read_input_s(int id);
int io_write_all_s(int id, int val);

#endif /* IO_EXP_SLAVE_STUB_H */
