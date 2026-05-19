#include "application/coordinators/system_context.h"

#include <stdatomic.h>
#include <string.h>

#include "domain/model/program_snapshot.h"
#include "domain/model/vehicle_type.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_program.h"
#include "domain/model/wash_session.h"
#include "src/application/coordinators/system_context_runtime_layout.h"

struct system_context_handle
{
    unsigned int reserved;
};

static system_context_instance_state_t s_system_context_instance;
static struct system_context_handle s_system_context_handle;

/**
 * @brief 初始化外部触发收件箱。
 * @param runtime 运行时对象，不能为空。
 */
static void initialize_external_trigger_queue(system_context_runtime_t *runtime)
{
    if (runtime == 0)
    {
        return;
    }

    atomic_init(&runtime->external_trigger_read_index, 0u);
    atomic_init(&runtime->external_trigger_write_index, 0u);
    atomic_init(&runtime->external_trigger_count, 0u);
}

static void initialize_runtime_object(system_context_runtime_t *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    /* memset 将标量字段归零，但域模型对象有自己的非零初始语义，必须显式初始化。 */
    wash_program_init(&runtime->wash_program, "", "");
    vehicle_type_init(&runtime->vehicle_type, "", "");
    wash_session_reset(&runtime->wash_session);
    wash_execution_reset(&runtime->wash_execution);
    wait_condition_reset(&runtime->wait_condition);
    program_snapshot_reset(&runtime->program_snapshot);
    initialize_external_trigger_queue(runtime);
}

static operation_result_t require_active_instance(const system_context_t system_context)
{
    if (system_context == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (system_context != &s_system_context_handle)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!s_system_context_instance.in_use)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    return operation_result_ok();
}

operation_result_t system_context_private_require_active(const system_context_t system_context)
{
    return require_active_instance(system_context);
}

operation_result_t system_context_private_complete_initialization(system_context_t system_context)
{
    system_context_runtime_t *runtime;
    operation_result_t result;

    result = require_active_instance(system_context);
    if (!result.ok)
    {
        return result;
    }

    runtime = &s_system_context_instance.runtime;
    if (runtime->device_state != DEVICE_STATE_INIT)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    runtime->device_state = DEVICE_STATE_STOPPED;
    return operation_result_ok();
}

operation_result_t system_context_private_bind_scheduler(system_context_t system_context,
                                                         void *scheduler_binding)
{
    system_context_runtime_t *runtime;
    operation_result_t result;

    if (scheduler_binding == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    result = require_active_instance(system_context);
    if (!result.ok)
    {
        return result;
    }

    runtime = &s_system_context_instance.runtime;
    if (runtime->scheduler_binding != 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    runtime->scheduler_binding = scheduler_binding;
    return operation_result_ok();
}

void system_context_private_unbind_scheduler(system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return;
    }
    s_system_context_instance.runtime.scheduler_binding = 0;
}

void *system_context_private_bound_scheduler(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return s_system_context_instance.runtime.scheduler_binding;
}

device_state_t system_context_private_device_state(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return DEVICE_STATE_STOPPED;
    }
    return s_system_context_instance.runtime.device_state;
}

void system_context_private_set_device_state(system_context_t system_context, device_state_t device_state)
{
    if (!require_active_instance(system_context).ok)
    {
        return;
    }
    s_system_context_instance.runtime.device_state = device_state;
}

const actuator_port_t *system_context_private_actuator_port(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.actuator_port;
}

const sensor_port_t *system_context_private_sensor_port(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.sensor_port;
}

bool system_context_private_global_fault_present(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return false;
    }
    return s_system_context_instance.runtime.global_fault_present;
}

const char *system_context_private_global_fault_reason(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return "";
    }
    return s_system_context_instance.runtime.global_fault_reason;
}

void system_context_private_set_global_fault(system_context_t system_context, const char *fault_code,
                                             const char *fault_reason)
{
    system_context_runtime_t *runtime;

    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    runtime->global_fault_present = true;
    runtime->global_fault_code[0] = '\0';
    runtime->global_fault_reason[0] = '\0';
    if (fault_code != 0)
    {
        strncpy(runtime->global_fault_code, fault_code, sizeof(runtime->global_fault_code) - 1u);
        runtime->global_fault_code[sizeof(runtime->global_fault_code) - 1u] = '\0';
    }
    if (fault_reason != 0)
    {
        strncpy(runtime->global_fault_reason, fault_reason, sizeof(runtime->global_fault_reason) - 1u);
        runtime->global_fault_reason[sizeof(runtime->global_fault_reason) - 1u] = '\0';
    }
}

