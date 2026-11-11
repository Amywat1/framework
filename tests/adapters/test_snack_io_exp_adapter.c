/**
 * @file    test_snack_io_exp_adapter.c
 * @brief   Snack io_exp IO HAL provider 单元测试。
 */

#include "adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.h"
#include "adapters/outbound/hal/providers/snack/io_exp/snack_io_adapter.h"
#include "adapters/outbound/hal/components/adc_gate/hal_adc_gate.h"
#include "adapters/outbound/hal/components/pulse_gate/hal_pulse_gate.h"
#include "common/io_handle.h"
#include "common/io_sample.h"
#include "domain/ports/outbound/hal/hal_io_port.h"
#include "tests/stubs/io_exp/io_exp_fake.h"
#include "wdf_test_spec.h"

#include <string.h>
#include <unistd.h>

static const drv_io_name_entry_t s_di_names[] = {
    {"DI_START", IO_HANDLE_MAKE(IO_KIND_DI, 1U, 1U)},
    {"DI_STOP",  IO_HANDLE_MAKE(IO_KIND_DI, 2U, 1U)},
};
static const drv_io_name_entry_t s_do_names[] = {
    {"DO_RELAY", IO_HANDLE_MAKE(IO_KIND_DO, 1U, 2U)},
    {"DO_LAMP",  IO_HANDLE_MAKE(IO_KIND_DO, 2U, 3U)},
};

static const hal_io_ops_t *io_ops(void)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    TEST_ASSERT_NOT_NULL(ops);
    return ops;
}

static drv_io_cfg_t make_cfg(void)
{
    drv_io_cfg_t cfg = {
        .can_bus     = "can0",
        .can_baud    = 500000,
        .self_node   = 9,
        .board_count = 2,
        .pin_count   = 8,
        .di_table    = s_di_names,
        .di_count    = sizeof(s_di_names) / sizeof(s_di_names[0]),
        .do_table    = s_do_names,
        .do_count    = sizeof(s_do_names) / sizeof(s_do_names[0]),
    };

    return cfg;
}

static void configure_adapter(int board_count)
{
    drv_io_cfg_t cfg = make_cfg();

    cfg.board_count = board_count;
    io_exp_fake_reset();
    snack_io_adapter_test_reset();
    snack_io_adapter_register();
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_io_adapter_configure(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->init());
}

static void start_online_boards(int board_count)
{
    int i;

    for (i = 1; i <= board_count; ++i) {
        io_exp_fake_set_online(i, 1);
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->start());
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->wait_boards_online(500U));
}

static io_di_sample_t read_di(io_di_t pin)
{
    io_di_sample_t sample;

    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->di_read(pin, &sample));
    return sample;
}

/** 等到 ADC 原始值匹配，或 500ms 超时。 */
static int wait_adc_raw(int board_id, int port, int expected)
{
    int      raw = IO_ADC_ERR_UNINITIALIZED;
    unsigned i;

    for (i = 0U; i < 50U; ++i) {
        raw = io_ops()->adc_read(board_id, port);
        if (raw == expected) {
            return raw;
        }
        usleep(10U * 1000U);
    }
    TEST_ASSERT_EQUAL_INT(expected, raw);
    return raw;
}

/** 等到脉冲计数值匹配，或 500ms 超时。 */
static int wait_pulse_raw(io_di_t pin, int expected)
{
    int      raw = IO_PULSE_ERR_UNINITIALIZED;
    unsigned i;

    for (i = 0U; i < 50U; ++i) {
        raw = io_ops()->pulse_read(pin);
        if (raw == expected) {
            return raw;
        }
        usleep(10U * 1000U);
    }
    TEST_ASSERT_EQUAL_INT(expected, raw);
    return raw;
}

void setUp(void)
{
    hal_adc_gate_reset_for_test();
    hal_pulse_gate_reset_for_test();
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_io_reset_for_test());
    configure_adapter(2);
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, drv_io_reset_for_test());
}

