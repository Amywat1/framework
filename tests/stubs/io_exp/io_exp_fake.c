#include "io_exp/demo.h"
#include "io_exp/slave.h"

#include <stdbool.h>
#include <string.h>

#define IO_EXP_FAKE_BOARD_MAX 8
#define IO_EXP_FAKE_PIN_MAX   32
#define IO_EXP_FAKE_ADC_PORT_MAX 4
#define IO_EXP_FAKE_ADC_NOT_INIT (-99)

typedef struct {
    int         init_result;
    const char *can_bus;
    int         can_baud;
    int         self_node;
    int         board_count;
    int (*log_cb)(const char *fmt, ...);

    int  online[IO_EXP_FAKE_BOARD_MAX];
    int  input[IO_EXP_FAKE_BOARD_MAX];
    int  output[IO_EXP_FAKE_BOARD_MAX];
    int  pulse[IO_EXP_FAKE_BOARD_MAX][IO_EXP_FAKE_PIN_MAX + 1];
    int  adc_raw[IO_EXP_FAKE_BOARD_MAX][IO_EXP_FAKE_ADC_PORT_MAX + 1];
    int  adc_mv[IO_EXP_FAKE_BOARD_MAX][IO_EXP_FAKE_ADC_PORT_MAX + 1];
    int  adc_ma[IO_EXP_FAKE_BOARD_MAX][IO_EXP_FAKE_ADC_PORT_MAX + 1];
    bool adc_ready[IO_EXP_FAKE_BOARD_MAX];
    int  sdo_result;
    int  sdo_board;
    int  sdo_index;
    int  sdo_sub_index;
    int  sdo_data;
    bool sdo_called;
} io_exp_fake_t;

static io_exp_fake_t s_fake;

void io_exp_fake_reset(void)
{
    memset(&s_fake, 0, sizeof(s_fake));
    s_fake.init_result = 0;
    s_fake.sdo_result  = 0;
}

void io_exp_fake_set_init_result(int result)
{
    s_fake.init_result = result;
}

const char *io_exp_fake_can_bus(void)
{
    return s_fake.can_bus;
}

int io_exp_fake_can_baud(void)
{
    return s_fake.can_baud;
}

int io_exp_fake_self_node(void)
{
    return s_fake.self_node;
}

int io_exp_fake_board_count(void)
{
    return s_fake.board_count;
}

bool io_exp_fake_log_api_set(void)
{
    return s_fake.log_cb != NULL;
}

void io_exp_fake_set_online(int board_id, int online)
{
    if ((board_id > 0) && (board_id < IO_EXP_FAKE_BOARD_MAX)) {
        s_fake.online[board_id] = online;
    }
}

void io_exp_fake_set_input(int board_id, int input)
{
    if ((board_id > 0) && (board_id < IO_EXP_FAKE_BOARD_MAX)) {
        s_fake.input[board_id] = input;
    }
}

int io_exp_fake_output(int board_id)
{
    if ((board_id <= 0) || (board_id >= IO_EXP_FAKE_BOARD_MAX)) {
        return 0;
    }
    return s_fake.output[board_id];
}

void io_exp_fake_set_pulse(int board_id, int pin_id, int value)
{
    if ((board_id > 0) && (board_id < IO_EXP_FAKE_BOARD_MAX) && (pin_id > 0) && (pin_id <= IO_EXP_FAKE_PIN_MAX)) {
        s_fake.pulse[board_id][pin_id] = value;
    }
}

void io_exp_fake_set_adc(int board_id, int port, int raw, int mv, int ma)
{
    if ((board_id <= 0) || (board_id >= IO_EXP_FAKE_BOARD_MAX) || (port <= 0)
        || (port > IO_EXP_FAKE_ADC_PORT_MAX)) {
        return;
    }

    s_fake.adc_raw[board_id][port] = raw;
    s_fake.adc_mv[board_id][port]  = mv;
    s_fake.adc_ma[board_id][port]  = ma;
    s_fake.adc_ready[board_id]     = true;
}

void io_exp_fake_set_sdo_result(int result)
{
    s_fake.sdo_result = result;
}

bool io_exp_fake_sdo_called(void)
{
    return s_fake.sdo_called;
}

int io_exp_fake_sdo_board(void)
{
    return s_fake.sdo_board;
}

int io_exp_fake_sdo_index(void)
{
    return s_fake.sdo_index;
}

int io_exp_fake_sdo_sub_index(void)
{
    return s_fake.sdo_sub_index;
}

int io_exp_fake_sdo_data(void)
{
    return s_fake.sdo_data;
}

int io_online_get(int id)
{
    if ((id <= 0) || (id >= IO_EXP_FAKE_BOARD_MAX)) {
        return 0;
    }
    return s_fake.online[id];
}

int io_read_input_s(int id)
{
    if ((id <= 0) || (id >= IO_EXP_FAKE_BOARD_MAX)) {
        return 0;
    }
    return s_fake.input[id];
}

void io_write_all_s(int id, int val)
{
    if ((id > 0) && (id < IO_EXP_FAKE_BOARD_MAX)) {
        s_fake.output[id] = val;
    }
}

int io_pluse_read(int board_id, int pin_id)
{
    if ((board_id <= 0) || (board_id >= IO_EXP_FAKE_BOARD_MAX) || (pin_id <= 0) || (pin_id > IO_EXP_FAKE_PIN_MAX)) {
        return -1;
    }
    return s_fake.pulse[board_id][pin_id];
}

static int io_exp_fake_adc_get(int board_id, int port, const int values[][IO_EXP_FAKE_ADC_PORT_MAX + 1])
{
    if ((board_id <= 0) || (board_id >= IO_EXP_FAKE_BOARD_MAX) || (port <= 0)
        || (port > IO_EXP_FAKE_ADC_PORT_MAX)) {
        return -1;
    }
    if (!s_fake.adc_ready[board_id]) {
        return IO_EXP_FAKE_ADC_NOT_INIT;
    }
    return values[board_id][port];
}

int io_adc_read(int board_id, int port)
{
    return io_exp_fake_adc_get(board_id, port, s_fake.adc_raw);
}

int io_adc_mV(int board_id, int port)
{
    return io_exp_fake_adc_get(board_id, port, s_fake.adc_mv);
}

int io_adc_mA(int board_id, int port)
{
    return io_exp_fake_adc_get(board_id, port, s_fake.adc_ma);
}

int io_SDO_write(int board_id, int index, int sub_index, int *data)
{
    s_fake.sdo_called    = true;
    s_fake.sdo_board     = board_id;
    s_fake.sdo_index     = index;
    s_fake.sdo_sub_index = sub_index;
    s_fake.sdo_data      = (data != NULL) ? *data : -1;
    return s_fake.sdo_result;
}

int io_init(const char *can_bus, int can_baud, int self_node, int board_count)
{
    s_fake.can_bus     = can_bus;
    s_fake.can_baud    = can_baud;
    s_fake.self_node   = self_node;
    s_fake.board_count = board_count;
    return s_fake.init_result;
}

void io_logApi_set(int (*cb)(const char *fmt, ...))
{
    s_fake.log_cb = cb;
}