void system_context_private_clear_global_fault(system_context_t system_context)
{
    system_context_runtime_t *runtime;

    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    runtime->global_fault_present = false;
    runtime->global_fault_code[0] = '\0';
    runtime->global_fault_reason[0] = '\0';
}

const state_transition_record_t *system_context_private_last_transition_record(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.last_transition_record;
}

state_transition_record_t *system_context_private_last_transition_record_mutable(system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.last_transition_record;
}

void system_context_private_set_latest_result(system_context_t system_context, const char *result_code,
                                              const char *reason_code)
{
    system_context_runtime_t *runtime;

    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    if (result_code != 0)
    {
        strncpy(runtime->last_result_code, result_code, sizeof(runtime->last_result_code) - 1u);
        runtime->last_result_code[sizeof(runtime->last_result_code) - 1u] = '\0';
    }
    if (reason_code != 0)
    {
        strncpy(runtime->last_reason_code, reason_code, sizeof(runtime->last_reason_code) - 1u);
        runtime->last_reason_code[sizeof(runtime->last_reason_code) - 1u] = '\0';
    }
}

/**
 * @brief 统一写入设备运行时落点。
 * @param system_context 系统上下文；必须为当前激活实例。
 * @param device_state 需要落到的设备状态。
 * @param result_code 最新结果码；传入 `0` 时保持现有结果码不变。
 * @param reason_code 最新原因码；传入 `0` 时保持现有原因码不变。
 */
void system_context_private_apply_device_runtime_result(system_context_t system_context, device_state_t device_state,
                                                        const char *result_code, const char *reason_code)
{
    system_context_private_set_device_state(system_context, device_state);
    if (result_code != 0 || reason_code != 0)
    {
        system_context_private_set_latest_result(system_context, result_code, reason_code);
    }
}

/**
 * @brief 应用 start accepted 路径的会话启动落点。
 * @param system_context 系统上下文；调用前要求 session start 已成功。
 * @param wash_session 可变洗车会话；调用后会清空上次关联键。
 * @param wash_execution 可变执行快照；调用后会重置执行状态并启动首段。
 * @return 首段启动结果；成功时会落到 running，并写入 accepted/session_started。
 */
operation_result_t system_context_private_apply_start_accepted(system_context_t system_context,
                                                               wash_session_t *wash_session,
                                                               wash_execution_t *wash_execution)
{
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;

    wash_execution_reset(wash_execution);
    wash_execution->segment_index = -1;
    wash_session->last_correlation_key[0] = '\0';

    system_context_private_build_execution_service_args(system_context, &wash_execution_service_args);
    result = wash_execution_service_begin_next_segment(&wash_execution_service_args, &wash_execution_fact);
    if (result.ok)
    {
        system_context_private_apply_device_runtime_result(system_context, DEVICE_STATE_RUNNING, "accepted",
                                                           "session_started");
    }
    return result;
}

const event_logger_port_t *system_context_private_event_logger_port(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.event_logger_port;
}

void system_context_private_build_session_service_args(system_context_t system_context,
                                                       wash_session_service_args_t *wash_session_service_args)
{
    system_context_runtime_t *runtime;

    if (wash_session_service_args == 0)
    {
        return;
    }

    memset(wash_session_service_args, 0, sizeof(*wash_session_service_args));
    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    wash_session_service_args->wash_session = &runtime->wash_session;
    wash_session_service_args->program_snapshot = &runtime->program_snapshot;
    wash_session_service_args->next_session_sequence = &runtime->next_session_sequence;
    wash_session_service_args->current_time_ms = runtime->current_time_ms;
}

void system_context_private_build_program_snapshot_service_args(
    system_context_t system_context, program_snapshot_service_args_t *program_snapshot_service_args)
{
    system_context_runtime_t *runtime;

    if (program_snapshot_service_args == 0)
    {
        return;
    }

    memset(program_snapshot_service_args, 0, sizeof(*program_snapshot_service_args));
    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    program_snapshot_service_args->program_snapshot = &runtime->program_snapshot;
    program_snapshot_service_args->wash_program = &runtime->wash_program;
    program_snapshot_service_args->program_repository_port = &runtime->program_repository_port;
    program_snapshot_service_args->current_time_ms = runtime->current_time_ms;
}

