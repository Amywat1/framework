/**
 * @file    home_orchestrator.c
 * @brief   全机归位编排器（engine tick 驱动归位方案）
 * @author  HUWANGWEI
 * @date    2026-07-17
 *
 * @note    start() 阻塞等待 engine_start；完成后发布 EVT_OP_MODE_HOME_COMPLETED。
 */

#include "application/orchestrators/home_orchestrator.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/time_util.h"
#include "domain/wash/engine/engine.h"
#include "domain/wash/model/engine_model.h"
#include "domain/wash/model/engine_program_manifest.h"
#include "ports/outbound/machine/machine_ops_port.h"
#include "ports/outbound/storage/engine_program_loader_port.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/thread_registry.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <unistd.h>

#ifndef HOME_PROGRAM_PATH
#define HOME_PROGRAM_PATH "/etc/m8/m8_home_program.json"
#endif

#ifndef PROGRAM_INTEGRITY_CHECK
#define PROGRAM_INTEGRITY_CHECK 1
#endif

#define STEP_POLL_INTERVAL_MS 50U
#define HOME_TOTAL_TIMEOUT_MS 180000U
#define HOME_STARTUP_WAIT_MS  4500U

static sem_t           s_start_sem;
static sem_t           s_startup_done;
static pthread_mutex_t s_startup_mutex      = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool     s_busy               = false;
static atomic_bool     s_terminate          = false;
static atomic_bool     s_abort_requested    = false;
static uint32_t        s_startup_gen        = 0U;
static uint32_t        s_active_startup_gen = 0U;
static sw_err_t        s_startup_result     = SW_ERR_BUSY;

static void home_stop_outputs(void)
{
    const machine_ops_t *ops = machine_ops_get();

    if ((ops != NULL) && (ops->stop_all_outputs != NULL)) {
        (void)ops->stop_all_outputs();
    } else if ((ops != NULL) && (ops->deferred_stop_all != NULL)) {
        ops->deferred_stop_all();
    }
}

static void signal_startup(uint32_t gen, sw_err_t result)
{
    bool should_post = false;

    pthread_mutex_lock(&s_startup_mutex);
    if (s_active_startup_gen == gen) {
        s_startup_result = result;
        should_post      = true;
    }
    pthread_mutex_unlock(&s_startup_mutex);

    if (should_post) {
        (void)sem_post(&s_startup_done);
    }
}

static void invalidate_startup_gen(uint32_t gen)
{
    pthread_mutex_lock(&s_startup_mutex);
    if (s_active_startup_gen == gen) {
        s_active_startup_gen = 0U;
    }
    pthread_mutex_unlock(&s_startup_mutex);
}

static sw_err_t worker_prepare_engine(engine_t **out_engine)
{
    const char       *prog_path = HOME_PROGRAM_PATH;
    engine_program_t *prog      = NULL;
    engine_t         *e         = NULL;

    if (out_engine == NULL) {
        return SW_ERR_PARAM;
    }

    *out_engine = NULL;

#if PROGRAM_INTEGRITY_CHECK
    {
        char manifest_path[256] = {0};
        char manifest_err[256]  = {0};

        if (!engine_program_manifest_path_from_json(prog_path, manifest_path, (unsigned)sizeof(manifest_path))) {
            LOG_ERROR("home_worker: manifest path derive failed path=[%s]", prog_path);
            return SW_ERR_PARAM;
        }

        if (engine_program_manifest_verify(prog_path, manifest_path, manifest_err, (unsigned)sizeof(manifest_err))
            != SW_OK) {
            LOG_ERROR("home_worker: manifest verify failed json=[%s] err=[%s]", prog_path, manifest_err);
            return SW_ERR_CRC;
        }
    }
#endif

    {
        char err_buf[256] = {0};

        prog = engine_program_load(prog_path, err_buf, (unsigned)sizeof(err_buf));
        if (prog == NULL) {
            LOG_ERROR("home_worker: load program failed path=[%s] err=[%s]", prog_path, err_buf);
            return SW_ERR_PARAM;
        }
    }

    {
        engine_program_t *snapshot = engine_program_clone(prog);

        engine_program_free(prog);
        prog = snapshot;
        if (prog == NULL) {
            LOG_ERROR("home_worker: program snapshot clone OOM");
            return SW_ERR_NOMEM;
        }
    }

    e = engine_create();
    if (e == NULL) {
        LOG_ERROR("home_worker: engine_create OOM");
        engine_program_free(prog);
        return SW_ERR_NOMEM;
    }

    if (engine_load_program(e, prog) != SW_OK) {
        LOG_ERROR("home_worker: engine_load_program failed");
        engine_destroy(e);
        return SW_ERR_PARAM;
    }

    if (engine_start(e) != SW_OK) {
        LOG_ERROR("home_worker: engine_start failed");
        engine_destroy(e);
        return SW_ERR_STATE;
    }

    LOG_INFO("home_worker: prepare path=%s", prog_path);
    *out_engine = e;
    return SW_OK;
}

