/**
 * @file    wash_orchestrator.c
 * @brief   洗车流程编排器（engine tick 驱动）
 * @author  HUWANGWEI
 * @date    2026-06-27
 *
 * @note    StartWash 两阶段（§5.5）：
 *          - start() 仅调度 worker 并阻塞等待 engine_start 结果（RPC 回执依据）；
 *          - worker 单次 load/manifest 校验，成功后发布 EVT_WASH_SESSION_STARTED；
 *          - 启动失败不发布模式事件，模式保持 IDLE。
 */

#include "framework/application/orchestrators/wash_orchestrator.h"
#include "framework/runtime/scheduler/thread_registry.h"
#include "framework/services/dev_ctx/dev_ctx.h"
#include "framework/domain/device_control/mechanism/brush.h"
#include "framework/domain/device_control/mechanism/gantry.h"
#include "framework/domain/device_control/mechanism/water.h"
#include "framework/domain/wash/engine/engine.h"
#include "framework/domain/wash/model/engine_model.h"
#include "framework/domain/wash/model/engine_program_manifest.h"
#include "framework/domain/command_gateway/op_mode_types.h"
#include "framework/ports/outbound/storage/engine_program_loader_port.h"
#include "framework/runtime/event_bus/event_bus.h"
#include "framework/runtime/config/thread_config.h"
#include "framework/common/event_types.h"
#include "framework/common/log.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <time.h>

/* 洗车方案 JSON 路径（CMakeLists 通过 compile_definitions 覆盖） */
#ifndef WASH_PROGRAM_NORMAL_PATH
#define WASH_PROGRAM_NORMAL_PATH "/etc/m8/m8_normal_wash_program.json"
#endif
#ifndef WASH_PROGRAM_QUICK_PATH
#define WASH_PROGRAM_QUICK_PATH  WASH_PROGRAM_NORMAL_PATH
#endif

#ifndef PROGRAM_INTEGRITY_CHECK
#define PROGRAM_INTEGRITY_CHECK  1
#endif

#define STEP_POLL_INTERVAL_MS    50U
#define MAX_PHASE_RECOVERIES     5U
#define WASH_TOTAL_TIMEOUT_MS    600000U
#define WASH_STARTUP_WAIT_MS     4500U
#define WASH_ABORT_NONE          (-1)

static sem_t        s_start_sem;
static sem_t        s_startup_done;
static pthread_mutex_t s_startup_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool  s_busy             = false;
static atomic_bool  s_terminate        = false;
static atomic_int   s_abort_cause        = WASH_ABORT_NONE;
static wash_mode_t  s_mode             = WASH_MODE_STANDARD;
static uint32_t     s_startup_gen      = 0U;
static uint32_t     s_active_startup_gen = 0U;
static sw_err_t     s_startup_result     = SW_ERR_BUSY;
static atomic_int   s_current_direction;

static void wash_stop_all_outputs(void)
{
    (void)gantry_stop();
    (void)brush_stop_all();
    (void)water_all_off();
}

static const char *wash_program_path_for_mode(wash_mode_t mode)
{
    if (mode == WASH_MODE_QUICK)
    {
        LOG_WARN("wash_orchestrator: quick wash program not ready, using normal");
        return WASH_PROGRAM_QUICK_PATH;
    }

    return WASH_PROGRAM_NORMAL_PATH;
}

/**
 * @brief  worker 向等待中的 start() 回传启动结果
 */
static void signal_startup(uint32_t gen, sw_err_t result)
{
    bool should_post = false;

    pthread_mutex_lock(&s_startup_mutex);
    if (s_active_startup_gen == gen)
    {
        s_startup_result = result;
        should_post = true;
    }
    pthread_mutex_unlock(&s_startup_mutex);

    if (should_post)
    {
        (void)sem_post(&s_startup_done);
    }
}

/**
 * @brief  失效指定代际的启动等待（RPC 超时后 worker 不再回传）
 */
static void invalidate_startup_gen(uint32_t gen)
{
    pthread_mutex_lock(&s_startup_mutex);
    if (s_active_startup_gen == gen)
    {
        s_active_startup_gen = 0U;
    }
    pthread_mutex_unlock(&s_startup_mutex);
}

/**
 * @brief  worker 启动阶段：manifest 校验、加载方案、engine_start（单次 I/O）
 */
