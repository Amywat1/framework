/**
 * @file    hal_io_sim.c
 * @brief   数字 IO HAL 仿真实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "adapters/outbound/hal/sim/hal_io_sim.h"

#include "common/io_handle.h"
#include "common/log.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "domain/ports/outbound/hal/hal_io_port.h"

#include <pthread.h>
#include <stddef.h>
#include <string.h>

#define SIM_IO_BOARD_MAX    8U
#define SIM_IO_PIN_COUNT    32U
#define SIM_IO_ADC_PORT_MAX 4
#define SIM_IO_ADC_NOT_INIT IO_ADC_ERR_UNINITIALIZED

static bool                           s_do_state[SIM_IO_BOARD_MAX][SIM_IO_PIN_COUNT + 1U];
static bool                           s_di_state[SIM_IO_BOARD_MAX][SIM_IO_PIN_COUNT + 1U];
static uint32_t                       s_pulse_counter[SIM_IO_BOARD_MAX][SIM_IO_PIN_COUNT + 1U];
static int                            s_adc_raw[SIM_IO_BOARD_MAX][SIM_IO_ADC_PORT_MAX + 1];
static int                            s_adc_mv[SIM_IO_BOARD_MAX][SIM_IO_ADC_PORT_MAX + 1];
static int                            s_adc_ma[SIM_IO_BOARD_MAX][SIM_IO_ADC_PORT_MAX + 1];
static io_sample_quality_t            s_di_quality[SIM_IO_BOARD_MAX];
static uint64_t                       s_di_timestamp_ms[SIM_IO_BOARD_MAX];
static uint32_t                       s_di_sequence[SIM_IO_BOARD_MAX];
static bool                           s_inited                    = false;
static bool                           s_started                   = false;
static uint32_t                       s_lifecycle_violation_count = 0U;
static const hal_io_sim_name_entry_t *s_do_names                  = NULL;
static size_t                         s_do_name_count             = 0U;
/* DO 写位于急停切断热路径：safety_cutout_execute -> 项目 cutout 实现 ->
 * hal_io do_set。该路径由 estop_poll 线程以 SCHED_FIFO 高优先级执行，而同
 * 两把锁又被 SCHED_OTHER 周期任务（IO 刷新、传感器采样）竞争，故启用优先级
 * 继承。本模块无 init 入口（靠加载期零初始化 + 首次使用），因此用
 * pthread_once 在首次上锁前完成初始化。 */
static pthread_mutex_t s_do_mutex;
static pthread_mutex_t s_di_mutex;
static pthread_once_t  s_mutex_once = PTHREAD_ONCE_INIT;

static const char *sim_do_name(io_do_t pin);

static void sim_mutex_init_once(void)
{
    (void)sw_mutex_init_prio_inherit(&s_do_mutex);
    (void)sw_mutex_init_prio_inherit(&s_di_mutex);
}

/** @brief 确保两把锁已初始化（幂等，所有加锁点入口调用）*/
static void sim_mutexes_ready(void)
{
    (void)pthread_once(&s_mutex_once, sim_mutex_init_once);
}

static void sim_record_lifecycle_violation(const char *operation)
{
    if (s_lifecycle_violation_count < UINT32_MAX) {
        s_lifecycle_violation_count++;
    }
    LOG_ERROR("hal_io_sim: %s called before init", operation);
}

static bool sim_is_valid_adc(int board_id, int port)
{
    return (board_id > 0) && (board_id < (int)SIM_IO_BOARD_MAX) && (port >= 1) && (port <= SIM_IO_ADC_PORT_MAX);
}

static bool sim_is_valid_di(io_di_t pin)
{
    uint16_t raw   = io_di_raw(pin);
    uint16_t board = io_handle_board(raw);
    uint16_t io    = io_handle_pin(raw);

    return (io_handle_kind(raw) == IO_KIND_DI) && (board > 0U) && (board < SIM_IO_BOARD_MAX) && (io > 0U)
           && (io <= SIM_IO_PIN_COUNT);
}

static bool sim_is_valid_do(io_do_t pin)
{
    uint16_t raw   = io_do_raw(pin);
    uint16_t board = io_handle_board(raw);
    uint16_t io    = io_handle_pin(raw);

    return (io_handle_kind(raw) == IO_KIND_DO) && (board > 0U) && (board < SIM_IO_BOARD_MAX) && (io > 0U)
           && (io <= SIM_IO_PIN_COUNT);
}

