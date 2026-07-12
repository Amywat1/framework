/**
 * @file    test_wash_orchestrator.c
 * @brief   wash_orchestrator 单元测试
 */

#include "adapters/outbound/hal/sim/engine_io_sim.h"
#include "adapters/outbound/storage/json/engine_program_json.h"
#include "application/orchestrators/wash_orchestrator.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/scheduler.h"
#include "unity.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef TEST_WASH_PROGRAM_PATH
#define TEST_WASH_PROGRAM_PATH "/tmp/wdf_wash_orchestrator_program.json"
#endif

static int s_deferred_stop_count;

static void test_deferred_stop_all(void)
{
    s_deferred_stop_count++;
}

static const machine_ops_t s_machine_ops = {
    .deferred_stop_all       = test_deferred_stop_all,
    .safety_home             = NULL,
    .home_device             = NULL,
    .execute_manual_actuator = NULL,
    .stop_all_outputs        = NULL,
};

static const char *const s_program_json
    = "{"
      "\"program\":{"
      "\"schema_version\":\"1.0\","
      "\"id\":\"orchestrator_unit\","
      "\"interlocks\":[{\"id\":\"estop\",\"condition\":\"ESTOP == 1\",\"action\":\"halt_all\","
      "\"priority\":0,\"reset_condition\":\"ESTOP == 0\",\"auto_reset\":false}],"
      "\"phases\":[{"
      "\"id\":\"p0\","
      "\"entry_guard\":\"true\","
      "\"exit_guard\":\"phase.elapsed_ms >= 100\","
      "\"timeout_ms\":1000,"
      "\"on_exit\":[{\"io_set\":{\"channel\":\"AOUT\",\"value\":0}}],"
      "\"lanes\":[{\"id\":\"lane\",\"steps\":["
      "{\"id\":\"a\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
      "\"actions\":[{\"io_set\":{\"channel\":\"AOUT\",\"value\":1}}],\"done\":{\"type\":\"actions_complete\"}}"
      "]}]"
      "}]"
      "}"
      "}";

static void write_program_file(void)
{
    FILE *fp = fopen(TEST_WASH_PROGRAM_PATH, "wb");

    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_UINT((unsigned)strlen(s_program_json),
                           (unsigned)fwrite(s_program_json, 1U, strlen(s_program_json), fp));
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));
}

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_wash_orchestrator_start_runs_engine_to_done(void)
{
    s_deferred_stop_count = 0;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    engine_io_sim_register();
    engine_io_sim_reset();
    engine_program_json_register_loader();
    machine_ops_register(&s_machine_ops);
    write_program_file();

    TEST_ASSERT_EQUAL_INT(SW_OK, wash_orchestrator_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, scheduler_start_all());

    TEST_ASSERT_FALSE(wash_orchestrator_is_busy());
    TEST_ASSERT_EQUAL_INT(SW_OK, wash_orchestrator_start(WASH_MODE_STANDARD));
    TEST_ASSERT_TRUE(wash_orchestrator_is_busy());

    usleep(250000U);

    TEST_ASSERT_FALSE(wash_orchestrator_is_busy());
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("AOUT"));
    TEST_ASSERT_EQUAL_INT(1, s_deferred_stop_count);
    TEST_ASSERT_EQUAL_INT(ENGINE_DIR_NONE, wash_orchestrator_current_direction());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wash_orchestrator_start_runs_engine_to_done);
    return UNITY_END();
}