void system_context_private_build_execution_service_args(system_context_t system_context,
                                                         wash_execution_service_args_t *wash_execution_service_args)
{
    system_context_runtime_t *runtime;

    if (wash_execution_service_args == 0)
    {
        return;
    }

    memset(wash_execution_service_args, 0, sizeof(*wash_execution_service_args));
    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    wash_execution_service_args->wash_execution = &runtime->wash_execution;
    wash_execution_service_args->wash_session = &runtime->wash_session;
    wash_execution_service_args->wait_condition = &runtime->wait_condition;
    wash_execution_service_args->program_snapshot = &runtime->program_snapshot;
    wash_execution_service_args->actuator_port = &runtime->actuator_port;
    wash_execution_service_args->sensor_port = &runtime->sensor_port;
    wash_execution_service_args->next_execution_sequence = &runtime->next_execution_sequence;
    wash_execution_service_args->next_wait_condition_sequence = &runtime->next_wait_condition_sequence;
    wash_execution_service_args->current_time_ms = runtime->current_time_ms;
}

const wash_session_t *system_context_private_wash_session(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.wash_session;
}

wash_session_t *system_context_private_wash_session_mutable(system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.wash_session;
}

const wash_execution_t *system_context_private_wash_execution(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.wash_execution;
}

wash_execution_t *system_context_private_wash_execution_mutable(system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.wash_execution;
}

const wait_condition_t *system_context_private_wait_condition(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.wait_condition;
}

wait_condition_t *system_context_private_wait_condition_mutable(system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.wait_condition;
}

const program_snapshot_t *system_context_private_program_snapshot(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.program_snapshot;
}

void system_context_private_advance_time(system_context_t system_context, unsigned long elapsed_ms)
{
    if (!require_active_instance(system_context).ok)
    {
        return;
    }
    s_system_context_instance.runtime.current_time_ms += elapsed_ms;
}

operation_result_t system_context_private_append_trigger(system_context_t system_context,
                                                         const wash_trigger_event_t *wash_trigger_event)
{
    system_context_runtime_t *runtime;
    operation_result_t result;

    result = require_active_instance(system_context);
    if (!result.ok)
    {
        return result;
    }
    if (wash_trigger_event == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    runtime = &s_system_context_instance.runtime;
    if (runtime->pending_trigger_count >= MAX_PENDING_TRIGGER_COUNT)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    runtime->pending_triggers[runtime->pending_trigger_count++] = *wash_trigger_event;
    return operation_result_ok();
}

operation_result_t system_context_private_enqueue_external_trigger(system_context_t system_context,
                                                                  const wash_trigger_event_t *wash_trigger_event)
{
    system_context_runtime_t *runtime;
    unsigned int write_index;
    unsigned int next_count;

    if (wash_trigger_event == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!require_active_instance(system_context).ok)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    runtime = &s_system_context_instance.runtime;
    next_count = atomic_load(&runtime->external_trigger_count);
    if (next_count >= MAX_EXTERNAL_TRIGGER_QUEUE_COUNT)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    write_index = atomic_load(&runtime->external_trigger_write_index);
    runtime->external_triggers[write_index] = *wash_trigger_event;
    write_index = (write_index + 1u) % MAX_EXTERNAL_TRIGGER_QUEUE_COUNT;
    atomic_store(&runtime->external_trigger_write_index, write_index);
    atomic_fetch_add(&runtime->external_trigger_count, 1u);
    return operation_result_ok();
}

bool system_context_private_try_pop_external_trigger(system_context_t system_context,
                                                     wash_trigger_event_t *wash_trigger_event)
{
    system_context_runtime_t *runtime;
    unsigned int read_index;

    if (wash_trigger_event == 0 || !require_active_instance(system_context).ok)
    {
        return false;
    }

    runtime = &s_system_context_instance.runtime;
    if (atomic_load(&runtime->external_trigger_count) == 0u)
    {
        return false;
    }

    read_index = atomic_load(&runtime->external_trigger_read_index);
    *wash_trigger_event = runtime->external_triggers[read_index];
    read_index = (read_index + 1u) % MAX_EXTERNAL_TRIGGER_QUEUE_COUNT;
    atomic_store(&runtime->external_trigger_read_index, read_index);
    atomic_fetch_sub(&runtime->external_trigger_count, 1u);
    return true;
}

unsigned int system_context_private_external_trigger_count(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0u;
    }

    return atomic_load(&s_system_context_instance.runtime.external_trigger_count);
}

const wash_trigger_event_t *system_context_private_pending_trigger_at(const system_context_t system_context,
                                                                      unsigned int index)
{
    const system_context_runtime_t *runtime;

    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }

    runtime = &s_system_context_instance.runtime;
    if (index >= runtime->pending_trigger_count)
    {
        return 0;
    }
    return &runtime->pending_triggers[index];
}