void hal_io_sim_set_di_level(io_di_t pin, bool level)
{
    uint16_t raw;
    uint16_t board;
    uint16_t io;

    if (!sim_is_valid_di(pin)) {
        return;
    }
    if (!s_inited) {
        return;
    }

    raw   = io_di_raw(pin);
    board = io_handle_board(raw);
    io    = io_handle_pin(raw);
    sim_mutexes_ready();
    pthread_mutex_lock(&s_di_mutex);
    s_di_state[board][io] = level;
    if (s_started && (s_di_quality[board] == IO_SAMPLE_QUALITY_VALID)) {
        s_di_timestamp_ms[board] = time_util_get_ms();
        s_di_sequence[board]++;
    }
    pthread_mutex_unlock(&s_di_mutex);
}

static sw_err_t sim_do_set(io_do_t pin, bool val)
{
    uint16_t raw   = io_do_raw(pin);
    uint16_t board = io_handle_board(raw);
    uint16_t io    = io_handle_pin(raw);
    bool     changed;

    if (!sim_is_valid_do(pin)) {
        return SW_ERR_PARAM;
    }
    if (!s_inited) {
        sim_record_lifecycle_violation("do_set");
        return SW_ERR_NOT_INIT;
    }

    sim_mutexes_ready();
    pthread_mutex_lock(&s_do_mutex);
    changed               = s_do_state[board][io] != val;
    s_do_state[board][io] = val;
    pthread_mutex_unlock(&s_do_mutex);
    if (changed) {
        const char *name = sim_do_name(pin);

        if ((name != NULL) && (strncmp(name, "DO_", 3) == 0)) {
            /* 日志用去掉 DO_ 的短信号名，便于对照机构动作。 */
            LOG_INFO("sim_io: %s=%d", name + 3, (int)val);
        } else if (name != NULL) {
            LOG_INFO("sim_io: %s=%d", name, (int)val);
        } else {
            LOG_INFO("sim_io: DO(board=%u,pin=%u)=%d", (unsigned)board, (unsigned)io, (int)val);
        }
    }
    return SW_OK;
}

sw_err_t hal_io_sim_get_do_level(io_do_t pin, bool *level)
{
    uint16_t raw;
    uint16_t board;
    uint16_t io;

    if ((level == NULL) || !sim_is_valid_do(pin)) {
        return SW_ERR_PARAM;
    }
    if (!s_inited) {
        sim_record_lifecycle_violation("get_do_level");
        return SW_ERR_NOT_INIT;
    }

    raw   = io_do_raw(pin);
    board = io_handle_board(raw);
    io    = io_handle_pin(raw);
    sim_mutexes_ready();
    pthread_mutex_lock(&s_do_mutex);
    *level = s_do_state[board][io];
    pthread_mutex_unlock(&s_do_mutex);
    return SW_OK;
}

static sw_err_t sim_di_read(io_di_t pin, io_di_sample_t *sample)
{
    uint16_t raw;
    uint16_t board;
    uint16_t io;

    if (sample == NULL) {
        return SW_ERR_PARAM;
    }
    *sample = (io_di_sample_t){.quality = IO_SAMPLE_QUALITY_UNINITIALIZED};
    if (!sim_is_valid_di(pin)) {
        return SW_ERR_PARAM;
    }
    if (!s_inited) {
        sim_record_lifecycle_violation("di_read");
        return SW_ERR_NOT_INIT;
    }

    raw   = io_di_raw(pin);
    board = io_handle_board(raw);
    io    = io_handle_pin(raw);
    sim_mutexes_ready();
    pthread_mutex_lock(&s_di_mutex);
    sample->level   = s_di_state[board][io];
    sample->quality = s_di_quality[board];
    if (sample->quality == IO_SAMPLE_QUALITY_VALID) {
        s_di_timestamp_ms[board] = time_util_get_ms();
        s_di_sequence[board]++;
    }
    sample->timestamp_ms = s_di_timestamp_ms[board];
    sample->sequence     = s_di_sequence[board];
    pthread_mutex_unlock(&s_di_mutex);
    return SW_OK;
}