static sw_err_t worker_prepare_engine(wash_mode_t mode, engine_t **out_engine)
{
    const char *prog_path = wash_program_path_for_mode(mode);
    engine_program_t *prog = NULL;
    engine_t *e = NULL;

    if (out_engine == NULL)
    {
        return SW_ERR_PARAM;
    }

    *out_engine = NULL;

#if PROGRAM_INTEGRITY_CHECK
    {
        char manifest_path[256] = {0};

        if (!engine_program_manifest_path_from_json(prog_path, manifest_path,
                                                    (unsigned)sizeof(manifest_path)))
        {
            LOG_ERROR("wash_worker: manifest path derive failed path=[%s]", prog_path);
            return SW_ERR_PARAM;
        }

        char manifest_err[256] = {0};
        if (engine_program_manifest_verify(prog_path, manifest_path,
                                           manifest_err,
                                           (unsigned)sizeof(manifest_err)) != SW_OK)
        {
            LOG_ERROR("wash_worker: manifest verify failed json=[%s] err=[%s]",
                      prog_path, manifest_err);
            return SW_ERR_CRC;
        }
    }
#endif

    {
        char err_buf[256] = {0};

        prog = engine_program_load(prog_path, err_buf, (unsigned)sizeof(err_buf));
        if (prog == NULL)
        {
            LOG_ERROR("wash_worker: load program failed path=[%s] err=[%s]",
                      prog_path, err_buf);
            return SW_ERR_PARAM;
        }
    }

    {
        engine_program_t *snapshot = engine_program_clone(prog);

        engine_program_free(prog);
        prog = snapshot;
        if (prog == NULL)
        {
            LOG_ERROR("wash_worker: program snapshot clone OOM");
            return SW_ERR_NOMEM;
        }
    }

    e = engine_create();
    if (e == NULL)
    {
        LOG_ERROR("wash_worker: engine_create OOM");
        engine_program_free(prog);
        return SW_ERR_NOMEM;
    }

    if (engine_load_program(e, prog) != SW_OK)
    {
        LOG_ERROR("wash_worker: engine_load_program failed");
        engine_destroy(e);
        return SW_ERR_PARAM;
    }

    dev_ctx_set_wash_mode(mode);
    LOG_INFO("wash_worker: prepare mode=%d path=%s", (int)mode, prog_path);

    if (engine_start(e) != SW_OK)
    {
        LOG_ERROR("wash_worker: engine_start failed");
        engine_destroy(e);
        return SW_ERR_STATE;
    }

    *out_engine = e;
    return SW_OK;
}