static void test_sdk_init_registers_internal_log_and_delegates_to_io_exp_sdk(void)
{
    io_exp_fake_set_init_result(0);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_exp_driver_sdk_init("can0", 500000, 9, 2));
    TEST_ASSERT_EQUAL_STRING("can0", io_exp_fake_can_bus());
    TEST_ASSERT_EQUAL_INT(500000, io_exp_fake_can_baud());
    TEST_ASSERT_EQUAL_INT(9, io_exp_fake_self_node());
    TEST_ASSERT_EQUAL_INT(2, io_exp_fake_board_count());

    io_exp_fake_set_init_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_ERR_HW, io_exp_driver_sdk_init("can1", 250000, 1, 1));
    TEST_ASSERT_TRUE(io_exp_fake_log_api_set());
}

static void test_init_rejects_invalid_config(void)
{
    drv_io_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_cfg_validate(NULL));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(NULL));

    cfg         = make_cfg();
    cfg.can_bus = NULL;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_cfg_validate(&cfg));

    cfg             = make_cfg();
    cfg.board_count = 0;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_cfg_validate(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(&cfg));

    cfg           = make_cfg();
    cfg.pin_count = 0;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_cfg_validate(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(&cfg));

    cfg           = make_cfg();
    cfg.pin_count = 33;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_cfg_validate(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, drv_io_init(&cfg));
}

static void test_hal_adapter_validates_cfg_before_sdk_init(void)
{
    drv_io_cfg_t cfg = make_cfg();

    io_exp_fake_reset();
    snack_io_adapter_test_reset();
    snack_io_adapter_register();
    cfg.pin_count = 33;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, snack_io_adapter_configure(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, io_ops()->init());
    TEST_ASSERT_NULL(io_exp_fake_can_bus());
}

static void test_hal_adapter_rejects_init_before_configure_and_duplicate_configure(void)
{
    drv_io_cfg_t cfg = make_cfg();

    snack_io_adapter_test_reset();
    snack_io_adapter_register();
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, io_ops()->init());
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, io_ops()->start());
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_io_adapter_configure(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_BUSY, snack_io_adapter_configure(&cfg));
}

static void test_hal_adapter_exposes_name_resolution_and_board_count(void)
{
    io_di_t di;
    io_do_t dout;

    TEST_ASSERT_EQUAL_INT(2, io_ops()->board_count());
    TEST_ASSERT_TRUE(io_ops()->try_parse_di("DI_START", &di));
    TEST_ASSERT_EQUAL_UINT16(IO_HANDLE_MAKE(IO_KIND_DI, 1U, 1U), di.raw);
    TEST_ASSERT_TRUE(io_ops()->try_parse_di("START", &di));
    TEST_ASSERT_EQUAL_UINT16(IO_HANDLE_MAKE(IO_KIND_DI, 1U, 1U), di.raw);
    TEST_ASSERT_FALSE(io_ops()->try_parse_di("MISSING", &di));

    TEST_ASSERT_TRUE(io_ops()->try_parse_do("DO_RELAY", &dout));
    TEST_ASSERT_EQUAL_UINT16(IO_HANDLE_MAKE(IO_KIND_DO, 1U, 2U), dout.raw);
    TEST_ASSERT_TRUE(io_ops()->try_parse_do("RELAY", &dout));
    TEST_ASSERT_EQUAL_STRING("DI_START", io_ops()->di_name(IO_DI(1U, 1U)));
    TEST_ASSERT_EQUAL_STRING("DO_RELAY", io_ops()->do_name(IO_DO(1U, 2U)));
}

static void test_do_set_updates_stats_and_rejects_invalid_pin(void)
{
    hal_io_stats_t stats;

    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->do_set(IO_DO(1U, 2U), true));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->get_stats(1, &stats));
    TEST_ASSERT_TRUE(stats.dirty_pending);
    TEST_ASSERT_EQUAL_UINT32(1U, stats.output_request_count);
    TEST_ASSERT_EQUAL_UINT32(0x00000002U, stats.last_output_snapshot);

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, io_ops()->do_set(IO_DO(9U, 1U), true));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, io_ops()->get_stats(0, &stats));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, io_ops()->get_stats(1, NULL));
}

