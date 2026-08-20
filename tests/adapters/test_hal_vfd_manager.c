/**
 * @file    test_hal_vfd_manager.c
 * @brief   hal_vfd_manager 单元测试
 */

#include "adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/ports/outbound/hal/hal_vfd_port.h"
#include "wdf_test_spec.h"

#include <string.h>
#include <unistd.h>

#define TEST_VFD_ID 0

typedef struct {
    hal_vfd_state_t state;
    hal_vfd_gear_t  last_gear;
    uint16_t        freq;
    uint16_t        fault_code;
    uint16_t        current;
    bool            has_rst_pin;
    bool            rst_level;
    bool            force_comm_fail;
    unsigned        apply_count;
    unsigned        frequency_count;
    unsigned        stop_count;
    unsigned        clear_fault_count;
    unsigned        read_count;
} mock_vfd_t;

static mock_vfd_t s_vfd;
static int        s_events[16];
static unsigned   s_event_count;

void hal_vfd_manager_test_tick(void);
void hal_vfd_manager_test_reset(void);

static sw_err_t mock_apply_gear(void *ctx, hal_vfd_gear_t gear)
{
    mock_vfd_t *vfd = (mock_vfd_t *)ctx;

    vfd->last_gear = gear;
    vfd->state     = (gear > 0) ? HAL_VFD_STATE_FWD : HAL_VFD_STATE_REV;
    vfd->apply_count++;
    return SW_OK;
}

static sw_err_t mock_stop_outputs(void *ctx)
{
    mock_vfd_t *vfd = (mock_vfd_t *)ctx;

    vfd->last_gear = 0;
    vfd->state     = HAL_VFD_STATE_STOPPED;
    vfd->stop_count++;
    return SW_OK;
}

static sw_err_t mock_apply_frequency(void *ctx, hal_vfd_frequency_t frequency_centi_hz)
{
    mock_vfd_t *vfd = (mock_vfd_t *)ctx;

    vfd->freq  = (uint16_t)((frequency_centi_hz < 0) ? -frequency_centi_hz : frequency_centi_hz);
    vfd->state = (frequency_centi_hz > 0) ? HAL_VFD_STATE_FWD : HAL_VFD_STATE_REV;
    vfd->frequency_count++;
    return SW_OK;
}

static sw_err_t mock_set_rst(void *ctx, bool level)
{
    mock_vfd_t *vfd = (mock_vfd_t *)ctx;

    vfd->rst_level = level;
    return SW_OK;
}

static sw_err_t mock_read(void *ctx, hal_vfd_reg_t reg, uint16_t *p_val)
{
    mock_vfd_t *vfd = (mock_vfd_t *)ctx;

    vfd->read_count++;
    if (vfd->force_comm_fail) {
        return SW_ERR_COMM;
    }
    if (p_val == NULL) {
        return SW_ERR_PARAM;
    }

    switch (reg) {
    case HAL_VFD_REG_FAULT_CODE:
        *p_val = vfd->fault_code;
        return SW_OK;
    case HAL_VFD_REG_CURRENT:
        *p_val = vfd->current;
        return SW_OK;
    case HAL_VFD_REG_STATE:
        *p_val = (uint16_t)vfd->state;
        return SW_OK;
    default:
        return SW_ERR_PARAM;
    }
}

static sw_err_t mock_clear_fault(void *ctx)
{
    mock_vfd_t *vfd = (mock_vfd_t *)ctx;

    vfd->clear_fault_count++;
    vfd->fault_code = 0U;
    return SW_OK;
}

static hal_vfd_state_t mock_get_state(void *ctx)
{
    mock_vfd_t *vfd = (mock_vfd_t *)ctx;

    return vfd->state;
}

static bool mock_has_rst_pin(void *ctx)
{
    mock_vfd_t *vfd = (mock_vfd_t *)ctx;

    return vfd->has_rst_pin;
}

