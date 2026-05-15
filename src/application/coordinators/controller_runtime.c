#include "application/coordinators/controller_runtime.h"

#include <stdint.h>
#include <string.h>

#include "adapters/logging/file_event_logger.h"
#include "adapters/outbound/file_program_repository.h"
#include "platform/linux/controller_scheduler_linux.h"
#include "src/application/coordinators/controller_runtime_private.h"
#include "src/application/coordinators/system_context_private.h"

/*
 * Controller Runtime 句柄编码方案（opaque token）：
 * 目的：防止外部直接解引用或伪造句柄；允许检测已释放/过期句柄。
 *
 * 编码布局（低位 → 高位）：
 *   [8位 magic] [16位 checksum] [N位 generation]
 * - magic: 固定标记 0x52u（ASCII 'R'），识别有效token
 * - checksum: XOR校验码，由generation和随机secret推导
 * - generation: 单调递增的代次号，用于检测use-after-free
 *
 * 流程：
 * 1. create: runtime_issue_generation() → runtime_handle_from_generation() → 返回编码后的opaque token
 * 2. 调用方: 保存token作为句柄
 * 3. 操作方: runtime_decode_handle() → 验证magic、checksum → 提取generation → 检查代次是否一致
 * 4. destroy: 递增generation（使旧token失效），清零占用标志
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
    controller_runtime_config_t config_snapshot;
    uintptr_t active_generation;
} controller_runtime_state_t;

static controller_runtime_state_t g_controller_runtime_state;
static uintptr_t g_controller_runtime_next_generation = 1u;
static uintptr_t g_controller_runtime_handle_secret = 0u;

/** @brief 判断字符串非空且非空字符串 */
static bool string_present(const char *value) { return value != 0 && value[0] != '\0'; }

/**
 * @brief 生成并缓存校验密钥
 * @details 由全局符号地址 XOR 推导，用于计算 handle checksum
 * @return 校验密钥，保证非零
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
 * @brief 计算 generation 的校验码
 * @param generation 代次号
 * @return 16 位校验码：通过 XOR 混合 generation、密钥、移位值推导
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
 * @brief 将 generation 编码成 opaque handle
 * @param generation 代次号
 * @return 编码后的 opaque token（布局：generation | checksum | magic），不指向真实内存
 * @note 返回的指针仅作 token 使用，调用方必须保存并在后续操作中传回
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
 * @brief 解码 opaque handle 并完整验证
 * @param runtime opaque handle token
 * @param[out] generation 指向输出 generation 的指针，可为 null
 * @return true 表示 handle 合法、magic 有效、checksum 通过；false 表示伪造或过期
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
    /* 检查magic marker */
    if ((token & CONTROLLER_RUNTIME_HANDLE_MAGIC_MASK) != CONTROLLER_RUNTIME_HANDLE_MAGIC)
    {
        return false;
    }

    /* 提取checksum和generation */
    checksum = (token >> CONTROLLER_RUNTIME_HANDLE_MAGIC_BITS) & CONTROLLER_RUNTIME_HANDLE_CHECKSUM_MASK;
    decoded_generation = token >> CONTROLLER_RUNTIME_HANDLE_GENERATION_SHIFT;
    if (decoded_generation == 0u)
    {
        return false;
    }
    /* 重算checksum验证 */
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
 * @brief 分配新的 generation 号
 * @return 单调递增的代次号；超过上限时返回 0（溢出标记）
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
 * @brief 判断 generation 是否曾被分配过
 * @param generation 代次号
 * @return true 表示此 generation 在历史上曾被分配；false 表示野指针或伪造句柄
 */
static bool runtime_generation_was_issued(uintptr_t generation)
{
    return generation > 0u && generation < g_controller_runtime_next_generation;
}

/**
 * @brief 从 system_context 复制最近的原因码到 runtime_state
 * @param runtime_state 目标运行状态结构
 * @note 用于运行时问题诊断；清空缓冲区后，从 system_context 读取最新原因码
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
 * @brief 从全局运行状态构建当前活跃 runtime 的状态快照
 * @param[out] status_view 输出状态视图
 * @details 包括生命周期状态、错误码、原因码、调度器视图等；若调度器视图不可用则标记为不可用
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
    /* 防止缓冲区越界：预留1字节给null终止符 */
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
 * @brief 为已释放的 runtime 构建销毁后的状态视图
 * @param[out] status_view 输出状态视图
 * @param last_error_code 最后的错误码
 * @param last_reason_code 最后的原因码
 * @details 状态标记为 DESTROYED，保留错误信息供后续查询
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
 * @brief 清零借用的外部资源指针
 * @param runtime_state 目标运行状态结构
 * @details 清零 system_context、scheduler、config；仅在释放资源后调用，防止悬空指针
 */
static void runtime_zero_borrowed_bindings(controller_runtime_state_t *runtime_state)
{
    if (runtime_state == 0)
    {
        return;
    }

    memset(&runtime_state->config_snapshot, 0, sizeof(runtime_state->config_snapshot));
    runtime_state->controller_scheduler = 0;
    runtime_state->system_context = 0;
    runtime_state->system_context_acquired = false;
    runtime_state->scheduler_created = false;
}

/**
 * @brief 验证调度器配置有效性
 * @param scheduler_config 待验证配置
 * @return 配置合法时返回 ok；任意字段无效时返回 INVALID_ARGUMENT
 * @details 检查项：周期非零、最大触发数非零、有界排干模式下排干刻度非零
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
 * @brief 验证句柄的有效性和当前性
 * @param runtime 待验证句柄
 * @return 句柄合法、实例占用、generation 匹配当前活跃 generation 时返回 ok；否则返回失败
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
 * @brief 释放 runtime 拥有的所有资源
 * @param retired_generation 待退役的 generation（目前未使用）
 * @return 资源释放成功时返回 ok；释放过程中任意失败返回相应错误码
 * @details 逆序释放：调度器 → system_context；清零所有指针；失败时保留错误信息
 */
static operation_result_t runtime_destroy_owned_resources(uintptr_t retired_generation)
{
    operation_result_t release_result;
    error_code_t destroy_error_code;

    runtime_copy_last_reason_from_context(&g_controller_runtime_state);
    g_controller_runtime_state.last_error_code = ERROR_CODE_OK;
    destroy_error_code = ERROR_CODE_OK;
    release_result = operation_result_ok();

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
    (void)retired_generation;

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
        (void)runtime_destroy_owned_resources(0u);
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
        (void)runtime_destroy_owned_resources(0u);
        return result;
    }

    result = file_event_logger_init(g_controller_runtime_state.system_context, config->event_log_path);
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources(0u);
        return result;
    }

    result = system_context_private_complete_initialization(g_controller_runtime_state.system_context);
    if (!result.ok)
    {
        g_controller_runtime_state.last_error_code = result.error_code;
        runtime_copy_last_reason_from_context(&g_controller_runtime_state);
        (void)runtime_destroy_owned_resources(0u);
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
        (void)runtime_destroy_owned_resources(0u);
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    g_controller_runtime_state.scheduler_created = true;
    g_controller_runtime_state.lifecycle_state = CONTROLLER_RUNTIME_STATE_CREATED;
    runtime_copy_last_reason_from_context(&g_controller_runtime_state);

    active_generation = runtime_issue_generation();
    if (active_generation == 0u)
    {
        g_controller_runtime_state.last_error_code = ERROR_CODE_RESOURCE_UNAVAILABLE;
        (void)runtime_destroy_owned_resources(0u);
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
    result = controller_scheduler_run(g_controller_runtime_state.controller_scheduler);
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
    return runtime_destroy_owned_resources(generation);
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
