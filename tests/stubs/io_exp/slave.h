/**
 * @file    slave.h
 * @brief   io_exp SDK 测试桩头文件（替代 io_exp 库，仅用于单元测试构建）
 * @note    单元测试会启动 I/O worker；这些 SDK 入口由 worker 串行调用，
 *          桩负责注入在线状态、输入位图和读写失败。
 */

#ifndef IO_EXP_SLAVE_STUB_H
#define IO_EXP_SLAVE_STUB_H

int io_online_get(int id);
int io_read_input_s(int id);
int io_write_all_s(int id, int val);

#endif /* IO_EXP_SLAVE_STUB_H */
