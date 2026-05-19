#define _POSIX_C_SOURCE 200809L

#include "application/coordinators/controller_runtime.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "adapters/logging/file_event_logger.h"
#include "adapters/outbound/file_program_repository.h"
#include "application/services/alarm_evaluator.h"
#include "platform/worker_thread.h"
#include "platform/linux/controller_scheduler_linux.h"
#include "src/application/jobs/alarm_detect_job.h"
#include "src/application/jobs/io_read_job.h"
#include "src/application/coordinators/controller_runtime_private.h"
#include "src/application/coordinators/system_context_private.h"

/*
 * Controller Runtime 句柄编码方案（opaque token）：
 * 目的：防止外部直接解引用或伪造句柄，并允许识别已释放或过期句柄。
 *
 * 编码布局（低位到高位）：
 *   [8位 magic] [16位 checksum] [N位 generation]
 * - magic: 固定标记 0x52u（ASCII 'R'），用于识别有效 token。
 * - checksum: 由 generation 与 secret 混合得到的校验码。
 * - generation: 单调递增的代次号，用于检测 use-after-free。
 *
 * 流程：
 * 1. create：分配 generation，再编码为 opaque token 返回给调用方。
 * 2. use：调用方保存 token，并在后续接口中传回。
 * 3. decode：校验 magic、checksum，再提取 generation 并核对当前代次。
 * 4. destroy：递增 generation，使旧 token 立即失效。
 */

#define CONTROLLER_RUNTIME_HANDLE_BITS ((unsigned int)(sizeof(uintptr_t) * 8u))
#define CONTROLLER_RUNTIME_HANDLE_MAGIC_BITS 8u
#define CONTROLLER_RUNTIME_HANDLE_MAGIC ((uintptr_t)0x52u)  /* ASCII 'R' marker */
#define CONTROLLER_RUNTIME_HANDLE_MAGIC_MASK ((((uintptr_t)1u) << CONTROLLER_RUNTIME_HANDLE_MAGIC_BITS) - 1u)
#define CONTROLLER_RUNTIME_HANDLE_CHECKSUM_BITS 16u
#define CONTROLLER_RUNTIME_HANDLE_CHECKSUM_MASK ((((uintptr_t)1u) << CONTROLLER_RUNTIME_HANDLE_CHECKSUM_BITS) - 1u)
#define CONTROLLER_RUNTIME_HANDLE_GENERATION_SHIFT                                                                     \
    (CONTROLLER_RUNTIME_HANDLE_MAGIC_BITS + CONTROLLER_RUNTIME_HANDLE_CHECKSUM_BITS)
#define CONTROLLER_RUNTIME_MAX_GENERATION (UINTPTR_MAX >> CONTROLLER_RUNTIME_HANDLE_GENERATION_SHIFT)

_Static_assert(CONTROLLER_RUNTIME_HANDLE_BITS > CONTROLLER_RUNTIME_HANDLE_GENERATION_SHIFT,
               "controller_runtime handle token must leave room for a positive generation field");

typedef struct background_alarm_snapshot_mailbox_t
{
    atomic_bool access_guard;
    unsigned long published_sequence;
    sensor_snapshot_t sensor_snapshot;
    unsigned long occurred_at_ms;
} background_alarm_snapshot_mailbox_t;

typedef struct controller_runtime_state_t
{
    controller_runtime_lifecycle_state_t lifecycle_state;
    bool occupied;
    bool run_invoked;
    bool system_context_acquired;
    bool scheduler_created;
    error_code_t last_error_code;
    char last_reason_code[64];
    system_context_t system_context;
    controller_scheduler_t *controller_scheduler;
    worker_thread_t *background_alarm_io_worker_thread;
    worker_thread_t *background_alarm_detect_worker_thread;
    struct
    {
        system_context_t system_context;
        const sensor_port_t *sensor_port;
        unsigned long io_sample_period_ms;
        unsigned long detect_period_ms;
        alarm_evaluator_t alarm_evaluator;
        background_alarm_snapshot_mailbox_t snapshot_mailbox;
    } background_alarm_monitor_context;
    controller_runtime_config_t config_snapshot;
    uintptr_t active_generation;
} controller_runtime_state_t;

static controller_runtime_state_t g_controller_runtime_state;
static uintptr_t g_controller_runtime_next_generation = 1u;
static uintptr_t g_controller_runtime_handle_secret = 0u;

static void runtime_stop_background_alarm_monitor(void);

