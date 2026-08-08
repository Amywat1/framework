/**
 * @file    engine_session.c
 * @brief   通用方案引擎会话实现
 * @author  HUWANGWEI
 * @date    2026-07-17
 */

#include "application/engine_session/engine_session.h"

#include "common/log.h"
#include "common/time_util.h"
#include "common/trace_context.h"
#include "domain/program_engine/model/engine_model.h"
#include "domain/ports/outbound/storage/engine_program_loader_port.h"
#include "runtime/scheduler/thread_registry.h"

#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

struct engine_session {
    engine_session_config_t cfg;
    engine_session_run_t    run; /* 当前/待执行运行参数 */
    trace_context_t         run_trace;
    char                    program_path[256];

    sem_t           start_sem;
    sem_t           startup_done;
    pthread_mutex_t startup_mutex;

    atomic_bool s_busy;
    atomic_bool s_terminate;
    atomic_bool s_abort_requested;
    atomic_int  s_current_direction;

    uint32_t s_startup_gen;
    uint32_t s_active_startup_gen;
    sw_err_t s_startup_result;

    bool inited;
};

typedef struct engine_session engine_session_t;

size_t engine_session_size(void)
{
    return sizeof(struct engine_session);
}

static engine_session_t *as_session(void *storage)
{
    return (engine_session_t *)storage;
}

static const engine_session_t *as_session_c(const void *storage)
{
    return (const engine_session_t *)storage;
}

static void signal_startup(engine_session_t *s, uint32_t gen, sw_err_t result)
{
    bool should_post = false;

    pthread_mutex_lock(&s->startup_mutex);
    if (s->s_active_startup_gen == gen) {
        s->s_startup_result = result;
        should_post         = true;
    }
    pthread_mutex_unlock(&s->startup_mutex);

    if (should_post) {
        (void)sem_post(&s->startup_done);
    }
}

static void invalidate_startup_gen(engine_session_t *s, uint32_t gen)
{
    pthread_mutex_lock(&s->startup_mutex);
    if (s->s_active_startup_gen == gen) {
        s->s_active_startup_gen = 0U;
    }
    pthread_mutex_unlock(&s->startup_mutex);
}

static void call_stop_outputs(engine_session_t *s)
{
    if (s->run.on_stop_outputs != NULL) {
        s->run.on_stop_outputs(s->run.user);
    }
}

static void notify_phase_changed(engine_session_t *s, const engine_t *e, char *last_phase_id, size_t last_phase_id_size)
{
    const char *phase_id;

    if ((s->run.on_phase_changed == NULL) || (last_phase_id == NULL) || (last_phase_id_size == 0U)) {
        return;
    }
    phase_id = engine_current_phase_id(e);
    if (phase_id == NULL) {
        phase_id = "";
    }
    if (strncmp(last_phase_id, phase_id, last_phase_id_size) == 0) {
        return;
    }
    (void)strncpy(last_phase_id, phase_id, last_phase_id_size - 1U);
    last_phase_id[last_phase_id_size - 1U] = '\0';
    s->run.on_phase_changed(s->run.user, last_phase_id, engine_current_direction(e));
}

static sw_err_t worker_prepare_engine(engine_session_t *s, engine_t **out_engine)
{
    const char       *prog_path = s->program_path;
    engine_program_t *prog      = NULL;
    engine_t         *e         = NULL;

    if (out_engine == NULL) {
        return SW_ERR_PARAM;
    }

    *out_engine = NULL;

    if (prog_path[0] == '\0') {
        LOG_ERROR("engine_session: empty program path");
        return SW_ERR_PARAM;
    }

    if (s->cfg.integrity_check) {
        char manifest_err[256] = {0};

        if (engine_program_verify_integrity(prog_path, manifest_err, (unsigned)sizeof(manifest_err)) != SW_OK) {
            LOG_ERROR("engine_session: integrity verify failed path=[%s] err=[%s]", prog_path, manifest_err);
            return SW_ERR_CRC;
        }
    }

    {
        char err_buf[256] = {0};

        prog = engine_program_load(prog_path, err_buf, (unsigned)sizeof(err_buf));
        if (prog == NULL) {
            LOG_ERROR("engine_session: load program failed path=[%s] err=[%s]", prog_path, err_buf);
            return SW_ERR_PARAM;
        }
    }

    {
        engine_program_t *snapshot = engine_program_clone(prog);

        engine_program_free(prog);
        prog = snapshot;
        if (prog == NULL) {
            LOG_ERROR("engine_session: program snapshot clone OOM");
            return SW_ERR_NOMEM;
        }
    }

    e = engine_create();
    if (e == NULL) {
        LOG_ERROR("engine_session: engine_create OOM");
        engine_program_free(prog);
        return SW_ERR_NOMEM;
    }

    if (engine_load_program(e, prog) != SW_OK) {
        LOG_ERROR("engine_session: engine_load_program failed");
        engine_destroy(e);
        return SW_ERR_PARAM;
    }

    if (engine_start(e) != SW_OK) {
        LOG_ERROR("engine_session: engine_start failed");
        engine_destroy(e);
        return SW_ERR_STATE;
    }

    LOG_INFO("engine_session: prepare path=%s", prog_path);
    *out_engine = e;
    return SW_OK;
}

