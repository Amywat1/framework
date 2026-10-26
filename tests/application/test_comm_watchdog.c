/**
 * @file    test_comm_watchdog.c
 * @brief   通讯心跳轮询器：超时出报警、恢复消报警
 */

#include "application/comm_watchdog.h"
#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "common/sw_error.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <string.h>

static uint64_t s_now_ms;
static uint32_t s_trig_codes[8];
static unsigned s_trig_count;
static uint32_t s_clear_codes[8];
static unsigned s_clear_count;
static sw_err_t s_clear_ret;
static unsigned s_lost_ids[8];
static unsigned s_lost_count;
static bool     s_port_registered;

static sw_err_t stub_trigger(uint32_t alarm_code)
{
    if (s_trig_count < 8U) {
        s_trig_codes[s_trig_count++] = alarm_code;
    }
    return SW_OK;
}

static sw_err_t stub_clear(uint32_t alarm_code)
{
    if (s_clear_count < 8U) {
        s_clear_codes[s_clear_count++] = alarm_code;
    }
    return s_clear_ret;
}

static const alarm_binding_ops_t s_ops = {
    .trigger      = stub_trigger,
    .clear        = stub_clear,
    .load_catalog = NULL,
};

const alarm_binding_ops_t *alarm_binding_get_ops(void)
{
    return s_port_registered ? &s_ops : NULL;
}

uint64_t time_util_get_ms(void)
{
    return s_now_ms;
}

bool sw_mutex_init_prio_inherit(pthread_mutex_t *mutex)
{
    return pthread_mutex_init(mutex, NULL) == 0;
}

static void on_lost(unsigned id)
{
    if (s_lost_count < 8U) {
        s_lost_ids[s_lost_count++] = id;
    }
}

static const comm_watchdog_channel_t s_channels[] = {
    { 100U, 0x1001U },
    { 200U, 0x2002U },
};

void setUp(void)
{
    s_now_ms          = 1000U;
    s_trig_count      = 0U;
    s_clear_count     = 0U;
    s_clear_ret       = SW_OK;
    s_lost_count      = 0U;
    s_port_registered = true;
    memset(s_trig_codes, 0, sizeof(s_trig_codes));
    memset(s_clear_codes, 0, sizeof(s_clear_codes));
    memset(s_lost_ids, 0, sizeof(s_lost_ids));
    comm_watchdog_reset_for_test();
}

void tearDown(void)
{
}

static sw_err_t bind_and_init(void)
{
    sw_err_t ret = comm_watchdog_bind(s_channels, 2U, on_lost);

    if (ret != SW_OK) {
        return ret;
    }
    comm_watchdog_init();
    return SW_OK;
}

static void test_timeout_triggers_and_notifies_once(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, bind_and_init());
    s_now_ms += 101U;
    comm_watchdog_poll();
    comm_watchdog_poll();
    TEST_ASSERT_EQUAL_UINT(1U, s_lost_count);
    TEST_ASSERT_EQUAL_UINT(0U, s_lost_ids[0]);
    TEST_ASSERT_EQUAL_UINT(2U, s_trig_count);
    TEST_ASSERT_EQUAL_UINT32(0x1001U, s_trig_codes[0]);
    TEST_ASSERT_EQUAL_UINT32(0x1001U, s_trig_codes[1]);
    TEST_ASSERT_EQUAL_UINT(0U, s_clear_count);
}

static void test_heartbeat_keeps_channel_alive(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, bind_and_init());
    s_now_ms += 90U;
    comm_watchdog_heartbeat(0U);
    s_now_ms += 90U;
    comm_watchdog_poll();
    TEST_ASSERT_EQUAL_UINT(0U, s_trig_count);
    TEST_ASSERT_EQUAL_UINT(0U, s_lost_count);
}

static void test_recover_clears(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, bind_and_init());
    s_now_ms += 101U;
    comm_watchdog_poll();
    comm_watchdog_heartbeat(0U);
    comm_watchdog_poll();
    TEST_ASSERT_EQUAL_UINT(1U, s_clear_count);
    TEST_ASSERT_EQUAL_UINT32(0x1001U, s_clear_codes[0]);
}

static void test_clear_failure_keeps_lost(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, bind_and_init());
    s_now_ms += 101U;
    comm_watchdog_poll();
    s_clear_ret = SW_ERR_BUSY;
    comm_watchdog_heartbeat(0U);
    comm_watchdog_poll();
    TEST_ASSERT_EQUAL_UINT(1U, s_clear_count);
    s_clear_ret   = SW_OK;
    s_clear_count = 0U;
    comm_watchdog_poll();
    TEST_ASSERT_EQUAL_UINT(1U, s_clear_count);
}

static void test_unregistered_port_is_noop(void)
{
    TEST_ASSERT_EQUAL_INT(SW_OK, bind_and_init());
    s_port_registered = false;
    s_now_ms += 500U;
    comm_watchdog_poll();
    TEST_ASSERT_EQUAL_UINT(0U, s_trig_count);
    TEST_ASSERT_EQUAL_UINT(0U, s_lost_count);
}

static void test_zero_timeout_rejected(void)
{
    static const comm_watchdog_channel_t bad[] = { { 0U, 1U } };

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, comm_watchdog_bind(bad, 1U, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_timeout_triggers_and_notifies_once, "WD-01",
                 "超时出报警，丢失上升沿只回调一次");
    WDF_RUN_TEST(test_heartbeat_keeps_channel_alive, "WD-02",
                 "心跳刷新后未超时不出报警");
    WDF_RUN_TEST(test_recover_clears, "WD-03",
                 "恢复后请求清除报警");
    WDF_RUN_TEST(test_clear_failure_keeps_lost, "WD-03",
                 "清除失败则保持丢失，下拍再清");
    WDF_RUN_TEST(test_unregistered_port_is_noop, "WD-04",
                 "未注册报警口时 poll 不做事");
    WDF_RUN_TEST(test_zero_timeout_rejected, "WD-01",
                 "超时为 0 拒绝登记");
    return UNITY_END();
}