/** @brief 判断字符串指针非空且内容非空。 */
static bool string_present(const char *value) { return value != 0 && value[0] != '\0'; }

/**
 * @brief 校验后台报警监控配置是否满足最小要求。
 * @param config runtime 创建配置。
 * @return 配置合法时返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t runtime_validate_background_alarm_monitor_config(const controller_runtime_config_t *config)
{
    if (config == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!config->background_alarm_monitor.enabled)
    {
        return operation_result_ok();
    }
    if (config->background_alarm_monitor.io_sample_period_ms == 0ul ||
        config->background_alarm_monitor.detect_period_ms == 0ul)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (config->sensor_port == 0 || config->sensor_port->read_snapshot == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    return operation_result_ok();
}

/**
 * @brief 将毫秒时长转换为 `timespec`。
 * @param sleep_ms 休眠毫秒数。
 * @param sleep_time 输出 `timespec`。
 */
static void runtime_build_sleep_time(unsigned long sleep_ms, struct timespec *sleep_time)
{
    if (sleep_time == 0)
    {
        return;
    }

    sleep_time->tv_sec = (time_t)(sleep_ms / 1000ul);
    sleep_time->tv_nsec = (long)((sleep_ms % 1000ul) * 1000000ul);
}

/**
 * @brief 读取当前单调时钟的毫秒值。
 * @return 读取成功时返回毫秒值，失败时返回 `0`。
 */
static unsigned long runtime_read_monotonic_ms(void)
{
    struct timespec current_time;

    if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0)
    {
        return 0ul;
    }

    return (unsigned long)(current_time.tv_sec * 1000ul) + (unsigned long)(current_time.tv_nsec / 1000000ul);
}

/**
 * @brief 按短切片休眠，避免后台线程在停止时长时间阻塞。
 * @param worker_thread 后台线程句柄。
 * @param sleep_ms 总休眠时长。
 */
static void runtime_sleep_with_stop_check(worker_thread_t *worker_thread, unsigned long sleep_ms)
{
    static const unsigned long s_sleep_slice_ms = 20ul;
    struct timespec sleep_time;
    unsigned long remaining_ms;
    unsigned long current_slice_ms;

    remaining_ms = sleep_ms;
    while (remaining_ms > 0ul && !worker_thread_stop_requested(worker_thread))
    {
        current_slice_ms = remaining_ms > s_sleep_slice_ms ? s_sleep_slice_ms : remaining_ms;
        runtime_build_sleep_time(current_slice_ms, &sleep_time);
        (void)nanosleep(&sleep_time, 0);
        remaining_ms -= current_slice_ms;
    }
}

/**
 * @brief 初始化后台报警快照邮箱。
 * @param snapshot_mailbox 单生产者单消费者快照邮箱。
 */
static void runtime_background_alarm_snapshot_mailbox_init(background_alarm_snapshot_mailbox_t *snapshot_mailbox)
{
    if (snapshot_mailbox == 0)
    {
        return;
    }

    memset(&snapshot_mailbox->sensor_snapshot, 0, sizeof(snapshot_mailbox->sensor_snapshot));
    snapshot_mailbox->occurred_at_ms = 0ul;
    snapshot_mailbox->published_sequence = 0ul;
    atomic_init(&snapshot_mailbox->access_guard, false);
}

/**
 * @brief 尝试获取后台报警邮箱访问锁。
 * @param snapshot_mailbox 单生产者单消费者快照邮箱。
 * @return 获取成功返回 `true`，否则返回 `false`。
 */
static bool runtime_background_alarm_snapshot_mailbox_try_lock(background_alarm_snapshot_mailbox_t *snapshot_mailbox)
{
    bool expected_unlocked;

    if (snapshot_mailbox == 0)
    {
        return false;
    }

    expected_unlocked = false;
    return atomic_compare_exchange_strong_explicit(&snapshot_mailbox->access_guard,
                                                   &expected_unlocked,
                                                   true,
                                                   memory_order_acquire,
                                                   memory_order_relaxed);
}

/**
 * @brief 释放后台报警邮箱访问锁。
 * @param snapshot_mailbox 单生产者单消费者快照邮箱。
 */
static void runtime_background_alarm_snapshot_mailbox_unlock(background_alarm_snapshot_mailbox_t *snapshot_mailbox)
{
    if (snapshot_mailbox == 0)
    {
        return;
    }

    atomic_store_explicit(&snapshot_mailbox->access_guard, false, memory_order_release);
}

