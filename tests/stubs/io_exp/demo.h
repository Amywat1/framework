/**
 * @file    demo.h
 * @brief   io_exp SDK 脉冲/ADC 接口测试桩（替代 io_exp/demo.h 库头文件）
 * @note    桩声明用于单元测试编译与链接；真机构建改用 SDK 头文件。
 */

#ifndef IO_EXP_DEMO_STUB_H
#define IO_EXP_DEMO_STUB_H

int          io_pluse_read(int board_id, int pin_id);
int          io_SDO_write(int board_id, int index, int sub_index, int *data);
int          io_adc_read(int board_id, int port);
int          io_adc_mV(int board_id, int port);
int          io_adc_mA(int board_id, int port);
int          io_init(const char *can_bus, int can_baud, int self_node, int board_count);
void         io_logApi_set(int (*cb)(const char *fmt, ...));
unsigned int io_read_input(int board_id);
int          io_write_all(int board_id, int value);

#endif /* IO_EXP_DEMO_STUB_H */