static void test_di_test_override_controls_read_value(void)
{
    io_di_t pin = IO_DI(1U, 1U);

    TEST_ASSERT_NOT_NULL(io_ops()->di_test_override);
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_PROBING, read_di(pin).quality);
    io_ops()->di_test_override(pin, 1);
    TEST_ASSERT_TRUE(read_di(pin).level);
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_VALID, read_di(pin).quality);
    io_ops()->di_test_override(pin, 0);
    TEST_ASSERT_FALSE(read_di(pin).level);
    io_ops()->di_test_override(pin, -1);
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_PROBING, read_di(pin).quality);
}

static void test_wait_boards_online_uses_worker_cache(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, io_ops()->wait_boards_online(10U));
    io_exp_fake_set_online(1, 1);
    io_exp_fake_set_online(2, 0);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->start());
    TEST_ASSERT_EQUAL_INT(SW_ERR_TIMEOUT, io_ops()->wait_boards_online(10U));

    io_exp_fake_set_online(2, 1);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->wait_boards_online(500U));
}

static void test_pulse_read_and_clear_delegate_to_sdk(void)
{
    io_di_t pin = IO_DI(1U, 1U);
    int     raw;

    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, io_ops()->pulse_clear(pin));
    start_online_boards(2);
    io_exp_fake_set_pulse(1, 1, 77);
    TEST_ASSERT_EQUAL_INT(IO_PULSE_ERR_UNINITIALIZED, io_ops()->pulse_read(pin));

    hal_pulse_gate_acquire(1, 1);
    TEST_ASSERT_EQUAL_INT(77, wait_pulse_raw(pin, 77));
    TEST_ASSERT_EQUAL_INT(-1, io_ops()->pulse_read(IO_DI(9U, 1U)));

    /* 受理即返回：完成前读数无效，任何时刻都不能再读到清零前的 77 */
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->pulse_clear(pin));
    raw = io_ops()->pulse_read(pin);
    TEST_ASSERT_TRUE((raw < 0) || (raw == 0));
    TEST_ASSERT_EQUAL_INT(0, wait_pulse_raw(pin, 0));
    TEST_ASSERT_TRUE(io_exp_fake_sdo_called());
    TEST_ASSERT_EQUAL_INT(1, io_exp_fake_sdo_board());
    TEST_ASSERT_EQUAL_INT(0x2005, io_exp_fake_sdo_index());
    TEST_ASSERT_EQUAL_INT(1, io_exp_fake_sdo_sub_index());
    TEST_ASSERT_EQUAL_INT(0, io_exp_fake_sdo_data());

    /* SDO 失败：请求仍受理，重试耗尽后采样恢复为未清零的计数 */
    io_exp_fake_set_pulse(1, 1, 55);
    TEST_ASSERT_EQUAL_INT(55, wait_pulse_raw(pin, 55));
    io_exp_fake_set_sdo_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->pulse_clear(pin));
    TEST_ASSERT_EQUAL_INT(55, wait_pulse_raw(pin, 55));

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, io_ops()->pulse_clear(IO_DI(9U, 1U)));
    hal_pulse_gate_release(1, 1);
}

static void test_adc_read_delegates_to_sdk(void)
{
    io_adc_sample_t sample;

    start_online_boards(2);
    TEST_ASSERT_EQUAL_INT(DRV_IO_ADC_ERR_NOT_INIT, io_ops()->adc_read(1, 1));
    TEST_ASSERT_EQUAL_INT(DRV_IO_ADC_ERR_NOT_INIT, io_ops()->adc_mv(1, 2));
    TEST_ASSERT_EQUAL_INT(DRV_IO_ADC_ERR_NOT_INIT, io_ops()->adc_ma(1, 3));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->adc_sample(1, 1, &sample));
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_UNINITIALIZED, sample.quality);

    io_exp_fake_set_adc(1, 1, 100, 2500, 12);
    TEST_ASSERT_EQUAL_INT(DRV_IO_ADC_ERR_NOT_INIT, io_ops()->adc_read(1, 1));

    hal_adc_gate_acquire(1, 1);
    TEST_ASSERT_EQUAL_INT(100, wait_adc_raw(1, 1, 100));
    TEST_ASSERT_EQUAL_INT(0, io_ops()->adc_mv(1, 1));
    TEST_ASSERT_EQUAL_INT(0, io_ops()->adc_ma(1, 1));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->adc_sample(1, 1, &sample));
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_VALID, sample.quality);
    TEST_ASSERT_EQUAL_INT(100, sample.raw);
    TEST_ASSERT_EQUAL_INT(0, sample.millivolt);
    TEST_ASSERT_EQUAL_INT(0, sample.milliamp);
    hal_adc_gate_release(1, 1);

    TEST_ASSERT_EQUAL_INT(-1, io_ops()->adc_read(0, 1));
    TEST_ASSERT_EQUAL_INT(-1, io_ops()->adc_mv(1, 0));
    TEST_ASSERT_EQUAL_INT(-1, io_ops()->adc_ma(1, 5));
    TEST_ASSERT_EQUAL_INT(-1, io_ops()->adc_read(9, 1));
}