static void *engine_session_worker_fn(void *arg)
{
    engine_session_t *s = (engine_session_t *)arg;

    while (true) {
        uint32_t        startup_gen;
        engine_t       *e = NULL;
        sw_err_t        prep_ret;
        trace_context_t previous_trace;
        char            last_phase_id[64] = {0};

        sem_wait(&s->start_sem);

        if (atomic_load(&s->s_terminate)) {
            break;
        }

        pthread_mutex_lock(&s->startup_mutex);
        startup_gen = s->s_active_startup_gen;
        pthread_mutex_unlock(&s->startup_mutex);

        previous_trace = trace_context_get();
        trace_context_set(&s->run_trace);

        prep_ret = worker_prepare_engine(s, &e);
        if (prep_ret != SW_OK) {
            signal_startup(s, startup_gen, prep_ret);
            atomic_store(&s->s_busy, false);
            trace_context_set(&previous_trace);
            continue;
        }

        {
            bool waiter_active;

            pthread_mutex_lock(&s->startup_mutex);
            waiter_active = (s->s_active_startup_gen == startup_gen);
            pthread_mutex_unlock(&s->startup_mutex);

            if (!waiter_active) {
                LOG_WARN("engine_session: startup waiter gone, teardown gen=%u", startup_gen);
                engine_destroy(e);
                atomic_store(&s->s_busy, false);
                trace_context_set(&previous_trace);
                continue;
            }
        }

        if (s->run.on_started != NULL) {
            s->run.on_started(s->run.user);
        }
        notify_phase_changed(s, e, last_phase_id, sizeof(last_phase_id));
        signal_startup(s, startup_gen, SW_OK);

        {
            uint32_t                total_ms      = 0U;
            uint32_t                recover_count = 0U;
            bool                    timed_out     = false;
            bool                    aborted       = false;
            engine_session_result_t result;

            while (true) {
                if (atomic_load(&s->s_abort_requested)) {
                    aborted = true;
                    LOG_WARN("engine_session: abort at phase [%s]",
                             engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                    break;
                }

                total_ms += s->cfg.tick_ms;
                if ((s->run.total_timeout_ms > 0U) && (total_ms >= s->run.total_timeout_ms)) {
                    timed_out = true;
                    LOG_ERROR("engine_session: total timeout %u ms at phase [%s]",
                              (unsigned)s->run.total_timeout_ms,
                              engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                    break;
                }

                engine_tick(e, s->cfg.tick_ms);
                atomic_store(&s->s_current_direction, (int)engine_current_direction(e));

                {
                    engine_run_state_t st = engine_state(e);

                    notify_phase_changed(s, e, last_phase_id, sizeof(last_phase_id));
                    if ((st == ENGINE_STATE_DONE) || (st == ENGINE_STATE_HALTED)) {
                        break;
                    }

                    if (st == ENGINE_STATE_PHASE_HALTED) {
                        if (s->run.max_phase_recoveries == 0U) {
                            LOG_ERROR("engine_session: phase halted at [%s]",
                                      engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                            break;
                        }
                        if (recover_count >= s->run.max_phase_recoveries) {
                            LOG_ERROR("engine_session: phase recovery limit (%u) at [%s]",
                                      (unsigned)s->run.max_phase_recoveries,
                                      engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                            break;
                        }
                        ++recover_count;
                        LOG_WARN("engine_session: phase halted, recover %u/%u at [%s]",
                                 (unsigned)recover_count,
                                 (unsigned)s->run.max_phase_recoveries,
                                 engine_current_phase_id(e) ? engine_current_phase_id(e) : "?");
                        (void)engine_recover(e);
                    } else {
                        recover_count = 0U;
                    }
                }

                usleep((unsigned long)s->cfg.tick_ms * 1000UL);
            }

            atomic_store(&s->s_current_direction, (int)ENGINE_DIR_NONE);

            result.final_state = engine_state(e);
            result.timed_out   = timed_out;
            result.aborted     = aborted;
            result.success     = (result.final_state == ENGINE_STATE_DONE) && !timed_out && !aborted;

            engine_destroy(e);
            call_stop_outputs(s);

            if (s->run.on_finished != NULL) {
                s->run.on_finished(s->run.user, &result);
            }
        }

        atomic_store(&s->s_busy, false);
        trace_context_set(&previous_trace);
    }

    LOG_INFO("engine_session: thread exit name=%s", s->cfg.thread_name != NULL ? s->cfg.thread_name : "?");
    return NULL;
}

sw_err_t engine_session_init(void *storage, const engine_session_config_t *cfg)
{
    engine_session_t *s = as_session(storage);

    if ((storage == NULL) || (cfg == NULL) || (cfg->thread_name == NULL) || (cfg->stack_size == 0U)
        || (cfg->tick_ms == 0U) || (cfg->startup_wait_ms == 0U)) {
        return SW_ERR_PARAM;
    }

    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    pthread_mutex_init(&s->startup_mutex, NULL);

    atomic_store(&s->s_busy, false);
    atomic_store(&s->s_terminate, false);
    atomic_store(&s->s_abort_requested, false);
    atomic_store(&s->s_current_direction, (int)ENGINE_DIR_NONE);

    if (sem_init(&s->start_sem, 0, 0) != 0) {
        LOG_ERROR("engine_session_init: sem_init start_sem failed");
        return SW_ERR_HW;
    }

    if (sem_init(&s->startup_done, 0, 0) != 0) {
        LOG_ERROR("engine_session_init: sem_init startup_done failed");
        return SW_ERR_HW;
    }

    {
        sw_err_t ret
            = thread_register_arg(cfg->thread_name, engine_session_worker_fn, s, SCHED_OTHER, 0, cfg->stack_size);

        if (ret != SW_OK) {
            return ret;
        }
    }

    s->inited = true;
    LOG_INFO("engine_session: init ok name=%s", cfg->thread_name);
    return SW_OK;
}

sw_err_t engine_session_start(void *storage, const engine_session_run_t *run)
{
    engine_session_t *s = as_session(storage);
    uint32_t          my_gen;
    struct timespec   ts;
    int               sem_ret;
    sw_err_t          ret;

    if ((s == NULL) || !s->inited || (run == NULL) || (run->program_path == NULL) || (run->program_path[0] == '\0')) {
        return SW_ERR_PARAM;
    }

    if (atomic_load(&s->s_busy)) {
        LOG_WARN("engine_session_start: busy name=%s", s->cfg.thread_name);
        return SW_ERR_BUSY;
    }

    if (strlen(run->program_path) >= sizeof(s->program_path)) {
        LOG_ERROR("engine_session_start: path too long");
        return SW_ERR_PARAM;
    }

    s->run = *run;
    (void)strncpy(s->program_path, run->program_path, sizeof(s->program_path) - 1U);
    s->program_path[sizeof(s->program_path) - 1U] = '\0';
    s->run.program_path                           = s->program_path;

    s->run_trace = trace_context_get();
    pthread_mutex_lock(&s->startup_mutex);
    my_gen                  = ++s->s_startup_gen;
    s->s_active_startup_gen = my_gen;
    s->s_startup_result     = SW_ERR_BUSY;
    pthread_mutex_unlock(&s->startup_mutex);

    atomic_store(&s->s_abort_requested, false);
    atomic_store(&s->s_busy, true);
    (void)sem_post(&s->start_sem);

    time_util_fill_deadline(s->cfg.startup_wait_ms, &ts);
    sem_ret = sem_timedwait(&s->startup_done, &ts);
    if (sem_ret != 0) {
        invalidate_startup_gen(s, my_gen);
        LOG_ERROR("engine_session_start: startup wait timeout name=%s", s->cfg.thread_name);
        return SW_ERR_TIMEOUT;
    }

    pthread_mutex_lock(&s->startup_mutex);
    if (s->s_active_startup_gen != my_gen) {
        pthread_mutex_unlock(&s->startup_mutex);
        return SW_ERR_TIMEOUT;
    }

    ret = s->s_startup_result;
    pthread_mutex_unlock(&s->startup_mutex);

    if (ret != SW_OK) {
        LOG_WARN("engine_session_start: worker startup failed ret=%d name=%s", (int)ret, s->cfg.thread_name);
    }

    return ret;
}

bool engine_session_abort(void *storage)
{
    engine_session_t *s = as_session(storage);

    if ((s == NULL) || !s->inited || !atomic_load(&s->s_busy)) {
        return false;
    }
    if (atomic_exchange(&s->s_abort_requested, true)) {
        return false;
    }

    call_stop_outputs(s);
    LOG_WARN("engine_session: abort requested name=%s", s->cfg.thread_name);
    return true;
}

bool engine_session_is_busy(const void *storage)
{
    const engine_session_t *s = as_session_c(storage);

    if ((s == NULL) || !s->inited) {
        return false;
    }
    return (bool)atomic_load(&s->s_busy);
}

engine_direction_t engine_session_direction(const void *storage)
{
    const engine_session_t *s = as_session_c(storage);

    if ((s == NULL) || !s->inited) {
        return ENGINE_DIR_NONE;
    }
    return (engine_direction_t)atomic_load(&s->s_current_direction);
}