void system_context_private_remove_pending_trigger_at(system_context_t system_context, unsigned int remove_index)
{
    system_context_runtime_t *runtime;
    unsigned int index;

    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    if (remove_index >= runtime->pending_trigger_count)
    {
        return;
    }
    for (index = remove_index + 1u; index < runtime->pending_trigger_count; ++index)
    {
        runtime->pending_triggers[index - 1u] = runtime->pending_triggers[index];
    }
    runtime->pending_trigger_count -= 1u;
}

system_context_runtime_t *system_context_private_runtime_mutable(system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime;
}

const system_context_runtime_t *system_context_private_runtime_const(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime;
}

bool system_context_private_debug_is_in_use(const system_context_t system_context)
{
    return s_system_context_instance.in_use && system_context == &s_system_context_handle;
}

operation_result_t system_context_acquire(system_context_t *system_context)
{
    if (system_context == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    *system_context = 0;
    if (s_system_context_instance.in_use)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    initialize_runtime_object(&s_system_context_instance.runtime);
    s_system_context_instance.in_use = true;
    *system_context = &s_system_context_handle;
    return operation_result_ok();
}

operation_result_t system_context_release(system_context_t system_context)
{
    operation_result_t result;

    result = require_active_instance(system_context);
    if (!result.ok)
    {
        return result;
    }
    if (s_system_context_instance.runtime.scheduler_binding != 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    initialize_runtime_object(&s_system_context_instance.runtime);
    s_system_context_instance.in_use = false;
    return operation_result_ok();
}

operation_result_t system_context_reset(system_context_t system_context)
{
    actuator_port_t actuator_port;
    event_logger_port_t event_logger_port;
    operation_result_t result;
    program_repository_port_t program_repository_port;
    void *scheduler_binding;
    sensor_port_t sensor_port;
    system_context_runtime_t *runtime;

    result = require_active_instance(system_context);
    if (!result.ok)
    {
        return result;
    }

    runtime = &s_system_context_instance.runtime;
    sensor_port = runtime->sensor_port;
    actuator_port = runtime->actuator_port;
    event_logger_port = runtime->event_logger_port;
    program_repository_port = runtime->program_repository_port;
    scheduler_binding = runtime->scheduler_binding;

    initialize_runtime_object(runtime);
    runtime->scheduler_binding = scheduler_binding;
    runtime->sensor_port = sensor_port;
    runtime->actuator_port = actuator_port;
    runtime->event_logger_port = event_logger_port;
    runtime->program_repository_port = program_repository_port;
    return system_context_private_complete_initialization(system_context);
}

void system_context_set_sensor_port(system_context_t system_context, const sensor_port_t *sensor_port)
{
    system_context_runtime_t *runtime;

    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    if (sensor_port == 0)
    {
        memset(&runtime->sensor_port, 0, sizeof(runtime->sensor_port));
        return;
    }
    runtime->sensor_port = *sensor_port;
}

void system_context_set_actuator_port(system_context_t system_context, const actuator_port_t *actuator_port)
{
    system_context_runtime_t *runtime;

    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    if (actuator_port == 0)
    {
        memset(&runtime->actuator_port, 0, sizeof(runtime->actuator_port));
        return;
    }
    runtime->actuator_port = *actuator_port;
}

void system_context_set_event_logger_port(system_context_t system_context, const event_logger_port_t *event_logger_port)
{
    system_context_runtime_t *runtime;

    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    if (event_logger_port == 0)
    {
        memset(&runtime->event_logger_port, 0, sizeof(runtime->event_logger_port));
        return;
    }
    runtime->event_logger_port = *event_logger_port;
}

void system_context_set_program_repository_port(system_context_t system_context,
                                                const program_repository_port_t *program_repository_port)
{
    system_context_runtime_t *runtime;

    if (!require_active_instance(system_context).ok)
    {
        return;
    }

    runtime = &s_system_context_instance.runtime;
    if (program_repository_port == 0)
    {
        memset(&runtime->program_repository_port, 0, sizeof(runtime->program_repository_port));
        return;
    }
    runtime->program_repository_port = *program_repository_port;
}

const program_repository_port_t *system_context_program_repository_port(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0;
    }
    return &s_system_context_instance.runtime.program_repository_port;
}

unsigned long system_context_current_time_ms(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0ul;
    }
    return s_system_context_instance.runtime.current_time_ms;
}