void hal_io_sim_set_board_online(int board_id, bool online)
{
    if (!s_inited || (board_id <= 0) || (board_id >= (int)SIM_IO_BOARD_MAX)) {
        return;
    }

    sim_mutexes_ready();
    pthread_mutex_lock(&s_di_mutex);
    if (online) {
        s_di_quality[board_id]      = IO_SAMPLE_QUALITY_VALID;
        s_di_timestamp_ms[board_id] = time_util_get_ms();
        s_di_sequence[board_id]++;
    } else {
        s_di_quality[board_id] = IO_SAMPLE_QUALITY_OFFLINE;
    }
    pthread_mutex_unlock(&s_di_mutex);
}

static void sim_register_debug_input_cb(hal_io_debug_input_cb_t cb)
{
    (void)cb;
}

static void sim_register_board_status_cb(hal_io_board_status_cb_t cb)
{
    (void)cb;
}

static sw_err_t sim_io_init(void)
{
    memset(s_do_state, 0, sizeof(s_do_state));
    memset(s_di_state, 0, sizeof(s_di_state));
    memset(s_pulse_counter, 0, sizeof(s_pulse_counter));
    memset(s_adc_raw, 0, sizeof(s_adc_raw));
    memset(s_adc_mv, 0, sizeof(s_adc_mv));
    memset(s_adc_ma, 0, sizeof(s_adc_ma));
    memset(s_di_timestamp_ms, 0, sizeof(s_di_timestamp_ms));
    memset(s_di_sequence, 0, sizeof(s_di_sequence));
    for (int board = 0; board < (int)SIM_IO_BOARD_MAX; ++board) {
        s_di_quality[board] = (board == 0) ? IO_SAMPLE_QUALITY_UNINITIALIZED : IO_SAMPLE_QUALITY_PROBING;
    }
    s_inited  = true;
    s_started = false;
    return SW_OK;
}

static sw_err_t sim_io_start(void)
{
    if (!s_inited) {
        sim_record_lifecycle_violation("start");
        return SW_ERR_NOT_INIT;
    }
    sim_mutexes_ready();
    pthread_mutex_lock(&s_di_mutex);
    s_started = true;
    for (int board = 1; board < (int)SIM_IO_BOARD_MAX; ++board) {
        s_di_quality[board]      = IO_SAMPLE_QUALITY_VALID;
        s_di_timestamp_ms[board] = time_util_get_ms();
        s_di_sequence[board]++;
    }
    pthread_mutex_unlock(&s_di_mutex);
    return SW_OK;
}

static void sim_register_panic_cb(hal_io_panic_cb_t cb)
{
    (void)cb;
}

static sw_err_t sim_flush_outputs_now(void)
{
    if (!s_inited) {
        sim_record_lifecycle_violation("flush_outputs_now");
        return SW_ERR_NOT_INIT;
    }
    return SW_OK;
}

static bool sim_board_is_online(int board_id)
{
    bool online;

    if (!s_inited) {
        sim_record_lifecycle_violation("board_is_online");
        return false;
    }
    if ((board_id <= 0) || (board_id >= (int)SIM_IO_BOARD_MAX)) {
        return false;
    }
    sim_mutexes_ready();
    pthread_mutex_lock(&s_di_mutex);
    online = s_di_quality[board_id] == IO_SAMPLE_QUALITY_VALID;
    pthread_mutex_unlock(&s_di_mutex);
    return online;
}

static sw_err_t sim_wait_boards_online(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_inited) {
        sim_record_lifecycle_violation("wait_boards_online");
        return SW_ERR_NOT_INIT;
    }
    return SW_OK;
}

static bool sim_try_parse_di(const char *name, io_di_t *out)
{
    (void)name;
    (void)out;
    return false;
}

static bool sim_name_matches(const char *input, const char *canonical)
{
    if ((input == NULL) || (canonical == NULL)) {
        return false;
    }
    if (strcmp(input, canonical) == 0) {
        return true;
    }
    /* 允许省略 DO_ 前缀：GANTRY_FWD 匹配 DO_GANTRY_FWD。 */
    if ((strncmp(canonical, "DO_", 3) == 0) && (strcmp(input, canonical + 3) == 0)) {
        return true;
    }
    return false;
}