static void test_start_confirms_healthy_boards_within_watchdog_window(void)
{
    hal_io_stats_t stats;

    io_exp_fake_set_online(1, 1);
    io_exp_fake_set_online(2, 1);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->start());
    usleep(200U * 1000U);

    TEST_ASSERT_TRUE(io_ops()->board_is_online(1));
    TEST_ASSERT_TRUE(io_ops()->board_is_online(2));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->get_stats(1, &stats));
    TEST_ASSERT_GREATER_THAN_UINT32(0U, stats.input_refresh_count);
}

static void test_four_boards_use_pdo_only(void)
{
    hal_io_stats_t stats;

    configure_adapter(4);
    TEST_ASSERT_EQUAL_INT(DRV_IO_TRANSPORT_PDO, drv_io_transport_mode());
    start_online_boards(4);
    TEST_ASSERT_EQUAL_INT(DRV_IO_TRANSPORT_PDO, drv_io_transport_mode());
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->do_set(IO_DO(1U, 2U), true));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->flush_outputs_now());

    TEST_ASSERT_GREATER_THAN_UINT32(0U, io_exp_fake_pdo_read_count());
    TEST_ASSERT_GREATER_THAN_UINT32(0U, io_exp_fake_pdo_write_count());
    TEST_ASSERT_EQUAL_UINT32(0U, io_exp_fake_sdo_read_count());
    TEST_ASSERT_EQUAL_UINT32(0U, io_exp_fake_sdo_write_count());
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->get_stats(1, &stats));
    TEST_ASSERT_FALSE(stats.dirty_pending);
}

static void test_five_boards_use_sdo_only(void)
{
    configure_adapter(5);
    TEST_ASSERT_EQUAL_INT(DRV_IO_TRANSPORT_SDO, drv_io_transport_mode());
    start_online_boards(5);
    TEST_ASSERT_EQUAL_INT(DRV_IO_TRANSPORT_SDO, drv_io_transport_mode());
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->do_set(IO_DO(1U, 2U), true));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->flush_outputs_now());

    TEST_ASSERT_GREATER_THAN_UINT32(0U, io_exp_fake_sdo_read_count());
    TEST_ASSERT_GREATER_THAN_UINT32(0U, io_exp_fake_sdo_write_count());
    TEST_ASSERT_EQUAL_UINT32(0U, io_exp_fake_pdo_read_count());
    TEST_ASSERT_EQUAL_UINT32(0U, io_exp_fake_pdo_write_count());
}

static void test_failed_flush_keeps_output_dirty(void)
{
    hal_io_stats_t stats;

    start_online_boards(2);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->do_set(IO_DO(1U, 2U), true));
    io_exp_fake_set_pdo_write_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_ERR_COMM, io_ops()->flush_outputs_now());
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->get_stats(1, &stats));
    TEST_ASSERT_TRUE(stats.dirty_pending);

    io_exp_fake_set_pdo_write_result(0);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->flush_outputs_now());
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->get_stats(1, &stats));
    TEST_ASSERT_FALSE(stats.dirty_pending);
}

