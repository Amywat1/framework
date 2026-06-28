/**
 * @file    wash_orchestrator.c
 * @brief   洗车流程编排器（engine tick 驱动）
 * @author  HUWANGWEI
 * @date    2026-06-27
 *
 * @note    worker_thread 每 STEP_POLL_INTERVAL_MS 毫秒调用一次 engine_tick()，
 *          引擎从方案 JSON 文件加载后驱动全流程。
 *          洗车方案 JSON 路径由编译期宏 WASH_PROGRAM_NORMAL_PATH /
 *          WASH_PROGRAM_QUICK_PATH 注入（CMakeLists 定义）；
 *          未定义时使用下方默认值（真机部署路径）。
 */

#include "application/orchestrators/wash_orchestrator.h"
#include "infrastructure/scheduler/thread_registry.h"
#include "infrastructure/services/dev_ctx/dev_ctx.h"
#include "domain/device/unit/brush.h"
#include "domain/device/unit/gantry.h"
#include "domain/device/water.h"
#include "domain/engine/engine.h"
#include "ports/storage/engine_program_loader_port.h"
#include "infrastructure/event_bus/event_bus.h"
#include "config/threading/thread_config.h"
#include "common/event_types.h"
#include "common/log.h"

#include <semaphore.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sched.h>

/* 洗车方案 JSON 路径（CMakeLists 通过 compile_definitions 覆盖） */
#ifndef WASH_PROGRAM_NORMAL_PATH
#define WASH_PROGRAM_NORMAL_PATH "/etc/m8/m8_normal_wash_program.json"
#endif
#ifndef WASH_PROGRAM_QUICK_PATH
/* TODO: 快洗方案待补全，暂借用标准洗 */
#define WASH_PROGRAM_QUICK_PATH  WASH_PROGRAM_NORMAL_PATH
#endif

#define STEP_POLL_INTERVAL_MS    50U
/* 单阶段最多自动恢复次数（auto_reset 联锁反复触发时触发硬中止） */
#define MAX_PHASE_RECOVERIES     5U
/* 整体洗车超时保护：所有阶段 JSON 配置超时之和的上界（防 timeout_ms=0 死循环） */
#define WASH_TOTAL_TIMEOUT_MS    600000U

static sem_t        s_start_sem;
static atomic_bool  s_busy      = false;
static atomic_bool  s_terminate = false;
static atomic_bool  s_abort_req = false;
static wash_mode_t  s_mode      = WASH_MODE_STANDARD;
/* 当前阶段方向：在 tick 循环内由 worker 写入，外部原子读（测试辅助，无指针跨线程） */
static atomic_int   s_current_direction;

/* -------------------------------------------------------------------------
 * 安全停机（中止或流程结束后调用）
 * ------------------------------------------------------------------------- */
static void wash_stop_all_outputs(void)
{
    (void)gantry_stop();
    (void)brush_off();
    (void)water_all_off();
}

/* -------------------------------------------------------------------------
 * worker_thread
 * ------------------------------------------------------------------------- */