/**
 * @brief 发布最新传感器快照到邮箱。
 * @param snapshot_mailbox 单生产者单消费者快照邮箱。
 * @param sensor_snapshot 本次读取到的传感器快照。
 * @param occurred_at_ms 本次快照采样时间。
 */
static bool runtime_background_alarm_snapshot_mailbox_publish(background_alarm_snapshot_mailbox_t *snapshot_mailbox,
                                                              const sensor_snapshot_t *sensor_snapshot,
                                                              unsigned long occurred_at_ms)
{
    if (snapshot_mailbox == 0 || sensor_snapshot == 0)
    {
        return false;
    }
    if (!runtime_background_alarm_snapshot_mailbox_try_lock(snapshot_mailbox))
    {
        return false;
    }

    snapshot_mailbox->sensor_snapshot = *sensor_snapshot;
    snapshot_mailbox->occurred_at_ms = occurred_at_ms;
    snapshot_mailbox->published_sequence += 1ul;
    runtime_background_alarm_snapshot_mailbox_unlock(snapshot_mailbox);
    return true;
}

/**
 * @brief 尝试读取一份稳定发布且尚未消费的新快照。
 * @param snapshot_mailbox 单生产者单消费者快照邮箱。
 * @param last_consumed_sequence 调用方维护的最近已消费序号。
 * @param sensor_snapshot 输出快照。
 * @param occurred_at_ms 输出采样时间。
 * @return 读到新快照时返回 `true`，否则返回 `false`。
 */
static bool runtime_background_alarm_snapshot_mailbox_try_read(
    background_alarm_snapshot_mailbox_t *snapshot_mailbox,
    unsigned long *last_consumed_sequence,
    sensor_snapshot_t *sensor_snapshot,
    unsigned long *occurred_at_ms)
{
    unsigned long consumed_sequence;
    unsigned long published_sequence;

    if (snapshot_mailbox == 0 || last_consumed_sequence == 0 || sensor_snapshot == 0 || occurred_at_ms == 0)
    {
        return false;
    }
    if (!runtime_background_alarm_snapshot_mailbox_try_lock(snapshot_mailbox))
    {
        return false;
    }

    consumed_sequence = *last_consumed_sequence;
    published_sequence = snapshot_mailbox->published_sequence;
    if (published_sequence == 0ul || published_sequence == consumed_sequence)
    {
        runtime_background_alarm_snapshot_mailbox_unlock(snapshot_mailbox);
        return false;
    }

    *sensor_snapshot = snapshot_mailbox->sensor_snapshot;
    *occurred_at_ms = snapshot_mailbox->occurred_at_ms;
    *last_consumed_sequence = published_sequence;
    runtime_background_alarm_snapshot_mailbox_unlock(snapshot_mailbox);
    return true;
}

/**
 * @brief 后台报警 IO 线程入口，仅负责周期读取并发布最新快照。
 * @param worker_thread 后台线程句柄。
 * @param context 运行时上下文。
 */
static void runtime_background_alarm_io_entry(worker_thread_t *worker_thread, void *context)
{
    controller_runtime_state_t *runtime_state;
    sensor_snapshot_t sensor_snapshot;
    operation_result_t result;

    runtime_state = (controller_runtime_state_t *)context;
    if (runtime_state == 0)
    {
        return;
    }

    while (!worker_thread_stop_requested(worker_thread))
    {
        result = io_read_job_read_snapshot(runtime_state->background_alarm_monitor_context.sensor_port, &sensor_snapshot);
        if (result.ok &&
            runtime_background_alarm_snapshot_mailbox_publish(
                &runtime_state->background_alarm_monitor_context.snapshot_mailbox,
                &sensor_snapshot,
                runtime_read_monotonic_ms()))
        {
            (void)worker_thread_notify(runtime_state->background_alarm_detect_worker_thread);
        }

        runtime_sleep_with_stop_check(worker_thread,
                                      runtime_state->background_alarm_monitor_context.io_sample_period_ms);
    }
}

/**
 * @brief 后台报警检测线程入口，只消费新快照并执行告警检测。
 * @param worker_thread 后台线程句柄。
 * @param context 运行时上下文。
 */
