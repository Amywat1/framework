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
void        io_exp_fake_set_pulse(int board_id, int pin_id, int value);
void        io_exp_fake_set_sdo_result(int result);
bool        io_exp_fake_sdo_called(void);
int         io_exp_fake_sdo_board(void);
int         io_exp_fake_sdo_index(void);
int         io_exp_fake_sdo_sub_index(void);
int         io_exp_fake_sdo_data(void);

#endif /* TESTS_STUBS_IO_EXP_IO_EXP_FAKE_H */