static bool mock_has_clear_fault(void *ctx)
{
    return ctx != NULL;
}

static const hal_vfd_backend_ops_t s_backend_ops = {
    .apply_gear      = mock_apply_gear,
    .apply_frequency = mock_apply_frequency,
    .stop_outputs    = mock_stop_outputs,
    .set_rst         = mock_set_rst,
    .read            = mock_read,
    .clear_fault     = mock_clear_fault,
    .has_clear_fault = mock_has_clear_fault,
    .get_state       = mock_get_state,
    .has_rst_pin     = mock_has_rst_pin,
};

static hal_vfd_manager_bind_cfg_t make_cfg(void)
{
    hal_vfd_manager_bind_cfg_t cfg = {
        .ops               = &s_backend_ops,
        .drv_ctx           = &s_vfd,
        .rst_pulse_ms      = 5U,
        .fault_period_ms   = 1U,
        .current_period_ms = 1U,
        .monitor_mask      = HAL_VFD_MON_NONE,
    };

    return cfg;
}

static void event_cb(int event_code)
{
    if (s_event_count < (sizeof(s_events) / sizeof(s_events[0]))) {
        s_events[s_event_count++] = event_code;
    }
}

static void run_due_tick(void)
{
    usleep(2000);
    hal_vfd_manager_test_tick();
}

static void bind_default(void)
{
    hal_vfd_manager_bind_cfg_t cfg = make_cfg();

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_manager_bind(TEST_VFD_ID, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->init());
}

void setUp(void)
{
    memset(&s_vfd, 0, sizeof(s_vfd));
    memset(s_events, 0, sizeof(s_events));
    s_event_count         = 0U;
    s_vfd.state           = HAL_VFD_STATE_STOPPED;
    s_vfd.has_rst_pin     = false;
    s_vfd.current         = 123U;
    s_vfd.fault_code      = 0U;
    s_vfd.force_comm_fail = false;

    time_util_init();
    hal_vfd_manager_test_reset();
    hal_vfd_manager_register();
    TEST_ASSERT_NOT_NULL(hal_vfd_get_ops());
}

void tearDown(void)
{
}

static void test_bind_rejects_invalid_config(void)
{
    hal_vfd_manager_bind_cfg_t cfg = make_cfg();
    hal_vfd_backend_ops_t      invalid_ops;

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_vfd_manager_bind(-1, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_vfd_manager_bind(HAL_VFD_MANAGER_SLOT_MAX, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_vfd_manager_bind(TEST_VFD_ID, NULL));

    cfg.ops = NULL;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_vfd_manager_bind(TEST_VFD_ID, &cfg));

    invalid_ops                 = s_backend_ops;
    invalid_ops.has_clear_fault = NULL;
    cfg                         = make_cfg();
    cfg.ops                     = &invalid_ops;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_vfd_manager_bind(TEST_VFD_ID, &cfg));

    cfg              = make_cfg();
    cfg.rst_pulse_ms = 0U;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_vfd_manager_bind(TEST_VFD_ID, &cfg));

    cfg                 = make_cfg();
    cfg.fault_period_ms = 0U;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_vfd_manager_bind(TEST_VFD_ID, &cfg));

    cfg                   = make_cfg();
    cfg.current_period_ms = 0U;
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, hal_vfd_manager_bind(TEST_VFD_ID, &cfg));

    cfg = make_cfg();
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_manager_bind(TEST_VFD_ID, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_ERR_BUSY, hal_vfd_manager_bind(TEST_VFD_ID, &cfg));
}

static void test_unbound_operations_return_not_init(void)
{
    uint16_t val;

    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_vfd_get_ops()->set_gear(TEST_VFD_ID, 1));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_vfd_get_ops()->set_frequency(TEST_VFD_ID, 50));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_vfd_get_ops()->stop(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_vfd_get_ops()->fault_reset(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_vfd_get_ops()->read(TEST_VFD_ID, HAL_VFD_REG_CURRENT, &val));
    TEST_ASSERT_EQUAL_INT(SW_ERR_NOT_INIT, hal_vfd_get_ops()->get_cached(TEST_VFD_ID, HAL_VFD_REG_CURRENT, &val));
}