static void runtime_background_alarm_detect_entry(worker_thread_t *worker_thread, void *context)
{
    unsigned long last_consumed_sequence;
    controller_runtime_state_t *runtime_state;
    sensor_snapshot_t sensor_snapshot;
    unsigned long occurred_at_ms;

    runtime_state = (controller_runtime_state_t *)context;
    if (runtime_state == 0)
    {
        return;
    }

    last_consumed_sequence = 0ul;
    while (!worker_thread_stop_requested(worker_thread))
    {
        if (runtime_background_alarm_snapshot_mailbox_try_read(
                &runtime_state->background_alarm_monitor_context.snapshot_mailbox,
                &last_consumed_sequence,
                &sensor_snapshot,
                &occurred_at_ms))
        {
            (void)alarm_detect_job_process_snapshot(
                runtime_state->background_alarm_monitor_context.system_context,
                &runtime_state->background_alarm_monitor_context.alarm_evaluator,
                &sensor_snapshot,
                occurred_at_ms);
            continue;
        }

        (void)worker_thread_wait(worker_thread,
                                 runtime_state->background_alarm_monitor_context.detect_period_ms);
    }
}

/**
 * @brief 启动可选的后台报警监控线程。
 * @return 启动成功返回 `operation_result_ok()`，否则返回失败结果。
 */
static operation_result_t runtime_start_background_alarm_monitor(void)
{
    operation_result_t result;
    worker_thread_config_t worker_thread_config;

    if (!g_controller_runtime_state.config_snapshot.background_alarm_monitor.enabled)
    {
        return operation_result_ok();
    }
    if (g_controller_runtime_state.background_alarm_io_worker_thread != 0 ||
        g_controller_runtime_state.background_alarm_detect_worker_thread != 0)
    {
        return operation_result_ok();
    }

    memset(&worker_thread_config, 0, sizeof(worker_thread_config));
    g_controller_runtime_state.background_alarm_monitor_context.system_context = g_controller_runtime_state.system_context;
    g_controller_runtime_state.background_alarm_monitor_context.sensor_port =
        g_controller_runtime_state.config_snapshot.sensor_port;
    g_controller_runtime_state.background_alarm_monitor_context.io_sample_period_ms =
        g_controller_runtime_state.config_snapshot.background_alarm_monitor.io_sample_period_ms;
    g_controller_runtime_state.background_alarm_monitor_context.detect_period_ms =
        g_controller_runtime_state.config_snapshot.background_alarm_monitor.detect_period_ms;
    alarm_evaluator_init(&g_controller_runtime_state.background_alarm_monitor_context.alarm_evaluator);
    runtime_background_alarm_snapshot_mailbox_init(&g_controller_runtime_state.background_alarm_monitor_context.snapshot_mailbox);
    worker_thread_config.entry = runtime_background_alarm_io_entry;
    worker_thread_config.context = &g_controller_runtime_state;
    result = worker_thread_start(&g_controller_runtime_state.background_alarm_io_worker_thread, &worker_thread_config);
    if (!result.ok)
    {
        return result;
    }

    worker_thread_config.entry = runtime_background_alarm_detect_entry;
    result = worker_thread_start(&g_controller_runtime_state.background_alarm_detect_worker_thread, &worker_thread_config);
    if (!result.ok)
    {
        runtime_stop_background_alarm_monitor();
        return result;
    }
    return operation_result_ok();
}

/**
 * @brief 停止并销毁后台报警监控线程。
 * @details 先同时发出停止请求，再先等待 IO 线程退出，避免 detect 线程销毁后仍被 IO 线程 notify。
 */
static void runtime_stop_background_alarm_monitor(void)
{
    if (g_controller_runtime_state.background_alarm_detect_worker_thread != 0)
    {
        (void)worker_thread_request_stop(g_controller_runtime_state.background_alarm_detect_worker_thread);
    }
    if (g_controller_runtime_state.background_alarm_io_worker_thread != 0)
    {
        (void)worker_thread_request_stop(g_controller_runtime_state.background_alarm_io_worker_thread);
    }
    if (g_controller_runtime_state.background_alarm_io_worker_thread != 0)
    {
        (void)worker_thread_join(g_controller_runtime_state.background_alarm_io_worker_thread);
    }
    if (g_controller_runtime_state.background_alarm_detect_worker_thread != 0)
    {
        (void)worker_thread_join(g_controller_runtime_state.background_alarm_detect_worker_thread);
        worker_thread_destroy(g_controller_runtime_state.background_alarm_detect_worker_thread);
        g_controller_runtime_state.background_alarm_detect_worker_thread = 0;
    }
    if (g_controller_runtime_state.background_alarm_io_worker_thread != 0)
    {
        worker_thread_destroy(g_controller_runtime_state.background_alarm_io_worker_thread);
        g_controller_runtime_state.background_alarm_io_worker_thread = 0;
    }
}