/** 等到滤波 SDO 次数达到下限，或超时。 */
static unsigned wait_filter_sdo_count(unsigned min_count, unsigned timeout_ms)
{
    unsigned count = 0U;
    unsigned i;
    unsigned loops = (timeout_ms / 20U) + 1U;

    for (i = 0U; i < loops; ++i) {
        count = io_exp_fake_filter_sdo_count();
        if (count >= min_count) {
            return count;
        }
        usleep(20U * 1000U);
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(min_count, count);
    return count;
}

/** 等到子板在线状态匹配，或超时。 */
static void wait_board_online_state(int board_id, bool online, unsigned timeout_ms)
{
    unsigned i;
    unsigned loops = (timeout_ms / 20U) + 1U;

    for (i = 0U; i < loops; ++i) {
        if (io_ops()->board_is_online(board_id) == online) {
            return;
        }
        usleep(20U * 1000U);
    }
    TEST_ASSERT_EQUAL(online, io_ops()->board_is_online(board_id));
}

/** 等到指定 DI 达到目标质量，或 500ms 超时。 */
static io_di_sample_t wait_di_quality(io_di_t pin, io_sample_quality_t quality)
{
    io_di_sample_t sample = {0};
    unsigned       i;

    for (i = 0U; i < 50U; ++i) {
        sample = read_di(pin);
        if (sample.quality == quality) {
            return sample;
        }
        usleep(10U * 1000U);
    }
    TEST_ASSERT_EQUAL_INT(quality, sample.quality);
    return sample;
}

static void test_sdo_read_failure_keeps_last_level_and_marks_stale(void)
{
    io_di_t        pin_on  = IO_DI(1U, 1U);
    io_di_t        pin_off = IO_DI(1U, 2U);
    io_di_sample_t sample_on;
    io_di_sample_t sample_off;

    configure_adapter(5);
    io_exp_fake_set_input(1, 0x1);
    start_online_boards(5);
    sample_on = wait_di_quality(pin_on, IO_SAMPLE_QUALITY_VALID);
    TEST_ASSERT_TRUE(sample_on.level);
    TEST_ASSERT_FALSE(read_di(pin_off).level);

    io_exp_fake_set_sdo_read_error(-1);
    sample_on  = wait_di_quality(pin_on, IO_SAMPLE_QUALITY_STALE);
    sample_off = read_di(pin_off);
    TEST_ASSERT_TRUE(sample_on.level);
    TEST_ASSERT_FALSE(sample_off.level);
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_STALE, sample_off.quality);
}

static void test_hal_adapter_rejects_duplicate_init(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, io_ops()->init());
}

static void test_init_after_start_preserves_online_state(void)
{
    drv_io_cfg_t cfg = make_cfg();

    start_online_boards(2);
    TEST_ASSERT_TRUE(io_ops()->board_is_online(1));
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, drv_io_init(&cfg));
    TEST_ASSERT_TRUE(io_ops()->board_is_online(1));
    TEST_ASSERT_TRUE(io_ops()->board_is_online(2));
}

static void test_unconfigured_di_filter_not_written_on_online(void)
{
    start_online_boards(2);
    usleep(200U * 1000U);
    TEST_ASSERT_EQUAL_UINT32(0U, io_exp_fake_filter_sdo_count());
}

static void test_di_set_filter_ms_writes_on_online(void)
{
    io_di_t pin = IO_DI(1U, 3U);

    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->di_set_filter_ms(pin, 8U));
    TEST_ASSERT_EQUAL_UINT32(0U, io_exp_fake_filter_sdo_count());

    start_online_boards(2);
    (void)wait_filter_sdo_count(1U, 500U);
    TEST_ASSERT_EQUAL_INT(1, io_exp_fake_filter_sdo_board());
    TEST_ASSERT_EQUAL_INT(3, io_exp_fake_filter_sdo_pin());
    TEST_ASSERT_EQUAL_INT(8, io_exp_fake_filter_sdo_data());
    TEST_ASSERT_EQUAL_INT(0x2006, io_exp_fake_sdo_index());
    TEST_ASSERT_EQUAL_INT(2, io_exp_fake_sdo_len());
    TEST_ASSERT_EQUAL_INT(0x06, io_exp_fake_sdo_datatype());
}

static void test_di_set_filter_ms_zero_is_written(void)
{
    io_di_t pin = IO_DI(2U, 1U);

    start_online_boards(2);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->di_set_filter_ms(pin, 0U));
    (void)wait_filter_sdo_count(1U, 500U);
    TEST_ASSERT_EQUAL_INT(2, io_exp_fake_filter_sdo_board());
    TEST_ASSERT_EQUAL_INT(1, io_exp_fake_filter_sdo_pin());
    TEST_ASSERT_EQUAL_INT(0, io_exp_fake_filter_sdo_data());
}