static void test_control_mode_is_selected_by_api_and_switch_requires_stop(void)
{
    bind_default();

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->set_gear(TEST_VFD_ID, 2));
    TEST_ASSERT_EQUAL_INT(2, s_vfd.last_gear);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_STATE_FWD, hal_vfd_get_ops()->get_state(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_UINT(1U, s_vfd.apply_count);

    TEST_ASSERT_EQUAL_UINT(0U, s_vfd.frequency_count);
    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, hal_vfd_get_ops()->set_frequency(TEST_VFD_ID, 45));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->stop(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->set_frequency(TEST_VFD_ID, -45));
    TEST_ASSERT_EQUAL_UINT16(45U, s_vfd.freq);
    TEST_ASSERT_EQUAL_UINT(1U, s_vfd.frequency_count);
    TEST_ASSERT_EQUAL_UINT(1U, s_vfd.apply_count);

    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, hal_vfd_get_ops()->set_gear(TEST_VFD_ID, 1));

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->stop(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_UINT(2U, s_vfd.stop_count);
}

static void test_fault_reset_uses_modbus_clear_when_no_rst_pin(void)
{
    bind_default();
    s_vfd.fault_code = 77U;

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->fault_reset(TEST_VFD_ID));
    TEST_ASSERT_EQUAL_UINT(1U, s_vfd.stop_count);
    TEST_ASSERT_EQUAL_UINT(1U, s_vfd.clear_fault_count);
    TEST_ASSERT_EQUAL_UINT16(0U, s_vfd.fault_code);
    TEST_ASSERT_FALSE(s_vfd.rst_level);
}

static void test_fault_reset_uses_rst_pulse_when_pin_exists(void)
{
    bind_default();
    s_vfd.has_rst_pin = true;

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->fault_reset(TEST_VFD_ID));
    TEST_ASSERT_TRUE(s_vfd.rst_level);
    TEST_ASSERT_EQUAL_UINT(1U, s_vfd.stop_count);
    TEST_ASSERT_EQUAL_UINT(0U, s_vfd.clear_fault_count);

    usleep(10000);
    hal_vfd_manager_test_tick();
    TEST_ASSERT_FALSE(s_vfd.rst_level);
}

static void test_monitor_updates_cached_fault_and_current_and_events(void)
{
    uint16_t val;

    bind_default();
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_manager_set_monitor_mask(TEST_VFD_ID, HAL_VFD_MON_ALL));
    hal_vfd_get_ops()->register_event_cb(TEST_VFD_ID, event_cb);
    s_vfd.state      = HAL_VFD_STATE_FWD;
    s_vfd.fault_code = 99U;
    s_vfd.current    = 456U;

    run_due_tick();

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->get_cached(TEST_VFD_ID, HAL_VFD_REG_FAULT_CODE, &val));
    TEST_ASSERT_EQUAL_UINT16(99U, val);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->get_cached(TEST_VFD_ID, HAL_VFD_REG_CURRENT, &val));
    TEST_ASSERT_EQUAL_UINT16(456U, val);
    TEST_ASSERT_EQUAL_UINT(2U, s_event_count);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_FAULT_DETECTED, s_events[0]);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_CURRENT_UPDATE, s_events[1]);

    s_vfd.fault_code = 0U;
    run_due_tick();
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_FAULT_CLEARED, s_events[2]);
}