/**
 * @brief 生成并缓存句柄校验密钥。
 * @details 由全局符号地址异或推导，用于计算 handle checksum。
 * @return 校验密钥，保证非零。
 */
static uintptr_t runtime_handle_secret(void)
{
    uintptr_t secret;

    if (g_controller_runtime_handle_secret != 0u)
    {
        return g_controller_runtime_handle_secret;
    }

    secret = ((uintptr_t)&g_controller_runtime_state >> 4u) ^ ((uintptr_t)&runtime_handle_secret >> 3u) ^
             ((uintptr_t)&controller_runtime_create >> 2u) ^ (uintptr_t)0x5a3cu;
    secret &= CONTROLLER_RUNTIME_HANDLE_CHECKSUM_MASK;
    if (secret == 0u)
    {
        secret = (uintptr_t)0x3du;
    }

    g_controller_runtime_handle_secret = secret;
    return g_controller_runtime_handle_secret;
}

/**
 * @brief 计算 generation 的校验码。
 * @param generation 代次号。
 * @return 16 位校验码。
 */
static uintptr_t runtime_handle_checksum(uintptr_t generation)
{
    uintptr_t checksum;

    checksum = generation ^ runtime_handle_secret();
    checksum ^= (generation << 7u);
    checksum ^= (generation >> 3u);
    return checksum & CONTROLLER_RUNTIME_HANDLE_CHECKSUM_MASK;
}

/**
 * @brief 将 generation 编码为 opaque handle。
 * @param generation 代次号。
 * @return 编码后的 opaque token，不指向真实内存。
 * @note 返回值仅作为 token 使用，调用方需保存并在后续操作中传回。
 */
static controller_runtime_t *runtime_handle_from_generation(uintptr_t generation)
{
    uintptr_t token;

    if (generation == 0u)
    {
        return 0;
    }

    token = (generation << CONTROLLER_RUNTIME_HANDLE_GENERATION_SHIFT) |
            (runtime_handle_checksum(generation) << CONTROLLER_RUNTIME_HANDLE_MAGIC_BITS) |
            CONTROLLER_RUNTIME_HANDLE_MAGIC;
    return (controller_runtime_t *)token;
}

/**
 * @brief 解码 opaque handle 并完成完整校验。
 * @param runtime opaque handle token。
 * @param[out] generation 输出 generation 的指针，可为 `0`。
 * @return 校验通过返回 `true`，否则返回 `false`。
 */
static bool runtime_decode_handle(const controller_runtime_t *runtime, uintptr_t *generation)
{
    uintptr_t checksum;
    uintptr_t decoded_checksum;
    uintptr_t token;
    uintptr_t decoded_generation;

    if (runtime == 0)
    {
        return false;
    }

    token = (uintptr_t)runtime;
    /* 先检查 magic 标记。 */
    if ((token & CONTROLLER_RUNTIME_HANDLE_MAGIC_MASK) != CONTROLLER_RUNTIME_HANDLE_MAGIC)
    {
        return false;
    }

    /* 再提取 checksum 与 generation。 */
    checksum = (token >> CONTROLLER_RUNTIME_HANDLE_MAGIC_BITS) & CONTROLLER_RUNTIME_HANDLE_CHECKSUM_MASK;
    decoded_generation = token >> CONTROLLER_RUNTIME_HANDLE_GENERATION_SHIFT;
    if (decoded_generation == 0u)
    {
        return false;
    }
    /* 最后重算 checksum 做一致性校验。 */
    decoded_checksum = runtime_handle_checksum(decoded_generation);
    if (checksum != decoded_checksum)
    {
        return false;
    }

    if (generation != 0)
    {
        *generation = decoded_generation;
    }
    return true;
}

/**
 * @brief 分配新的 generation 号。
 * @return 单调递增的代次号；超过上限时返回 `0`。
 */
static uintptr_t runtime_issue_generation(void)
{
    uintptr_t generation;

    if (g_controller_runtime_next_generation > CONTROLLER_RUNTIME_MAX_GENERATION)
    {
        return 0u;
    }

    generation = g_controller_runtime_next_generation;
    g_controller_runtime_next_generation += 1u;
    return generation;
}

/**
 * @brief 判断 generation 是否曾被分配过。
 * @param generation 代次号。
 * @return 历史上分配过则返回 `true`，否则返回 `false`。
 */
