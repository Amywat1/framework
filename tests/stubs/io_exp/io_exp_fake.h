#ifndef TESTS_STUBS_IO_EXP_IO_EXP_FAKE_H
#define TESTS_STUBS_IO_EXP_IO_EXP_FAKE_H

#include <stdbool.h>

void        io_exp_fake_reset(void);
void        io_exp_fake_set_init_result(int result);
const char *io_exp_fake_can_bus(void);
int         io_exp_fake_can_baud(void);
int         io_exp_fake_self_node(void);
int         io_exp_fake_board_count(void);
bool        io_exp_fake_log_api_set(void);
void        io_exp_fake_set_online(int board_id, int online);
void        io_exp_fake_set_input(int board_id, int input);
int         io_exp_fake_output(int board_id);
void        io_exp_fake_set_pdo_write_result(int result);
void        io_exp_fake_set_sdo_write_result(int result);
void        io_exp_fake_set_sdo_read_error(int result);
unsigned    io_exp_fake_pdo_read_count(void);
unsigned    io_exp_fake_sdo_read_count(void);
unsigned    io_exp_fake_pdo_write_count(void);
unsigned    io_exp_fake_sdo_write_count(void);
bool        io_exp_fake_sdk_thread_seen(void);
bool        io_exp_fake_sdk_thread_consistent(void);
void        io_exp_fake_set_pulse(int board_id, int pin_id, int value);
void        io_exp_fake_set_adc(int board_id, int port, int raw, int mv, int ma);
void        io_exp_fake_set_sdo_result(int result);
bool        io_exp_fake_sdo_called(void);
int         io_exp_fake_sdo_board(void);
int         io_exp_fake_sdo_index(void);
int         io_exp_fake_sdo_sub_index(void);
int         io_exp_fake_sdo_data(void);
/** @brief 最近一次 `sdo_write` / `io_SDO_write` 的数据字节数。 */
int         io_exp_fake_sdo_len(void);
/** @brief 最近一次 `sdo_write` 的 CANopen 数据类型。 */
int         io_exp_fake_sdo_datatype(void);
/** @brief 统计写入 0x2006 的次数。 */
unsigned    io_exp_fake_filter_sdo_count(void);
/** @brief 最近一次 0x2006 写入的子板号。 */
int         io_exp_fake_filter_sdo_board(void);
/** @brief 最近一次 0x2006 写入的引脚号。 */
int         io_exp_fake_filter_sdo_pin(void);
/** @brief 最近一次 0x2006 写入的滤波时间。 */
int         io_exp_fake_filter_sdo_data(void);

#endif /* TESTS_STUBS_IO_EXP_IO_EXP_FAKE_H */