static void test_di_set_filter_ms_rewrites_on_recover(void)
{
    io_di_t  pin   = IO_DI(1U, 2U);
    unsigned first;

    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->di_set_filter_ms(pin, 5U));
    start_online_boards(2);
    first = wait_filter_sdo_count(1U, 500U);

    io_exp_fake_set_online(1, 0);
    wait_board_online_state(1, false, 2000U);
    TEST_ASSERT_TRUE(io_ops()->board_is_online(2));

    io_exp_fake_set_online(1, 1);
    wait_board_online_state(1, true, 4000U);
    (void)wait_filter_sdo_count(first + 1U, 1000U);
    TEST_ASSERT_EQUAL_INT(1, io_exp_fake_filter_sdo_board());
    TEST_ASSERT_EQUAL_INT(2, io_exp_fake_filter_sdo_pin());
    TEST_ASSERT_EQUAL_INT(5, io_exp_fake_filter_sdo_data());
}

static void test_di_set_filter_ms_rejects_invalid_pin(void)
{
    io_di_t as_do = {IO_HANDLE_MAKE(IO_KIND_DO, 1U, 1U)};

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, io_ops()->di_set_filter_ms(IO_DI(9U, 1U), 4U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, io_ops()->di_set_filter_ms(as_do, 4U));
    TEST_ASSERT_EQUAL_UINT32(0U, io_exp_fake_filter_sdo_count());
}

static void test_di_set_filter_ms_failure_keeps_board_online(void)
{
    io_di_t pin = IO_DI(1U, 1U);

    start_online_boards(2);
    io_exp_fake_set_sdo_result(-1);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->di_set_filter_ms(pin, 6U));
    (void)wait_filter_sdo_count(3U, 500U);
    TEST_ASSERT_TRUE(io_ops()->board_is_online(1));
    TEST_ASSERT_TRUE(io_ops()->board_is_online(2));
}

static unsigned s_panic_count;

static void test_count_panic(void)
{
    s_panic_count++;
}

static void test_hal_adapter_init_continues_when_sdk_fails(void)
{
    drv_io_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_OK, drv_io_reset_for_test());
    io_exp_fake_reset();
    snack_io_adapter_test_reset();
    snack_io_adapter_register();
    io_exp_fake_set_init_result(-3);
    TEST_ASSERT_EQUAL_INT(SW_OK, snack_io_adapter_configure(&cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->init());
    TEST_ASSERT_EQUAL_STRING("can0", io_exp_fake_can_bus());
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->start());
    usleep(200U * 1000U);
    TEST_ASSERT_FALSE(io_ops()->board_is_online(1));
    TEST_ASSERT_FALSE(io_ops()->board_is_online(2));
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->flush_outputs_now());
}

static void test_all_boards_offline_safe_stop_keeps_running(void)
{
    s_panic_count = 0U;
    io_ops()->register_panic_cb(test_count_panic);
    TEST_ASSERT_EQUAL_INT(SW_OK, io_ops()->start());
    usleep(4000U * 1000U);
    TEST_ASSERT_EQUAL_UINT32(1U, s_panic_count);
    TEST_ASSERT_FALSE(io_ops()->board_is_online(1));
    TEST_ASSERT_FALSE(io_ops()->board_is_online(2));
    TEST_ASSERT_EQUAL_INT(IO_SAMPLE_QUALITY_OFFLINE, read_di(IO_DI(1U, 1U)).quality);
}