static bool runtime_generation_was_issued(uintptr_t generation)
{
    return generation > 0u && generation < g_controller_runtime_next_generation;
}

/**
 * @brief 从 system_context 复制最近原因码到 runtime_state。
 * @param runtime_state 目标运行状态结构。
 * @note 复制前会先清空本地缓冲区。
 */
static void runtime_copy_last_reason_from_context(controller_runtime_state_t *runtime_state)
{
    const char *reason_code;

    if (runtime_state == 0)
    {
        return;
    }

    memset(runtime_state->last_reason_code, 0, sizeof(runtime_state->last_reason_code));
    if (runtime_state->system_context == 0)
    {
        return;
    }

    reason_code = system_context_last_reason_code(runtime_state->system_context);
    if (reason_code != 0)
    {
        strncpy(runtime_state->last_reason_code, reason_code, sizeof(runtime_state->last_reason_code) - 1u);
    }
}

/**
 * @brief 从全局运行状态构建当前 runtime 的状态快照。
 * @param[out] status_view 输出状态视图。
 * @details 包含生命周期、错误码、原因码和调度器视图等信息。
 */
static void runtime_build_status_view(controller_runtime_status_view_t *status_view)
{
    if (status_view == 0)
    {
        return;
    }

    memset(status_view, 0, sizeof(*status_view));
    status_view->lifecycle_state = g_controller_runtime_state.lifecycle_state;
    status_view->system_context_acquired = g_controller_runtime_state.system_context_acquired;
    status_view->scheduler_created = g_controller_runtime_state.scheduler_created;
    status_view->run_invoked = g_controller_runtime_state.run_invoked;
    status_view->last_error_code = g_controller_runtime_state.last_error_code;
    /* 预留 1 字节给 null 终止符，避免缓冲区越界。 */
    strncpy(status_view->last_reason_code, g_controller_runtime_state.last_reason_code,
            sizeof(status_view->last_reason_code) - 1u);

    if (g_controller_runtime_state.controller_scheduler != 0 &&
        controller_scheduler_read_view(g_controller_runtime_state.controller_scheduler, &status_view->scheduler_view)
            .ok)
    {
        status_view->scheduler_view_available = true;
    }
}

/**
 * @brief 为已销毁 runtime 构建状态视图。
 * @param[out] status_view 输出状态视图。
 * @param last_error_code 最后的错误码。
 * @param last_reason_code 最后的原因码。
 * @details 生命周期会标记为 `CONTROLLER_RUNTIME_STATE_DESTROYED`。
 */
static void runtime_build_destroyed_status_view(controller_runtime_status_view_t *status_view,
                                                error_code_t last_error_code, const char *last_reason_code)
{
    if (status_view == 0)
    {
        return;
    }

    memset(status_view, 0, sizeof(*status_view));
    status_view->lifecycle_state = CONTROLLER_RUNTIME_STATE_DESTROYED;
    status_view->last_error_code = last_error_code;
    if (last_reason_code != 0)
    {
        strncpy(status_view->last_reason_code, last_reason_code, sizeof(status_view->last_reason_code) - 1u);
    }
}

/**
 * @brief 清零借用的外部资源绑定。
 * @param runtime_state 目标运行状态结构。
 * @details 仅在资源释放后调用，用于避免悬空指针。
 */
static void runtime_zero_borrowed_bindings(controller_runtime_state_t *runtime_state)
{
    if (runtime_state == 0)
    {
        return;
    }

    memset(&runtime_state->config_snapshot, 0, sizeof(runtime_state->config_snapshot));
    runtime_state->controller_scheduler = 0;
    runtime_state->background_alarm_io_worker_thread = 0;
    runtime_state->background_alarm_detect_worker_thread = 0;
    runtime_state->system_context = 0;
    runtime_state->system_context_acquired = false;
    runtime_state->scheduler_created = false;
}

/**
 * @brief 验证调度器配置有效性。
 * @param scheduler_config 待验证配置。
 * @return 配置合法时返回 `ok`，否则返回 `INVALID_ARGUMENT`。
 * @details 检查周期、最大触发数和有界排干配置是否有效。
 */