static bool sim_try_parse_do(const char *name, io_do_t *out)
{
    size_t i;

    if ((name == NULL) || (s_do_names == NULL)) {
        return false;
    }
    for (i = 0; i < s_do_name_count; ++i) {
        if (sim_name_matches(name, s_do_names[i].name)) {
            if (out != NULL) {
                out->raw = s_do_names[i].raw;
            }
            return true;
        }
    }
    return false;
}

static const char *sim_di_name(io_di_t pin)
{
    (void)pin;
    return NULL;
}

static const char *sim_do_name(io_do_t pin)
{
    uint16_t raw = io_do_raw(pin);
    size_t   i;

    if (s_do_names == NULL) {
        return NULL;
    }
    for (i = 0; i < s_do_name_count; ++i) {
        if (s_do_names[i].raw == raw) {
            return s_do_names[i].name;
        }
    }
    return NULL;
}

void hal_io_sim_set_do_names(const hal_io_sim_name_entry_t *table, size_t count)
{
    s_do_names      = table;
    s_do_name_count = (table != NULL) ? count : 0U;
}

static int sim_board_count(void)
{
    return 1;
}

static int sim_pulse_read(io_di_t pin)
{
    uint16_t board = io_handle_board(io_di_raw(pin));
    uint16_t p     = io_handle_pin(io_di_raw(pin));
    int      value;

    if (!sim_is_valid_di(pin)) {
        return -1;
    }
    if (!s_inited) {
        sim_record_lifecycle_violation("pulse_read");
        return -1;
    }

    sim_mutexes_ready();
    pthread_mutex_lock(&s_di_mutex);
    value = (int)s_pulse_counter[board][p];
    pthread_mutex_unlock(&s_di_mutex);
    return value;
}

static sw_err_t sim_pulse_clear(io_di_t pin)
{
    uint16_t board = io_handle_board(io_di_raw(pin));
    uint16_t p     = io_handle_pin(io_di_raw(pin));

    if (!sim_is_valid_di(pin)) {
        return SW_ERR_PARAM;
    }
    if (!s_inited) {
        sim_record_lifecycle_violation("pulse_clear");
        return SW_ERR_NOT_INIT;
    }

    sim_mutexes_ready();
    pthread_mutex_lock(&s_di_mutex);
    s_pulse_counter[board][p] = 0U;
    pthread_mutex_unlock(&s_di_mutex);
    return SW_OK;
}

void hal_io_sim_set_pulse_counter(io_di_t pin, uint32_t value)
{
    uint16_t board = io_handle_board(io_di_raw(pin));
    uint16_t p     = io_handle_pin(io_di_raw(pin));

    if (!sim_is_valid_di(pin)) {
        return;
    }
    if (!s_inited) {
        return;
    }

    sim_mutexes_ready();
    pthread_mutex_lock(&s_di_mutex);
    s_pulse_counter[board][p] = value;
    pthread_mutex_unlock(&s_di_mutex);
}

void hal_io_sim_set_adc(int board_id, int port, int raw, int mv, int ma)
{
    if (!sim_is_valid_adc(board_id, port)) {
        return;
    }
    if (!s_inited) {
        return;
    }

    s_adc_raw[board_id][port] = raw;
    s_adc_mv[board_id][port]  = mv;
    s_adc_ma[board_id][port]  = ma;
}

static sw_err_t sim_adc_sample(int board_id, int port, io_adc_sample_t *sample)
{
    if ((sample == NULL) || !sim_is_valid_adc(board_id, port)) {
        return SW_ERR_PARAM;
    }
    if (!s_inited) {
        sim_record_lifecycle_violation("adc_sample");
        return SW_ERR_NOT_INIT;
    }
    sample->raw          = s_adc_raw[board_id][port];
    sample->millivolt    = s_adc_mv[board_id][port];
    sample->milliamp     = s_adc_ma[board_id][port];
    sample->quality      = IO_SAMPLE_QUALITY_VALID;
    sample->timestamp_ms = time_util_get_ms();
    return SW_OK;
}

static int sim_adc_read(int board_id, int port)
{
    if (!sim_is_valid_adc(board_id, port)) {
        return -1;
    }
    if (!s_inited) {
        sim_record_lifecycle_violation("adc_read");
        return SIM_IO_ADC_NOT_INIT;
    }
    return s_adc_raw[board_id][port];
}