static void *wash_worker_fn(void *arg)
{
    (void)arg;

    while (true)
    {
        uint32_t startup_gen;
        engine_t *e = NULL;
        sw_err_t prep_ret;

        sem_wait(&s_start_sem);

        if (atomic_load(&s_terminate))
        {
            break;
        }

        pthread_mutex_lock(&s_startup_mutex);
        startup_gen = s_active_startup_gen;
        pthread_mutex_unlock(&s_startup_mutex);

        prep_ret = worker_prepare_engine(s_mode, &e);
        if (prep_ret != SW_OK)
        {
            signal_startup(startup_gen, prep_ret);
            atomic_store(&s_busy, false);
            continue;
        }

        {
            bool waiter_active;

            pthread_mutex_lock(&s_startup_mutex);
            waiter_active = (s_active_startup_gen == startup_gen);
            pthread_mutex_unlock(&s_startup_mutex);

            if (!waiter_active)
            {
                LOG_WARN("wash_worker: startup waiter gone, teardown gen=%u", startup_gen);
                engine_destroy(e);
                atomic_store(&s_busy, false);
                continue;
            }
        }

        (void)event_publish(EVT_WASH_SESSION_STARTED, 0U);
        signal_startup(startup_gen, SW_OK);

        {
            uint32_t total_ms      = 0U;
            uint32_t recover_count = 0U;

            while (true)
            {
                if (atomic_load(&s_abort_cause) >= 0)
                {
                    LOG_WARN("wash_worker: abort cause=%d at phase [%s]",
                             atomic_load(&s_abort_cause),
                             engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                    break;
                }

                total_ms += STEP_POLL_INTERVAL_MS;
                if (total_ms >= WASH_TOTAL_TIMEOUT_MS)
                {
                    LOG_ERROR("wash_worker: total timeout %u ms at phase [%s]",
                              (unsigned)WASH_TOTAL_TIMEOUT_MS,
                              engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                    break;
                }

                engine_tick(e, STEP_POLL_INTERVAL_MS);
                atomic_store(&s_current_direction, (int)engine_current_direction(e));
                dev_ctx_set_gantry_pos((int32_t)gantry_position());

                engine_run_state_t st = engine_state(e);

                if ((st == ENGINE_STATE_DONE) || (st == ENGINE_STATE_HALTED))
                {
                    break;
                }

                if (st == ENGINE_STATE_PHASE_HALTED)
                {
                    if (recover_count >= MAX_PHASE_RECOVERIES)
                    {
                        LOG_ERROR("wash_worker: phase recovery limit (%u) at [%s]",
                                  (unsigned)MAX_PHASE_RECOVERIES,
                                  engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                        break;
                    }
                    ++recover_count;
                    LOG_WARN("wash_worker: phase halted, recover %u/%u at [%s]",
                             (unsigned)recover_count, (unsigned)MAX_PHASE_RECOVERIES,
                             engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                    (void)engine_recover(e);
                }
                else
                {
                    recover_count = 0U;
                }

                usleep((unsigned long)STEP_POLL_INTERVAL_MS * 1000UL);
            }

            atomic_store(&s_current_direction, (int)ENGINE_DIR_NONE);

            {
                engine_run_state_t final_state = engine_state(e);
                int stored_abort = atomic_load(&s_abort_cause);
                bool timed_out = (total_ms >= WASH_TOTAL_TIMEOUT_MS);

                engine_destroy(e);
                wash_stop_all_outputs();
                dev_ctx_set_wash_mode(s_mode);

                if ((final_state == ENGINE_STATE_DONE) && (stored_abort < 0) && !timed_out)
                {
                    (void)event_publish(EVT_WASH_DONE, 0U);
                    LOG_INFO("wash_worker: wash done");
                }
                else
                {
                    wash_abort_cause_t cause;

                    if (stored_abort >= 0)
                    {
                        cause = (wash_abort_cause_t)stored_abort;
                    }
                    else if (timed_out)
                    {
                        cause = WASH_ABORT_STEP_TIMEOUT;
                    }
                    else
                    {
                        cause = WASH_ABORT_INTERNAL;
                    }

                    (void)event_publish(EVT_WASH_ABORTED, wash_abort_evt_param(cause));
                    LOG_WARN("wash_worker: wash aborted cause=%d engine_state=%d",
                             (int)cause, (int)final_state);
                }
            }
        }

        atomic_store(&s_busy, false);
    }

    LOG_INFO("wash_worker: thread exit");
    return NULL;
}

sw_err_t wash_orchestrator_init(void)
{
    atomic_store(&s_busy,              false);
    atomic_store(&s_terminate,         false);
    atomic_store(&s_abort_cause,       WASH_ABORT_NONE);
    atomic_store(&s_current_direction, (int)ENGINE_DIR_NONE);

    if (sem_init(&s_start_sem, 0, 0) != 0)
    {
        LOG_ERROR("wash_orchestrator_init: sem_init start_sem failed");
        return SW_ERR_HW;
    }

    if (sem_init(&s_startup_done, 0, 0) != 0)
    {
        LOG_ERROR("wash_orchestrator_init: sem_init startup_done failed");
        return SW_ERR_HW;
    }

    sw_err_t ret = thread_register("wash_worker", wash_worker_fn,
                                   SCHED_OTHER, 0, THD_WASH_WORKER_STACK);
    if (ret != SW_OK)
    {
        return ret;
    }

    LOG_INFO("wash_orchestrator: init ok");
    return SW_OK;
}

sw_err_t wash_orchestrator_start(wash_mode_t mode)
{
    uint32_t my_gen;
    struct timespec ts;
    int sem_ret;
    sw_err_t ret;

    if (atomic_load(&s_busy))
    {
        LOG_WARN("wash_orchestrator_start: busy");
        return SW_ERR_BUSY;
    }

    pthread_mutex_lock(&s_startup_mutex);
    my_gen = ++s_startup_gen;
    s_active_startup_gen = my_gen;
    s_startup_result = SW_ERR_BUSY;
    pthread_mutex_unlock(&s_startup_mutex);

    atomic_store(&s_abort_cause, WASH_ABORT_NONE);
    s_mode = mode;
    atomic_store(&s_busy, true);
    sem_post(&s_start_sem);

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
        invalidate_startup_gen(my_gen);
        atomic_store(&s_busy, false);
        return SW_ERR_HW;
    }

    ts.tv_sec += (time_t)(WASH_STARTUP_WAIT_MS / 1000U);
    ts.tv_nsec += (long)(WASH_STARTUP_WAIT_MS % 1000U) * 1000000L;
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    sem_ret = sem_timedwait(&s_startup_done, &ts);
    if (sem_ret != 0)
    {
        invalidate_startup_gen(my_gen);
        LOG_ERROR("wash_orchestrator_start: startup wait timeout");
        return SW_ERR_TIMEOUT;
    }

    pthread_mutex_lock(&s_startup_mutex);
    if (s_active_startup_gen != my_gen)
    {
        pthread_mutex_unlock(&s_startup_mutex);
        return SW_ERR_TIMEOUT;
    }

    ret = s_startup_result;
    pthread_mutex_unlock(&s_startup_mutex);

    if (ret != SW_OK)
    {
        LOG_WARN("wash_orchestrator_start: worker startup failed ret=%d", (int)ret);
    }

    return ret;
}

void wash_orchestrator_abort(wash_abort_cause_t cause)
{
    atomic_store(&s_abort_cause, (int)cause);
    wash_stop_all_outputs();
    LOG_WARN("wash_orchestrator: abort cause=%d", (int)cause);
}

bool wash_orchestrator_is_busy(void)
{
    return (bool)atomic_load(&s_busy);
}

engine_direction_t wash_orchestrator_current_direction(void)
{
    return (engine_direction_t)atomic_load(&s_current_direction);
}