static operation_result_t runtime_validate_scheduler_config(const controller_scheduler_config_t *scheduler_config)
{
    if (scheduler_config == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (scheduler_config->control_period_ms == 0ul)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (scheduler_config->max_triggers_per_tick == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (scheduler_config->exit_mode == CONTROLLER_SCHEDULER_EXIT_MODE_BOUNDED_DRAIN &&
        scheduler_config->bounded_drain_ticks == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    return operation_result_ok();
}

/**
 * @brief 验证句柄是否指向当前活跃 runtime。
 * @param runtime 待验证句柄。
 * @return 句柄合法且代次匹配时返回 `ok`，否则返回失败。
 */
static operation_result_t runtime_require_current_handle(const controller_runtime_t *runtime)
{
    uintptr_t generation;

    if (!runtime_decode_handle(runtime, &generation))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!g_controller_runtime_state.occupied || g_controller_runtime_state.active_generation == 0u)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (generation != g_controller_runtime_state.active_generation)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    return operation_result_ok();
}

/**
 * @brief 释放 runtime 持有的所有资源。
 * @return 全部释放成功返回 `ok`，否则返回对应错误码。
 * @details 按调度器再到 system_context 的顺序释放资源。
 */
static operation_result_t runtime_destroy_owned_resources(void)
{
    operation_result_t release_result;
    error_code_t destroy_error_code;

    runtime_copy_last_reason_from_context(&g_controller_runtime_state);
    g_controller_runtime_state.last_error_code = ERROR_CODE_OK;
    destroy_error_code = ERROR_CODE_OK;
    release_result = operation_result_ok();

    runtime_stop_background_alarm_monitor();

    if (g_controller_runtime_state.controller_scheduler != 0)
    {
        controller_scheduler_linux_destroy(g_controller_runtime_state.controller_scheduler);
        g_controller_runtime_state.controller_scheduler = 0;
    }
    g_controller_runtime_state.scheduler_created = false;

    if (g_controller_runtime_state.system_context != 0)
    {
        release_result = system_context_release(g_controller_runtime_state.system_context);
        g_controller_runtime_state.system_context = 0;
        g_controller_runtime_state.system_context_acquired = false;
        if (!release_result.ok)
        {
            g_controller_runtime_state.last_error_code = release_result.error_code;
            destroy_error_code = release_result.error_code;
        }
    }

    g_controller_runtime_state.occupied = false;
    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_DESTROYED;
    g_controller_runtime_state.active_generation = 0u;

    runtime_zero_borrowed_bindings(&g_controller_runtime_state);
    return destroy_error_code == ERROR_CODE_OK ? operation_result_ok() : operation_result_fail(destroy_error_code);
}

void controller_runtime_config_init(controller_runtime_config_t *config)
{
    if (config == 0)
    {
        return;
    }
    memset(config, 0, sizeof(*config));
}

operation_result_t controller_runtime_config_validate(const controller_runtime_config_t *config)
{
    operation_result_t result;

    if (config == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (config->sensor_port == 0 || config->actuator_port == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!string_present(config->config_root) || !string_present(config->event_log_path))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    result = runtime_validate_scheduler_config(config->scheduler_config);
    if (!result.ok)
    {
        return result;
    }
    result = runtime_validate_background_alarm_monitor_config(config);
    if (!result.ok)
    {
        return result;
    }
    return operation_result_ok();
}

operation_result_t controller_runtime_create(controller_runtime_t **runtime, const controller_runtime_config_t *config)
{
    controller_scheduler_linux_stdio_t scheduler_stdio;
    operation_result_t result;
    uintptr_t active_generation;

    if (runtime == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    *runtime = 0;

    result = controller_runtime_config_validate(config);
    if (!result.ok)
    {
        return result;
    }
    if (g_controller_runtime_state.occupied)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    memset(&g_controller_runtime_state, 0, sizeof(g_controller_runtime_state));
    g_controller_runtime_state.occupied = true;
    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_UNAVAILABLE;
    g_controller_runtime_state.last_error_code = ERROR_CODE_OK;
    g_controller_runtime_state.config_snapshot = *config;

    result = system_context_acquire(&g_controller_runtime_state.system_context);
    if (!result.ok || g_controller_runtime_state.system_context == 0)
    {
        g_controller_runtime_state.last_error_code = result.ok ? ERROR_CODE_RESOURCE_UNAVAILABLE : result.error_code;
        (void)runtime_destroy_owned_resources();
        return operation_result_fail(g_controller_runtime_state.last_error_code);
    }
    g_controller_runtime_state.system_context_acquired = true;

    system_context_set_sensor_port(g_controller_runtime_state.system_context, config->sensor_port);
    system_context_set_actuator_port(g_controller_runtime_state.system_context, config->actuator_port);

    result = file_program_repository_init(g_controller_runtime_state.system_context, config->config_root);
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources();
        return result;
    }

    result = file_event_logger_init(g_controller_runtime_state.system_context, config->event_log_path);
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources();
        return result;
    }

    result = system_context_private_complete_initialization(g_controller_runtime_state.system_context);
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources();
        return result;
    }

    memset(&scheduler_stdio, 0, sizeof(scheduler_stdio));
    scheduler_stdio.input = config->command_input;
    scheduler_stdio.output = config->command_output;
    scheduler_stdio.error = config->command_error;
    g_controller_runtime_state.controller_scheduler = controller_scheduler_linux_create(
        g_controller_runtime_state.system_context, config->scheduler_config, &scheduler_stdio);
    if (g_controller_runtime_state.controller_scheduler == 0)
    {
        g_controller_runtime_state.last_error_code = ERROR_CODE_IO_FAILED;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources();
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    g_controller_runtime_state.scheduler_created = true;
    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_CREATED;
    runtime_copy_last_reason_from_context(&g_controller_runtime_state);

    active_generation = runtime_issue_generation();
    if (active_generation == 0u)
    {
        g_controller_runtime_state.last_error_code = ERROR_CODE_RESOURCE_UNAVAILABLE;
        (void)runtime_destroy_owned_resources();
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }
    g_controller_runtime_state.active_generation = active_generation;
    *runtime = runtime_handle_from_generation(active_generation);
    return operation_result_ok();
}

operation_result_t controller_runtime_run(controller_runtime_t *runtime)
{
    operation_result_t result;

    result = runtime_require_current_handle(runtime);
    if (!result.ok)
    {
        return result;
    }
    if (g_controller_runtime_state.lifecycle_state != CONTROLLER_RUNTIME_STATE_CREATED)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (g_controller_runtime_state.controller_scheduler == 0 || g_controller_runtime_state.system_context == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_RUNNING;
    g_controller_runtime_state.run_invoked = true;
    result = runtime_start_background_alarm_monitor();
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_TERMINATED;
        return result;
    }

    result = controller_scheduler_run(g_controller_runtime_state.controller_scheduler);
    runtime_stop_background_alarm_monitor();
    g_controller_runtime_state.last_error_code = result.error_code;
    runtime_copy_last_reason_from_context(&g_controller_runtime_state);
    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_TERMINATED;
    return result;
}

operation_result_t controller_runtime_destroy(controller_runtime_t *runtime)
{
    uintptr_t generation;
    operation_result_t result;

    if (runtime == 0)
    {
        return operation_result_ok();
    }

    if (!runtime_decode_handle(runtime, &generation))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (!g_controller_runtime_state.occupied || generation != g_controller_runtime_state.active_generation)
    {
        return runtime_generation_was_issued(generation) ? operation_result_ok()
                                                         : operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    result = runtime_require_current_handle(runtime);
    if (!result.ok)
    {
        return result;
    }
    return runtime_destroy_owned_resources();
}

operation_result_t controller_runtime_read_state(const controller_runtime_t *runtime,
                                                 controller_runtime_status_view_t *status_view)
{
    uintptr_t generation;

    if (status_view == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    memset(status_view, 0, sizeof(*status_view));
    if (runtime == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (!runtime_decode_handle(runtime, &generation))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (g_controller_runtime_state.occupied && generation == g_controller_runtime_state.active_generation)
    {
        runtime_build_status_view(status_view);
        return operation_result_ok();
    }
    if (!runtime_generation_was_issued(generation))
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    runtime_build_destroyed_status_view(status_view, ERROR_CODE_OK, 0);
    return operation_result_ok();
}

operation_result_t controller_runtime_private_read_system_context(const controller_runtime_t *runtime,
                                                                  system_context_t *system_context)
{
    operation_result_t result;

    if (system_context == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    *system_context = 0;

    result = runtime_require_current_handle(runtime);
    if (!result.ok)
    {
        return result;
    }
    if (!g_controller_runtime_state.system_context_acquired || g_controller_runtime_state.system_context == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    *system_context = g_controller_runtime_state.system_context;
    return operation_result_ok();
}

controller_scheduler_t *controller_runtime_private_scheduler(const controller_runtime_t *runtime)
{
    if (!runtime_require_current_handle(runtime).ok || !g_controller_runtime_state.scheduler_created)
    {
        return 0;
    }
    return g_controller_runtime_state.controller_scheduler;
}