unsigned int system_context_pending_trigger_count(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return 0u;
    }
    return s_system_context_instance.runtime.pending_trigger_count;
}

unsigned int system_context_count_pending_triggers_by_id(const system_context_t system_context, const char *trigger_id)
{
    const system_context_runtime_t *runtime;
    unsigned int count;
    unsigned int index;

    if (!require_active_instance(system_context).ok || trigger_id == 0 || trigger_id[0] == '\0')
    {
        return 0u;
    }

    runtime = &s_system_context_instance.runtime;
    count = 0u;
    for (index = 0u; index < runtime->pending_trigger_count; ++index)
    {
        if (strcmp(runtime->pending_triggers[index].trigger_id, trigger_id) == 0)
        {
            count += 1u;
        }
    }
    return count;
}

unsigned int system_context_count_pending_triggers_by_type(const system_context_t system_context,
                                                           trigger_type_t trigger_type)
{
    const system_context_runtime_t *runtime;
    unsigned int count;
    unsigned int index;

    if (!require_active_instance(system_context).ok)
    {
        return 0u;
    }

    runtime = &s_system_context_instance.runtime;
    count = 0u;
    for (index = 0u; index < runtime->pending_trigger_count; ++index)
    {
        if (runtime->pending_triggers[index].trigger_type == trigger_type)
        {
            count += 1u;
        }
    }
    return count;
}

bool system_context_has_pending_runtime_work(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return false;
    }

    return s_system_context_instance.runtime.pending_trigger_count > 0u ||
           system_context_private_external_trigger_count(system_context) > 0u ||
           wait_condition_is_timed_out(&s_system_context_instance.runtime.wait_condition,
                                       s_system_context_instance.runtime.current_time_ms);
}

bool system_context_wait_condition_active(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return false;
    }
    return s_system_context_instance.runtime.wait_condition.active;
}

unsigned long system_context_wait_timeout_ms(const system_context_t system_context)
{
    const system_context_runtime_t *runtime;

    if (!require_active_instance(system_context).ok)
    {
        return 0ul;
    }

    runtime = &s_system_context_instance.runtime;
    if (!runtime->wait_condition.active)
    {
        return 0ul;
    }
    if (runtime->current_time_ms >= runtime->wait_condition.timeout_at_ms)
    {
        return 0ul;
    }
    return runtime->wait_condition.timeout_at_ms - runtime->current_time_ms;
}

const char *system_context_last_result_code(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return "";
    }
    return s_system_context_instance.runtime.last_result_code;
}

const char *system_context_last_reason_code(const system_context_t system_context)
{
    if (!require_active_instance(system_context).ok)
    {
        return "";
    }
    return s_system_context_instance.runtime.last_reason_code;
}

operation_result_t system_context_bind_scheduler(system_context_t system_context,
                                                 void *scheduler_binding)
{
    return system_context_private_bind_scheduler(system_context, scheduler_binding);
}

void system_context_unbind_scheduler(system_context_t system_context)
{
    system_context_private_unbind_scheduler(system_context);
}

void *system_context_bound_scheduler(const system_context_t system_context)
{
    return system_context_private_bound_scheduler(system_context);
}

operation_result_t system_context_require_active(system_context_t system_context)
{
    return system_context_private_require_active(system_context);
}

const wash_trigger_event_t *system_context_pending_trigger_at(const system_context_t system_context, unsigned int index)
{
    return system_context_private_pending_trigger_at(system_context, index);
}

operation_result_t system_context_append_trigger(system_context_t system_context,
                                                 const wash_trigger_event_t *wash_trigger_event)
{
    return system_context_private_append_trigger(system_context, wash_trigger_event);
}

void system_context_remove_pending_trigger_at(system_context_t system_context, unsigned int index)
{
    system_context_private_remove_pending_trigger_at(system_context, index);
}

bool system_context_try_pop_external_trigger(system_context_t system_context, wash_trigger_event_t *wash_trigger_event)
{
    return system_context_private_try_pop_external_trigger(system_context, wash_trigger_event);
}

const wait_condition_t *system_context_wait_condition(const system_context_t system_context)
{
    return system_context_private_wait_condition(system_context);
}

void system_context_advance_time(system_context_t system_context, unsigned long elapsed_ms)
{
    system_context_private_advance_time(system_context, elapsed_ms);
}

const event_logger_port_t *system_context_event_logger_port(const system_context_t system_context)
{
    return system_context_private_event_logger_port(system_context);
}
