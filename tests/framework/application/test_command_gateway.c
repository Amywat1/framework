/**
 * @file    test_command_gateway.c
 * @brief   command_gateway RPC 单元测试
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "framework/application/command_gateway.h"
#include "framework/domain/command_gateway/operational_mode.h"
#include "framework/ports/inbound/command/command_port.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/common/time_util.h"
#include "unity.h"

#include <pthread.h>
#include <unistd.h>

static void *dispatch_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

static pthread_t start_dispatch(void)
{
    pthread_t tid;

    (void)pthread_create(&tid, NULL, dispatch_fn, NULL);
    usleep(10000);
    return tid;
}

static void stop_dispatch(pthread_t tid)
{
    (void)event_bus_shutdown();
    (void)pthread_join(tid, NULL);
}

void setUp(void)
{
    (void)time_util_init();
    (void)event_bus_init();
    (void)dev_ctx_init();
    (void)operational_mode_init();
    (void)command_gateway_init();
}

void tearDown(void)
{
}

static void test_inject_enter_manual_succeeds(void)
{
    pthread_t tid = start_dispatch();
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t cmd = { .type = CMD_ENTER_MANUAL };
    sw_err_t ret;

    TEST_ASSERT_NOT_NULL(cp);
    TEST_ASSERT_NOT_NULL(cp->inject);

    ret = cp->inject(&cmd);
    TEST_ASSERT_EQUAL_INT(SW_OK, (int)ret);
    TEST_ASSERT_EQUAL_INT(OP_MODE_MANUAL, (int)op_mode_get_current());

    stop_dispatch(tid);
}

static void test_inject_denied_when_busy_mode(void)
{
    pthread_t tid = start_dispatch();
    const command_port_ops_t *cp = command_port_get_ops();
    cmd_t enter = { .type = CMD_ENTER_MANUAL };
    cmd_t start = { .type = CMD_START_WASH };

    start.payload.start_wash.mode = WASH_MODE_STANDARD;

    (void)cp->inject(&enter);
    TEST_ASSERT_EQUAL_INT(OP_MODE_MANUAL, (int)op_mode_get_current());

    TEST_ASSERT_EQUAL_INT(SW_ERR_STATE, (int)cp->inject(&start));

    stop_dispatch(tid);
}

static void test_home_device_allowed_in_exception(void)
{
    cmd_t home = { .type = CMD_HOME_DEVICE };
    op_command_result_t r;

    op_mode_on_critical_alarm();
    TEST_ASSERT_EQUAL_INT(OP_MODE_EXCEPTION, (int)op_mode_get_current());

    r = op_mode_handle_command(&home);
    TEST_ASSERT_EQUAL_INT(OP_CMD_ALLOWED, (int)r.result);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_inject_enter_manual_succeeds);
    RUN_TEST(test_inject_denied_when_busy_mode);
    RUN_TEST(test_home_device_allowed_in_exception);
    return UNITY_END();
}