static int sim_adc_mv(int board_id, int port)
{
    if (!sim_is_valid_adc(board_id, port)) {
        return -1;
    }
    if (!s_inited) {
        sim_record_lifecycle_violation("adc_mv");
        return SIM_IO_ADC_NOT_INIT;
    }
    return s_adc_mv[board_id][port];
}

static int sim_adc_ma(int board_id, int port)
{
    if (!sim_is_valid_adc(board_id, port)) {
        return -1;
    }
    if (!s_inited) {
        sim_record_lifecycle_violation("adc_ma");
        return SIM_IO_ADC_NOT_INIT;
    }
    return s_adc_ma[board_id][port];
}

static sw_err_t sim_get_stats(int board_id, hal_io_stats_t *out)
{
    if ((out == NULL) || (board_id <= 0) || (board_id >= (int)SIM_IO_BOARD_MAX)) {
        return SW_ERR_PARAM;
    }
    if (!s_inited) {
        sim_record_lifecycle_violation("get_stats");
        return SW_ERR_NOT_INIT;
    }
    sim_mutexes_ready();
    pthread_mutex_lock(&s_di_mutex);
    memset(out, 0, sizeof(*out));
    out->online                = s_di_quality[board_id] == IO_SAMPLE_QUALITY_VALID;
    out->input_refresh_count   = s_di_sequence[board_id];
    out->last_input_refresh_ms = s_di_timestamp_ms[board_id];
    pthread_mutex_unlock(&s_di_mutex);
    return SW_OK;
}

static const hal_io_ops_t s_ops = {
    .init                     = sim_io_init,
    .start                    = sim_io_start,
    .register_panic_cb        = sim_register_panic_cb,
    .flush_outputs_now        = sim_flush_outputs_now,
    .board_is_online          = sim_board_is_online,
    .wait_boards_online       = sim_wait_boards_online,
    .do_set                   = sim_do_set,
    .di_read                  = sim_di_read,
    .register_debug_input_cb  = sim_register_debug_input_cb,
    .register_board_status_cb = sim_register_board_status_cb,
    .try_parse_di             = sim_try_parse_di,
    .try_parse_do             = sim_try_parse_do,
    .di_name                  = sim_di_name,
    .do_name                  = sim_do_name,
    .board_count              = sim_board_count,
    .get_stats                = sim_get_stats,
    .pulse_read               = sim_pulse_read,
    .pulse_clear              = sim_pulse_clear,
    .adc_sample               = sim_adc_sample,
    .adc_read                 = sim_adc_read,
    .adc_mv                   = sim_adc_mv,
    .adc_ma                   = sim_adc_ma,
};

void hal_io_sim_register(void)
{
    s_lifecycle_violation_count = 0U;
    hal_io_register(&s_ops);
    LOG_INFO("hal_io_sim: registered");
}

sw_err_t hal_io_sim_validate_lifecycle(void)
{
    if (s_lifecycle_violation_count != 0U) {
        LOG_ERROR("hal_io_sim: lifecycle validation failed, violations=%u", (unsigned)s_lifecycle_violation_count);
        return SW_ERR_STATE;
    }
    return SW_OK;
}

#ifdef HAL_IO_SIM_UNIT_TEST
void hal_io_sim_test_reset(void)
{
    memset(s_do_state, 0, sizeof(s_do_state));
    memset(s_di_state, 0, sizeof(s_di_state));
    memset(s_pulse_counter, 0, sizeof(s_pulse_counter));
    memset(s_adc_raw, 0, sizeof(s_adc_raw));
    memset(s_adc_mv, 0, sizeof(s_adc_mv));
    memset(s_adc_ma, 0, sizeof(s_adc_ma));
    memset(s_di_quality, 0, sizeof(s_di_quality));
    memset(s_di_timestamp_ms, 0, sizeof(s_di_timestamp_ms));
    memset(s_di_sequence, 0, sizeof(s_di_sequence));
    s_inited                    = false;
    s_started                   = false;
    s_lifecycle_violation_count = 0U;
    s_do_names                  = NULL;
    s_do_name_count             = 0U;
}
#endif