static void test_monitor_reports_comm_lost_and_restored(void)
{
    bind_default();
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_manager_set_monitor_mask(TEST_VFD_ID, HAL_VFD_MON_FAULT));
    hal_vfd_get_ops()->register_event_cb(TEST_VFD_ID, event_cb);
    s_vfd.force_comm_fail = true;

    run_due_tick();
    run_due_tick();
    run_due_tick();

    TEST_ASSERT_EQUAL_UINT(1U, s_event_count);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_COMM_LOST, s_events[0]);

    s_vfd.force_comm_fail = false;
    run_due_tick();

    TEST_ASSERT_EQUAL_UINT(2U, s_event_count);
    TEST_ASSERT_EQUAL_INT(HAL_VFD_EVT_COMM_RESTORED, s_events[1]);
}

static void test_monitor_independent_periods_current_faster_than_fault(void)
{
    hal_vfd_manager_bind_cfg_t cfg = make_cfg();
    uint16_t                   val;

    cfg.fault_period_ms   = 100000U;
    cfg.current_period_ms = 1U;

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_manager_bind(TEST_VFD_ID, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->init());
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_manager_set_monitor_mask(TEST_VFD_ID, HAL_VFD_MON_ALL));

    s_vfd.state      = HAL_VFD_STATE_FWD;
    s_vfd.fault_code = 55U;
    s_vfd.current    = 111U;
    /* 绑定后首次 tick 两个指标都尚未采样过（last_*_ms == 0），必然各自采样一次 */
    run_due_tick();

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->get_cached(TEST_VFD_ID, HAL_VFD_REG_CURRENT, &val));
    TEST_ASSERT_EQUAL_UINT16(111U, val);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->get_cached(TEST_VFD_ID, HAL_VFD_REG_FAULT_CODE, &val));
    TEST_ASSERT_EQUAL_UINT16(55U, val);

    /* 之后：电流周期 1ms，每次 tick 都到期；故障码周期 100000ms，短时间内不会再次到期 */
    s_vfd.fault_code = 77U;
    s_vfd.current    = 222U;
    run_due_tick();

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->get_cached(TEST_VFD_ID, HAL_VFD_REG_CURRENT, &val));
    TEST_ASSERT_EQUAL_UINT16(222U, val);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->get_cached(TEST_VFD_ID, HAL_VFD_REG_FAULT_CODE, &val));
    TEST_ASSERT_EQUAL_UINT16(55U, val);

    s_vfd.current = 333U;
    run_due_tick();

    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->get_cached(TEST_VFD_ID, HAL_VFD_REG_CURRENT, &val));
    TEST_ASSERT_EQUAL_UINT16(333U, val);
    TEST_ASSERT_EQUAL_INT(SW_OK, hal_vfd_get_ops()->get_cached(TEST_VFD_ID, HAL_VFD_REG_FAULT_CODE, &val));
    TEST_ASSERT_EQUAL_UINT16(55U, val);
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_bind_rejects_invalid_config, "", "验证绑定拒绝无效配置");
    WDF_RUN_TEST(test_unbound_operations_return_not_init, "", "验证未绑定操作返回未初始化");
    WDF_RUN_TEST(
        test_control_mode_is_selected_by_api_and_switch_requires_stop, "", "验证 API 选择控制模式且切换前必须停止");
    WDF_RUN_TEST(test_fault_reset_uses_modbus_clear_when_no_rst_pin, "", "验证无复位引脚时通过 Modbus 清除故障");
    WDF_RUN_TEST(test_fault_reset_uses_rst_pulse_when_pin_exists, "", "验证存在复位引脚时通过脉冲清除故障");
    WDF_RUN_TEST(test_monitor_updates_cached_fault_and_current_and_events, "", "验证监控更新缓存的故障、电流和事件");
    WDF_RUN_TEST(test_monitor_reports_comm_lost_and_restored, "", "验证监控上报通信丢失并恢复");
    WDF_RUN_TEST(test_monitor_independent_periods_current_faster_than_fault, "", "验证电流监控周期独立且快于故障监控");

    return UNITY_END();
}