static void test_runtime_sdk_calls_share_one_worker(void)
{
    start_online_boards(2);
    io_exp_fake_set_pulse(1, 1, 12);
    io_exp_fake_set_adc(1, 1, 100, 2500, 12);
    TEST_ASSERT_EQUAL_INT(IO_PULSE_ERR_UNINITIALIZED, io_ops()->pulse_read(IO_DI(1U, 1U)));
    hal_pulse_gate_acquire(1, 1);
    TEST_ASSERT_EQUAL_INT(12, wait_pulse_raw(IO_DI(1U, 1U), 12));
    hal_adc_gate_acquire(1, 1);
    TEST_ASSERT_EQUAL_INT(100, wait_adc_raw(1, 1, 100));
    TEST_ASSERT_TRUE(io_exp_fake_sdk_thread_seen());
    TEST_ASSERT_TRUE(io_exp_fake_sdk_thread_consistent());
    hal_adc_gate_release(1, 1);
    hal_pulse_gate_release(1, 1);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_sdk_init_registers_internal_log_and_delegates_to_io_exp_sdk,
                 "",
                 "验证SDK初始化注册内部日志并委托到IO扩展SDK");
    WDF_RUN_TEST(test_init_rejects_invalid_config, "", "验证初始化拒绝无效配置");
    WDF_RUN_TEST(test_hal_adapter_validates_cfg_before_sdk_init, "", "验证 HAL 适配器在 SDK 初始化前校验配置");
    WDF_RUN_TEST(test_hal_adapter_rejects_init_before_configure_and_duplicate_configure,
                 "",
                 "验证 HAL 适配器拒绝配置前初始化和重复配置");
    WDF_RUN_TEST(test_hal_adapter_exposes_name_resolution_and_board_count, "", "验证 HAL 适配器提供名称解析和板卡数量");
    WDF_RUN_TEST(test_do_set_updates_stats_and_rejects_invalid_pin, "", "验证DO设置更新统计并拒绝无效引脚");
    WDF_RUN_TEST(test_di_test_override_controls_read_value, "", "验证DI测试覆盖值控制读取值");
    WDF_RUN_TEST(test_wait_boards_online_uses_worker_cache, "", "验证等待板卡在线仅使用worker缓存");
    WDF_RUN_TEST(test_pulse_read_and_clear_delegate_to_sdk, "", "验证脉冲读取并清除委托到SDK");
    WDF_RUN_TEST(test_adc_read_delegates_to_sdk, "", "验证ADC读取委托到SDK");
    WDF_RUN_TEST(test_start_confirms_healthy_boards_within_watchdog_window, "", "验证启动在看门狗窗口内确认健康板卡");
    WDF_RUN_TEST(test_four_boards_use_pdo_only, "", "验证四块子板统一使用PDO");
    WDF_RUN_TEST(test_five_boards_use_sdo_only, "", "验证五块子板统一使用SDO");
    WDF_RUN_TEST(test_sdo_read_failure_keeps_last_level_and_marks_stale, "", "验证SDO读失败保留旧电平并标STALE");
    WDF_RUN_TEST(test_hal_adapter_rejects_duplicate_init, "", "验证HAL适配器拒绝重复初始化");
    WDF_RUN_TEST(test_init_after_start_preserves_online_state, "", "验证start后init失败且不清除在线状态");
    WDF_RUN_TEST(test_failed_flush_keeps_output_dirty, "", "验证输出写失败保留待落地状态");
    WDF_RUN_TEST(test_hal_adapter_init_continues_when_sdk_fails,
                 "",
                 "验证SDK初始化失败后HAL仍完成init并保持离线");
    WDF_RUN_TEST(test_all_boards_offline_safe_stop_keeps_running,
                 "",
                 "验证全板离线只做安全停机且进程继续");
    WDF_RUN_TEST(test_runtime_sdk_calls_share_one_worker, "", "验证运行期SDK调用共享唯一worker");
    WDF_RUN_TEST(test_unconfigured_di_filter_not_written_on_online, "", "验证未配置滤波的脚上线不下发");
    WDF_RUN_TEST(test_di_set_filter_ms_writes_on_online, "", "验证先配置滤波再上线会下发0x2006");
    WDF_RUN_TEST(test_di_set_filter_ms_zero_is_written, "", "验证滤波时间0也会下发");
    WDF_RUN_TEST(test_di_set_filter_ms_rewrites_on_recover, "", "验证子板恢复后重下发已配置滤波");
    WDF_RUN_TEST(test_di_set_filter_ms_rejects_invalid_pin, "", "验证滤波配置拒绝非法句柄");
    WDF_RUN_TEST(test_di_set_filter_ms_failure_keeps_board_online, "", "验证滤波下发失败不把子板打成离线");

    return UNITY_END();
}
