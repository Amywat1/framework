/**
 * @file    test_engine_session.c
 * @brief   engine_session 单元测试
 */

#include "adapters/outbound/hal/sim/engine_io_sim.h"
#include "adapters/outbound/storage/json/engine_program_json.h"
#include "application/engine_session/engine_session.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/scheduler.h"
#include "unity.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef TEST_ENGINE_SESSION_PROGRAM_PATH
#define TEST_ENGINE_SESSION_PROGRAM_PATH "/tmp/wdf_engine_session_program.json"
#endif

static int  s_stop_count;
static int  s_started_count;
static int  s_finished_count;
static bool s_last_success;
static uint8_t s_session_buf[2048];

static void on_stop(void *user)
{
    (void)user;
    s_stop_count++;
}

static void on_started(void *user)
{
    (void)user;
    s_started_count++;
}

static void on_finished(void *user, const engine_session_result_t *result)
{
    (void)user;
    s_finished_count++;
    s_last_success = result->success;
}

static const char *const s_program_json
    = "{"
      "\"program\":{"
      "\"schema_version\":\"1.0\","
      "\"id\":\"session_unit\","
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
    FILE *fp = fopen(TEST_ENGINE_SESSION_PROGRAM_PATH, "wb");

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

static void test_engine_session_runs_to_done(void)
{
    engine_session_config_t cfg;

    s_stop_count     = 0;
    s_started_count  = 0;
    s_finished_count = 0;
    s_last_success   = false;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    engine_io_sim_register();
    engine_io_sim_reset();
    engine_program_json_register_loader();
    write_program_file();

    TEST_ASSERT_TRUE(engine_session_size() <= sizeof(s_session_buf));

    memset(&cfg, 0, sizeof(cfg));
    cfg.thread_name     = "test_engine_session";
    cfg.stack_size      = THD_WASH_WORKER_STACK;
    cfg.tick_ms         = 50U;
    cfg.startup_wait_ms = 4500U;
    cfg.integrity_check = false;

    TEST_ASSERT_EQUAL_INT(SW_OK, engine_session_init(s_session_buf, &cfg));
    TEST_ASSERT_EQUAL_INT(SW_OK, scheduler_start_all());

    {
        engine_session_run_t run;

        memset(&run, 0, sizeof(run));
        run.program_path         = TEST_ENGINE_SESSION_PROGRAM_PATH;
        run.total_timeout_ms     = 5000U;
        run.max_phase_recoveries = 5U;
        run.on_started           = on_started;
        run.on_finished          = on_finished;
        run.on_stop_outputs      = on_stop;

        TEST_ASSERT_FALSE(engine_session_is_busy(s_session_buf));
        TEST_ASSERT_EQUAL_INT(SW_OK, engine_session_start(s_session_buf, &run));
        TEST_ASSERT_TRUE(engine_session_is_busy(s_session_buf));
    }

    usleep(250000U);

    TEST_ASSERT_FALSE(engine_session_is_busy(s_session_buf));
    TEST_ASSERT_EQUAL_INT(0, engine_io_sim_get_output("AOUT"));
    TEST_ASSERT_EQUAL_INT(1, s_started_count);
    TEST_ASSERT_EQUAL_INT(1, s_finished_count);
    TEST_ASSERT_TRUE(s_last_success);
    TEST_ASSERT_EQUAL_INT(1, s_stop_count);
    TEST_ASSERT_EQUAL_INT(ENGINE_DIR_NONE, engine_session_direction(s_session_buf));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_engine_session_runs_to_done);
    return UNITY_END();
}