static void *wash_worker_fn(void *arg)
{
    (void)arg;

    while (true)
    {
        sem_wait(&s_start_sem);

        if (atomic_load(&s_terminate))
        {
            break;
        }

        /* 选取方案路径 */
        const char *prog_path = (s_mode == WASH_MODE_QUICK)
            ? WASH_PROGRAM_QUICK_PATH
            : WASH_PROGRAM_NORMAL_PATH;

        if (s_mode == WASH_MODE_QUICK)
        {
            LOG_WARN("wash_worker: quick wash program not ready, using normal");
        }

        /* 加载方案 */
        char err_buf[256] = {0};
        engine_program_t *prog =
            engine_program_load(prog_path, err_buf, (unsigned)sizeof(err_buf));
        if (prog == NULL)
        {
            LOG_ERROR("wash_worker: load program failed path=[%s] err=[%s]",
                      prog_path, err_buf);
            (void)event_publish(EVT_WASH_ABORTED, (uint32_t)SW_ERR_PARAM);
            atomic_store(&s_busy, false);
            continue;
        }

        /* 创建引擎；engine_load_program 调用前所有权仍在 prog，失败须手动释放 */
        engine_t *e = engine_create();
        if (e == NULL)
        {
            LOG_ERROR("wash_worker: engine_create OOM");
            engine_program_free(prog);
            (void)event_publish(EVT_WASH_ABORTED, (uint32_t)SW_ERR_NOMEM);
            atomic_store(&s_busy, false);
            continue;
        }

        if (engine_load_program(e, prog) != SW_OK)
        {
            LOG_ERROR("wash_worker: engine_load_program failed");
            engine_destroy(e);  /* prog 所有权已转移，由 destroy 释放 */
            (void)event_publish(EVT_WASH_ABORTED, (uint32_t)SW_ERR_PARAM);
            atomic_store(&s_busy, false);
            continue;
        }

        dev_ctx_set_wash_mode(s_mode);

        LOG_INFO("wash_worker: start mode=%d path=%s", (int)s_mode, prog_path);

        if (engine_start(e) != SW_OK)
        {
            LOG_ERROR("wash_worker: engine_start failed");
            engine_destroy(e);
            (void)event_publish(EVT_WASH_ABORTED, (uint32_t)SW_ERR_STATE);
            atomic_store(&s_busy, false);
            continue;
        }

        /* tick 驱动循环 */
        uint32_t total_ms      = 0U;
        uint32_t recover_count = 0U;

        while (true)
        {
            if (atomic_load(&s_abort_req))
            {
                LOG_WARN("wash_worker: abort at phase [%s]",
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

            /* 将当前方向发布给外部读取者（无指针跨线程，原子安全） */
            atomic_store(&s_current_direction, (int)engine_current_direction(e));
            dev_ctx_set_gantry_pos(gantry_get_pos());

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
                recover_count = 0U;  /* 正常推进时重置，避免跨阶段累计 */
            }

            usleep((unsigned long)STEP_POLL_INTERVAL_MS * 1000UL);
        }

        /* 清空方向：防止外部在洗车结束后读到残留值 */
        atomic_store(&s_current_direction, (int)ENGINE_DIR_NONE);

        engine_run_state_t final_state = engine_state(e);
        engine_destroy(e);

        wash_stop_all_outputs();
        dev_ctx_set_wash_mode(s_mode);

        bool aborted = atomic_load(&s_abort_req);
        if ((final_state == ENGINE_STATE_DONE) && !aborted && (total_ms < WASH_TOTAL_TIMEOUT_MS))
        {
            (void)event_publish(EVT_WASH_DONE, 0U);
            LOG_INFO("wash_worker: wash done");
        }
        else
        {
            (void)event_publish(EVT_WASH_ABORTED, (uint32_t)SW_ERR_STATE);
            LOG_WARN("wash_worker: wash aborted engine_state=%d", (int)final_state);
        }

        atomic_store(&s_busy, false);
    }

    LOG_INFO("wash_worker: thread exit");
    return NULL;
}

/* -------------------------------------------------------------------------
 * 对外接口
 * ------------------------------------------------------------------------- */
sw_err_t wash_orchestrator_init(void)
{
    atomic_store(&s_busy,              false);
    atomic_store(&s_terminate,         false);
    atomic_store(&s_abort_req,         false);
    atomic_store(&s_current_direction, (int)ENGINE_DIR_NONE);

    if (sem_init(&s_start_sem, 0, 0) != 0)
    {
        LOG_ERROR("wash_orchestrator_init: sem_init failed");
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
    if (atomic_load(&s_busy))
    {
        LOG_WARN("wash_orchestrator_start: busy");
        return SW_ERR_BUSY;
    }
    /* abort_req 在此清零：start() 之后到达的 abort 才有效，之前残留的无效 */
    atomic_store(&s_abort_req, false);
    s_mode = mode;
    atomic_store(&s_busy, true);
    sem_post(&s_start_sem);
    return SW_OK;
}

void wash_orchestrator_abort(void)
{
    atomic_store(&s_abort_req, true);
    wash_stop_all_outputs();
    LOG_WARN("wash_orchestrator: abort");
}

bool wash_orchestrator_is_busy(void)
{
    return (bool)atomic_load(&s_busy);
}

engine_direction_t wash_orchestrator_current_direction(void)
{
    return (engine_direction_t)atomic_load(&s_current_direction);
}