static void *home_worker_fn(void *arg)
{
    (void)arg;

    while (true) {
        uint32_t  startup_gen;
        engine_t *e = NULL;
        sw_err_t  prep_ret;

        sem_wait(&s_start_sem);

        if (atomic_load(&s_terminate)) {
            break;
        }

        pthread_mutex_lock(&s_startup_mutex);
        startup_gen = s_active_startup_gen;
        pthread_mutex_unlock(&s_startup_mutex);

        atomic_store(&s_abort_requested, false);
        prep_ret = worker_prepare_engine(&e);
        if (prep_ret != SW_OK) {
            signal_startup(startup_gen, prep_ret);
            atomic_store(&s_busy, false);
            continue;
        }

        {
            bool waiter_active;

            pthread_mutex_lock(&s_startup_mutex);
            waiter_active = (s_active_startup_gen == startup_gen);
            pthread_mutex_unlock(&s_startup_mutex);

            if (!waiter_active) {
                LOG_WARN("home_worker: startup waiter gone, teardown gen=%u", startup_gen);
                engine_destroy(e);
                atomic_store(&s_busy, false);
                continue;
            }
        }

        signal_startup(startup_gen, SW_OK);

        {
            uint32_t total_ms   = 0U;
            bool     timed_out = false;
            bool     aborted   = false;

            while (true) {
                if (atomic_load(&s_abort_requested)) {
                    aborted = true;
                    LOG_WARN("home_worker: abort at phase [%s]",
                             engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                    break;
                }

                total_ms += STEP_POLL_INTERVAL_MS;
                if (total_ms >= HOME_TOTAL_TIMEOUT_MS) {
                    timed_out = true;
                    LOG_ERROR("home_worker: total timeout %u ms at phase [%s]",
                              (unsigned)HOME_TOTAL_TIMEOUT_MS,
                              engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                    break;
                }

                engine_tick(e, STEP_POLL_INTERVAL_MS);

                {
                    engine_run_state_t st = engine_state(e);

                    if ((st == ENGINE_STATE_DONE) || (st == ENGINE_STATE_HALTED)) {
                        break;
                    }

                    /* 归位不做 phase 恢复：halt_phase 视为失败 */
                    if (st == ENGINE_STATE_PHASE_HALTED) {
                        LOG_ERROR("home_worker: phase halted at [%s]",
                                  engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                        break;
                    }
                }

                usleep((unsigned long)STEP_POLL_INTERVAL_MS * 1000UL);
            }

            {
                engine_run_state_t final_state = engine_state(e);
                bool               success
                    = (final_state == ENGINE_STATE_DONE) && !timed_out && !aborted;

                engine_destroy(e);
                home_stop_outputs();

                (void)event_publish(EVT_OP_MODE_HOME_COMPLETED, success ? 1U : 0U);
                if (success) {
                    LOG_INFO("home_worker: home done");
                } else {
                    LOG_WARN("home_worker: home failed engine_state=%d timed_out=%d aborted=%d",
                             (int)final_state, (int)timed_out, (int)aborted);
                }
            }
        }

        atomic_store(&s_busy, false);
    }

    LOG_INFO("home_worker: thread exit");
    return NULL;
}

sw_err_t home_orchestrator_init(void)
{
    atomic_store(&s_busy, false);
    atomic_store(&s_terminate, false);
    atomic_store(&s_abort_requested, false);

    if (sem_init(&s_start_sem, 0, 0) != 0) {
        LOG_ERROR("home_orchestrator_init: sem_init start_sem failed");
        return SW_ERR_HW;
    }

    if (sem_init(&s_startup_done, 0, 0) != 0) {
        LOG_ERROR("home_orchestrator_init: sem_init startup_done failed");
        return SW_ERR_HW;
    }

    {
        sw_err_t ret = thread_register("home_worker", home_worker_fn, SCHED_OTHER, 0, THD_HOME_WORKER_STACK);

        if (ret != SW_OK) {
            return ret;
        }
    }

    LOG_INFO("home_orchestrator: init ok");
    return SW_OK;
}

sw_err_t home_orchestrator_start(void)
{
    uint32_t        my_gen;
    struct timespec ts;
    int             sem_ret;
    sw_err_t        ret;

    if (atomic_load(&s_busy)) {
        LOG_WARN("home_orchestrator_start: busy");
        return SW_ERR_BUSY;
    }

    pthread_mutex_lock(&s_startup_mutex);
    my_gen               = ++s_startup_gen;
    s_active_startup_gen = my_gen;
    s_startup_result     = SW_ERR_BUSY;
    pthread_mutex_unlock(&s_startup_mutex);

    atomic_store(&s_abort_requested, false);
    atomic_store(&s_busy, true);
    sem_post(&s_start_sem);

    time_util_fill_deadline(HOME_STARTUP_WAIT_MS, &ts);
    sem_ret = sem_timedwait(&s_startup_done, &ts);
    if (sem_ret != 0) {
        invalidate_startup_gen(my_gen);
        LOG_ERROR("home_orchestrator_start: startup wait timeout");
        return SW_ERR_TIMEOUT;
    }

    pthread_mutex_lock(&s_startup_mutex);
    if (s_active_startup_gen != my_gen) {
        pthread_mutex_unlock(&s_startup_mutex);
        return SW_ERR_TIMEOUT;
    }

    ret = s_startup_result;
    pthread_mutex_unlock(&s_startup_mutex);

    if (ret != SW_OK) {
        LOG_WARN("home_orchestrator_start: worker startup failed ret=%d", (int)ret);
    }

    return ret;
}

void home_orchestrator_abort(void)
{
    atomic_store(&s_abort_requested, true);
    home_stop_outputs();
    LOG_WARN("home_orchestrator: abort requested");
}

bool home_orchestrator_is_busy(void)
{
    return (bool)atomic_load(&s_busy);
}
