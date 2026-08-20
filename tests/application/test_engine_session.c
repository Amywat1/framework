/**
 * @file    test_engine_session.c
 * @brief   engine_session 单元测试
 */

#include "adapters/outbound/hal/sim/engine_actuator_sim.h"
#include "adapters/outbound/hal/sim/engine_io_sim.h"
#include "adapters/outbound/storage/json/engine_program_json.h"
#include "application/engine_session/engine_session.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/scheduler.h"
#include "wdf_test_spec.h"

#include <pthread.h>
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
static bool s_last_aborted;

typedef struct {
    engine_session_t    *session;
    engine_session_run_t run;
    pthread_barrier_t   *barrier;
    sw_err_t             result;
} concurrent_start_arg_t;

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
    s_last_aborted = result->aborted;
}

static engine_session_run_t make_run(void)
{
    engine_session_run_t run;

    memset(&run, 0, sizeof(run));
    run.program_path         = TEST_ENGINE_SESSION_PROGRAM_PATH;
    run.total_timeout_ms     = 5000U;
    run.max_phase_recoveries = 5U;
    run.on_started           = on_started;
    run.on_finished          = on_finished;
    run.on_stop_outputs      = on_stop;
    return run;
}

static void *concurrent_start(void *arg)
{
    concurrent_start_arg_t *start_arg = (concurrent_start_arg_t *)arg;

    (void)pthread_barrier_wait(start_arg->barrier);
    start_arg->result = engine_session_start(start_arg->session, &start_arg->run);
    return NULL;
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
      "\"on_exit\":[{\"act\":{\"resource\":\"aout\",\"cmd\":\"stop\"}}],"
      "\"lanes\":[{\"id\":\"lane\",\"steps\":["
      "{\"id\":\"a\",\"type\":\"event\",\"trigger\":{\"type\":\"condition\",\"expr\":\"true\"},"
      "\"actions\":[{\"act\":{\"resource\":\"aout\",\"cmd\":\"run\",\"gear\":1}}],\"done\":{\"type\":\"actions_"
      "complete\"}}"
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
    engine_session_t       *session = NULL;

    s_stop_count     = 0;
    s_started_count  = 0;
    s_finished_count = 0;
    s_last_success   = false;
    s_last_aborted   = false;

    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    engine_io_sim_reset();
    engine_actuator_sim_reset();
    engine_program_json_register_loader();
    write_program_file();

    memset(&cfg, 0, sizeof(cfg));
    cfg.thread_name          = "test_engine_session";
    cfg.stack_size           = THD_WASH_WORKER_STACK;
    cfg.tick_ms              = 50U;
    cfg.startup_wait_ms      = 4500U;
    cfg.integrity_check      = false;
    cfg.environment.io       = engine_io_sim_instance();
    cfg.environment.actuator = engine_actuator_sim_instance();

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, engine_session_bind(WDF_ENGINE_SESSION_INSTANCE_COUNT, &cfg, &session));
    TEST_ASSERT_NULL(session);
    TEST_ASSERT_EQUAL_INT(SW_OK, engine_session_bind(0U, &cfg, &session));
    TEST_ASSERT_NOT_NULL(session);
    {
        engine_session_t *duplicate = NULL;

        TEST_ASSERT_EQUAL_INT(SW_ERR_BUSY, engine_session_bind(0U, &cfg, &duplicate));
        TEST_ASSERT_NULL(duplicate);
    }
    TEST_ASSERT_EQUAL_INT(SW_OK, scheduler_start_all());

    {
        pthread_barrier_t      barrier;
        pthread_t              threads[2];
        concurrent_start_arg_t args[2];
        int                    ok_count   = 0;
        int                    busy_count = 0;

        TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&barrier, NULL, 3U));
        for (int i = 0; i < 2; ++i) {
            memset(&args[i], 0, sizeof(args[i]));
            args[i].session             = session;
            args[i].run                 = make_run();
            args[i].run.on_started      = NULL;
            args[i].run.on_finished     = NULL;
            args[i].run.on_stop_outputs = NULL;
            args[i].barrier             = &barrier;
            args[i].result              = SW_ERR_STATE;
            TEST_ASSERT_EQUAL_INT(0, pthread_create(&threads[i], NULL, concurrent_start, &args[i]));
        }

        (void)pthread_barrier_wait(&barrier);
        for (int i = 0; i < 2; ++i) {
            TEST_ASSERT_EQUAL_INT(0, pthread_join(threads[i], NULL));
            if (args[i].result == SW_OK) {
                ++ok_count;
            } else if (args[i].result == SW_ERR_BUSY) {
                ++busy_count;
            }
        }
        TEST_ASSERT_EQUAL_INT(1, ok_count);
        TEST_ASSERT_EQUAL_INT(1, busy_count);
        TEST_ASSERT_EQUAL_INT(0, pthread_barrier_destroy(&barrier));

        usleep(250000U);
        TEST_ASSERT_FALSE(engine_session_is_busy(session));
    }

    {
        engine_session_run_t run = make_run();

        TEST_ASSERT_FALSE(engine_session_is_busy(session));
        TEST_ASSERT_EQUAL_INT(SW_OK, engine_session_start(session, &run));
        TEST_ASSERT_TRUE(engine_session_is_busy(session));
    }

    usleep(250000U);

    TEST_ASSERT_FALSE(engine_session_is_busy(session));
    TEST_ASSERT_EQUAL_INT(0, engine_actuator_sim_active("aout"));
    TEST_ASSERT_EQUAL_INT(1, s_started_count);
    TEST_ASSERT_EQUAL_INT(1, s_finished_count);
    TEST_ASSERT_TRUE(s_last_success);
    TEST_ASSERT_FALSE(s_last_aborted);
    TEST_ASSERT_EQUAL_INT(1, s_stop_count);
    TEST_ASSERT_EQUAL_INT(ENGINE_DIR_NONE, engine_session_direction(session));

    TEST_ASSERT_FALSE(engine_session_abort(NULL));
    TEST_ASSERT_FALSE(engine_session_abort(session));
    TEST_ASSERT_EQUAL_INT(1, s_stop_count);

    {
        engine_session_run_t run = make_run();

        s_stop_count     = 0;
        s_finished_count = 0;
        s_last_success   = true;
        s_last_aborted   = false;

        TEST_ASSERT_EQUAL_INT(SW_OK, engine_session_start(session, &run));
        TEST_ASSERT_TRUE(engine_session_abort(session));
        TEST_ASSERT_FALSE(engine_session_abort(session));
        TEST_ASSERT_EQUAL_INT(1, s_stop_count);
    }

    usleep(100000U);

    TEST_ASSERT_FALSE(engine_session_is_busy(session));
    TEST_ASSERT_EQUAL_INT(1, s_finished_count);
    TEST_ASSERT_FALSE(s_last_success);
    TEST_ASSERT_TRUE(s_last_aborted);
    TEST_ASSERT_EQUAL_INT(2, s_stop_count);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_engine_session_runs_to_done, "", "验证程序引擎会话运行到完成");
    return UNITY_END();
}
