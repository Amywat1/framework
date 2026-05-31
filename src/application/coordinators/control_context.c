#include "application/coordinators/control_context.h"

#include <stdatomic.h>
#include <string.h>

#include "domain/model/program_snapshot.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_program.h"
#include "domain/model/wash_session.h"
#include "shared/timeouts.h"
#include "src/application/coordinators/control_context_private.h"

#define MAX_EXTERNAL_TRIGGER_QUEUE_COUNT 8u

typedef struct control_context_state_t
{
    wash_program_t wash_program;
    wash_session_t wash_session;
    wash_execution_t wash_execution;
    wait_condition_t wait_condition;
    program_snapshot_t program_snapshot;
    state_transition_record_t last_transition_record;
    wash_trigger_event_t pending_triggers[MAX_PENDING_TRIGGER_COUNT];
    wash_trigger_event_t external_triggers[MAX_EXTERNAL_TRIGGER_QUEUE_COUNT];
    unsigned int pending_trigger_count;
    unsigned long current_time_ms;
    unsigned long next_session_sequence;
    unsigned long next_execution_sequence;
    unsigned long next_wait_condition_sequence;
    atomic_uint external_trigger_read_index;
    atomic_uint external_trigger_write_index;
    atomic_uint external_trigger_count;
    device_state_t device_state;
    bool global_fault_present;
    char global_fault_code[64];
    char global_fault_reason[128];
    char last_result_code[32];
    char last_reason_code[64];
    void *scheduler_binding;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    program_repository_port_t program_repository_port;
} control_context_state_t;

typedef struct control_context_instance_t
{
    bool initialized;
    control_context_state_t state;
} control_context_instance_t;

static control_context_instance_t s_control_context_instance;

/**
 * @brief 重置外部触发收件箱。
 * @param state 控制上下文状态，不能为空。
 */
static void reset_external_trigger_queue(control_context_state_t *state)
{
    if (state == 0)
    {
        return;
    }

    atomic_init(&state->external_trigger_read_index, 0u);
    atomic_init(&state->external_trigger_write_index, 0u);
    atomic_init(&state->external_trigger_count, 0u);
}

static void reset_control_context_state(control_context_state_t *state)
{
    memset(state, 0, sizeof(*state));
    /* memset 将标量字段归零，但域模型对象有自己的非零初始语义，必须显式初始化。 */
    wash_program_init(&state->wash_program, "", "");
    wash_session_reset(&state->wash_session);
    wash_execution_reset(&state->wash_execution);
    wait_condition_reset(&state->wait_condition);
    program_snapshot_reset(&state->program_snapshot);
    reset_external_trigger_queue(state);
}


operation_result_t control_context_mark_device_ready_stopped(void)
{
    control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    state = &s_control_context_instance.state;
    if (state->device_state != DEVICE_STATE_INIT)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    state->device_state = DEVICE_STATE_STOPPED;
    return operation_result_ok();
}

device_state_t control_context_private_device_state(void)
{
    if (!s_control_context_instance.initialized)
    {
        return DEVICE_STATE_STOPPED;
    }
    return s_control_context_instance.state.device_state;
}

void control_context_private_set_device_state(device_state_t device_state)
{
    if (!s_control_context_instance.initialized)
    {
        return;
    }
    s_control_context_instance.state.device_state = device_state;
}

const actuator_port_t *control_context_private_actuator_port(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.actuator_port;
}

const sensor_port_t *control_context_private_sensor_port(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.sensor_port;
}

bool control_context_private_global_fault_present(void)
{
    if (!s_control_context_instance.initialized)
    {
        return false;
    }
    return s_control_context_instance.state.global_fault_present;
}

const char *control_context_private_global_fault_reason(void)
{
    if (!s_control_context_instance.initialized)
    {
        return "";
    }
    return s_control_context_instance.state.global_fault_reason;
}

const char *control_context_private_global_fault_code(void)
{
    if (!s_control_context_instance.initialized)
    {
        return "";
    }
    return s_control_context_instance.state.global_fault_code;
}

void control_context_private_set_global_fault(const char *fault_code, const char *fault_reason)
{
    control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return;
    }

    state = &s_control_context_instance.state;
    state->global_fault_present = true;
    state->global_fault_code[0] = '\0';
    state->global_fault_reason[0] = '\0';
    if (fault_code != 0)
    {
        strncpy(state->global_fault_code, fault_code, sizeof(state->global_fault_code) - 1u);
        state->global_fault_code[sizeof(state->global_fault_code) - 1u] = '\0';
    }
    if (fault_reason != 0)
    {
        strncpy(state->global_fault_reason, fault_reason, sizeof(state->global_fault_reason) - 1u);
        state->global_fault_reason[sizeof(state->global_fault_reason) - 1u] = '\0';
    }
}

void control_context_private_clear_global_fault(void)
{
    control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return;
    }

    state = &s_control_context_instance.state;
    state->global_fault_present = false;
    state->global_fault_code[0] = '\0';
    state->global_fault_reason[0] = '\0';
}

const state_transition_record_t *control_context_private_last_transition_record(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.last_transition_record;
}

state_transition_record_t *control_context_private_last_transition_record_mutable(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.last_transition_record;
}

void control_context_private_set_latest_result(const char *result_code, const char *reason_code)
{
    control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return;
    }

    state = &s_control_context_instance.state;
    if (result_code != 0)
    {
        strncpy(state->last_result_code, result_code, sizeof(state->last_result_code) - 1u);
        state->last_result_code[sizeof(state->last_result_code) - 1u] = '\0';
    }
    if (reason_code != 0)
    {
        strncpy(state->last_reason_code, reason_code, sizeof(state->last_reason_code) - 1u);
        state->last_reason_code[sizeof(state->last_reason_code) - 1u] = '\0';
    }
}

/**
 * @brief 统一写入设备运行时落点。
 * @param device_state 需要落到的设备状态。
 * @param result_code 最新结果码；传入 `0` 时保持现有结果码不变。
 * @param reason_code 最新原因码；传入 `0` 时保持现有原因码不变。
 */
void control_context_private_apply_device_state_outcome(device_state_t device_state,
                                                        const char *result_code, const char *reason_code)
{
    control_context_private_set_device_state(device_state);
    if (result_code != 0 || reason_code != 0)
    {
        control_context_private_set_latest_result(result_code, reason_code);
    }
}

/**
 * @brief 应用 start accepted 路径的会话启动落点。
 * @param wash_session 可变洗车会话；调用后会清空上次关联键。
 * @param wash_execution 可变执行快照；调用后会重置执行状态并启动首段。
 * @return 首段启动结果；成功时会落到 running，并写入 accepted/session_started。
 */
operation_result_t control_context_private_apply_start_accepted(wash_session_t *wash_session,
                                                               wash_execution_t *wash_execution)
{
    operation_result_t result;
    wash_execution_service_args_t wash_execution_service_args;
    wash_execution_fact_t wash_execution_fact;

    wash_execution_reset(wash_execution);
    wash_execution->segment_index = -1;
    wash_session->last_correlation_key[0] = '\0';

    control_context_private_build_execution_service_args(&wash_execution_service_args);
    result = wash_execution_service_begin_next_segment(&wash_execution_service_args, &wash_execution_fact);
    if (result.ok)
    {
        control_context_private_apply_device_state_outcome(DEVICE_STATE_RUNNING, "accepted",
                                                           "session_started");
    }
    return result;
}

void control_context_private_build_session_service_args(wash_session_service_args_t *wash_session_service_args)
{
    control_context_state_t *state;

    if (wash_session_service_args == 0)
    {
        return;
    }

    memset(wash_session_service_args, 0, sizeof(*wash_session_service_args));
    if (!s_control_context_instance.initialized)
    {
        return;
    }

    state = &s_control_context_instance.state;
    wash_session_service_args->wash_session = &state->wash_session;
    wash_session_service_args->program_snapshot = &state->program_snapshot;
    wash_session_service_args->next_session_sequence = &state->next_session_sequence;
    wash_session_service_args->current_time_ms = state->current_time_ms;
}

void control_context_private_build_program_snapshot_service_args(
    program_snapshot_service_args_t *program_snapshot_service_args)
{
    control_context_state_t *state;

    if (program_snapshot_service_args == 0)
    {
        return;
    }

    memset(program_snapshot_service_args, 0, sizeof(*program_snapshot_service_args));
    if (!s_control_context_instance.initialized)
    {
        return;
    }

    state = &s_control_context_instance.state;
    program_snapshot_service_args->program_snapshot = &state->program_snapshot;
    program_snapshot_service_args->wash_program = &state->wash_program;
    program_snapshot_service_args->program_repository_port = &state->program_repository_port;
    program_snapshot_service_args->current_time_ms = state->current_time_ms;
}

void control_context_private_build_execution_service_args(wash_execution_service_args_t *wash_execution_service_args)
{
    control_context_state_t *state;

    if (wash_execution_service_args == 0)
    {
        return;
    }

    memset(wash_execution_service_args, 0, sizeof(*wash_execution_service_args));
    if (!s_control_context_instance.initialized)
    {
        return;
    }

    state = &s_control_context_instance.state;
    wash_execution_service_args->wash_execution = &state->wash_execution;
    wash_execution_service_args->wash_session = &state->wash_session;
    wash_execution_service_args->wait_condition = &state->wait_condition;
    wash_execution_service_args->program_snapshot = &state->program_snapshot;
    wash_execution_service_args->actuator_port = &state->actuator_port;
    wash_execution_service_args->sensor_port = &state->sensor_port;
    wash_execution_service_args->next_execution_sequence = &state->next_execution_sequence;
    wash_execution_service_args->next_wait_condition_sequence = &state->next_wait_condition_sequence;
    wash_execution_service_args->current_time_ms = state->current_time_ms;
}

const wash_session_t *control_context_private_wash_session(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.wash_session;
}

wash_session_t *control_context_private_wash_session_mutable(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.wash_session;
}

const wash_execution_t *control_context_private_wash_execution(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.wash_execution;
}

wash_execution_t *control_context_private_wash_execution_mutable(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.wash_execution;
}

wait_condition_t *control_context_private_wait_condition_mutable(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.wait_condition;
}

const program_snapshot_t *control_context_private_program_snapshot(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.program_snapshot;
}

operation_result_t control_context_private_enqueue_external_trigger(const wash_trigger_event_t *wash_trigger_event)
{
    control_context_state_t *state;
    unsigned int write_index;
    unsigned int next_count;

    if (wash_trigger_event == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!s_control_context_instance.initialized)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    state = &s_control_context_instance.state;
    next_count = atomic_load(&state->external_trigger_count);
    if (next_count >= MAX_EXTERNAL_TRIGGER_QUEUE_COUNT)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    write_index = atomic_load(&state->external_trigger_write_index);
    state->external_triggers[write_index] = *wash_trigger_event;
    write_index = (write_index + 1u) % MAX_EXTERNAL_TRIGGER_QUEUE_COUNT;
    atomic_store(&state->external_trigger_write_index, write_index);
    atomic_fetch_add(&state->external_trigger_count, 1u);
    return operation_result_ok();
}

operation_result_t control_context_init(void)
{
    if (s_control_context_instance.initialized)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    reset_control_context_state(&s_control_context_instance.state);
    s_control_context_instance.initialized = true;
    return operation_result_ok();
}

operation_result_t control_context_deinit(void)
{
    if (!s_control_context_instance.initialized)
    {
        return operation_result_ok();
    }
    if (s_control_context_instance.state.scheduler_binding != 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    reset_control_context_state(&s_control_context_instance.state);
    s_control_context_instance.initialized = false;
    return operation_result_ok();
}

operation_result_t control_context_reset_runtime_keep_bindings(void)
{
    actuator_port_t actuator_port;
    program_repository_port_t program_repository_port;
    void *scheduler_binding;
    sensor_port_t sensor_port;
    control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    state = &s_control_context_instance.state;
    sensor_port = state->sensor_port;
    actuator_port = state->actuator_port;
    program_repository_port = state->program_repository_port;
    scheduler_binding = state->scheduler_binding;

    reset_control_context_state(state);
    state->scheduler_binding = scheduler_binding;
    state->sensor_port = sensor_port;
    state->actuator_port = actuator_port;
    state->program_repository_port = program_repository_port;
    return control_context_mark_device_ready_stopped();
}

void control_context_set_sensor_port(const sensor_port_t *sensor_port)
{
    control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return;
    }

    state = &s_control_context_instance.state;
    if (sensor_port == 0)
    {
        memset(&state->sensor_port, 0, sizeof(state->sensor_port));
        return;
    }
    state->sensor_port = *sensor_port;
}

void control_context_set_actuator_port(const actuator_port_t *actuator_port)
{
    control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return;
    }

    state = &s_control_context_instance.state;
    if (actuator_port == 0)
    {
        memset(&state->actuator_port, 0, sizeof(state->actuator_port));
        return;
    }
    state->actuator_port = *actuator_port;
}

void control_context_set_program_repository_port(const program_repository_port_t *program_repository_port)
{
    control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return;
    }

    state = &s_control_context_instance.state;
    if (program_repository_port == 0)
    {
        memset(&state->program_repository_port, 0, sizeof(state->program_repository_port));
        return;
    }
    state->program_repository_port = *program_repository_port;
}

const program_repository_port_t *control_context_program_repository_port(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.program_repository_port;
}

unsigned long control_context_current_time_ms(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0ul;
    }
    return s_control_context_instance.state.current_time_ms;
}

unsigned int control_context_pending_trigger_count(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0u;
    }
    return s_control_context_instance.state.pending_trigger_count;
}

unsigned int control_context_count_pending_triggers_by_id(const char *trigger_id)
{
    const control_context_state_t *state;
    unsigned int count;
    unsigned int index;

    if (!s_control_context_instance.initialized || trigger_id == 0 || trigger_id[0] == '\0')
    {
        return 0u;
    }

    state = &s_control_context_instance.state;
    count = 0u;
    for (index = 0u; index < state->pending_trigger_count; ++index)
    {
        if (strcmp(state->pending_triggers[index].trigger_id, trigger_id) == 0)
        {
            count += 1u;
        }
    }
    return count;
}

unsigned int control_context_count_pending_triggers_by_type(trigger_type_t trigger_type)
{
    const control_context_state_t *state;
    unsigned int count;
    unsigned int index;

    if (!s_control_context_instance.initialized)
    {
        return 0u;
    }

    state = &s_control_context_instance.state;
    count = 0u;
    for (index = 0u; index < state->pending_trigger_count; ++index)
    {
        if (state->pending_triggers[index].trigger_type == trigger_type)
        {
            count += 1u;
        }
    }
    return count;
}

bool control_context_has_pending_work(void)
{
    if (!s_control_context_instance.initialized)
    {
        return false;
    }

    return s_control_context_instance.state.pending_trigger_count > 0u ||
           atomic_load(&s_control_context_instance.state.external_trigger_count) > 0u ||
           wait_condition_is_timed_out(&s_control_context_instance.state.wait_condition,
                                       s_control_context_instance.state.current_time_ms);
}

bool control_context_wait_condition_active(void)
{
    if (!s_control_context_instance.initialized)
    {
        return false;
    }
    return s_control_context_instance.state.wait_condition.active;
}

unsigned long control_context_wait_timeout_ms(void)
{
    const control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return 0ul;
    }

    state = &s_control_context_instance.state;
    if (!state->wait_condition.active)
    {
        return 0ul;
    }
    if (state->current_time_ms >= state->wait_condition.timeout_at_ms)
    {
        return 0ul;
    }
    return state->wait_condition.timeout_at_ms - state->current_time_ms;
}

const char *control_context_last_result_code(void)
{
    if (!s_control_context_instance.initialized)
    {
        return "";
    }
    return s_control_context_instance.state.last_result_code;
}

const char *control_context_last_reason_code(void)
{
    if (!s_control_context_instance.initialized)
    {
        return "";
    }
    return s_control_context_instance.state.last_reason_code;
}

device_state_t control_context_device_state(void)
{
    if (!s_control_context_instance.initialized)
    {
        return DEVICE_STATE_STOPPED;
    }
    return s_control_context_instance.state.device_state;
}

const wash_session_t *control_context_wash_session(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.wash_session;
}

operation_result_t control_context_bind_scheduler(void *scheduler_binding)
{
    control_context_state_t *state;

    if (scheduler_binding == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!s_control_context_instance.initialized)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    state = &s_control_context_instance.state;
    if (state->scheduler_binding != 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    state->scheduler_binding = scheduler_binding;
    return operation_result_ok();
}

void control_context_unbind_scheduler(void)
{
    if (!s_control_context_instance.initialized)
    {
        return;
    }
    s_control_context_instance.state.scheduler_binding = 0;
}

void *control_context_bound_scheduler(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return s_control_context_instance.state.scheduler_binding;
}

const wash_trigger_event_t *control_context_pending_trigger_at(unsigned int index)
{
    const control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return 0;
    }

    state = &s_control_context_instance.state;
    if (index >= state->pending_trigger_count)
    {
        return 0;
    }
    return &state->pending_triggers[index];
}

operation_result_t control_context_append_trigger(const wash_trigger_event_t *wash_trigger_event)
{
    control_context_state_t *state;

    if (!s_control_context_instance.initialized)
    {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }
    if (wash_trigger_event == 0)
    {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    state = &s_control_context_instance.state;
    if (state->pending_trigger_count >= MAX_PENDING_TRIGGER_COUNT)
    {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    state->pending_triggers[state->pending_trigger_count++] = *wash_trigger_event;
    return operation_result_ok();
}

void control_context_remove_pending_trigger_at(unsigned int remove_index)
{
    control_context_state_t *state;
    unsigned int index;

    if (!s_control_context_instance.initialized)
    {
        return;
    }

    state = &s_control_context_instance.state;
    if (remove_index >= state->pending_trigger_count)
    {
        return;
    }
    for (index = remove_index + 1u; index < state->pending_trigger_count; ++index)
    {
        state->pending_triggers[index - 1u] = state->pending_triggers[index];
    }
    state->pending_trigger_count -= 1u;
}

bool control_context_try_pop_external_trigger(wash_trigger_event_t *wash_trigger_event)
{
    control_context_state_t *state;
    unsigned int read_index;

    if (wash_trigger_event == 0 || !s_control_context_instance.initialized)
    {
        return false;
    }

    state = &s_control_context_instance.state;
    if (atomic_load(&state->external_trigger_count) == 0u)
    {
        return false;
    }

    read_index = atomic_load(&state->external_trigger_read_index);
    *wash_trigger_event = state->external_triggers[read_index];
    read_index = (read_index + 1u) % MAX_EXTERNAL_TRIGGER_QUEUE_COUNT;
    atomic_store(&state->external_trigger_read_index, read_index);
    atomic_fetch_sub(&state->external_trigger_count, 1u);
    return true;
}

const wait_condition_t *control_context_wait_condition(void)
{
    if (!s_control_context_instance.initialized)
    {
        return 0;
    }
    return &s_control_context_instance.state.wait_condition;
}

void control_context_advance_time(unsigned long elapsed_ms)
{
    if (!s_control_context_instance.initialized)
    {
        return;
    }
    s_control_context_instance.state.current_time_ms += elapsed_ms;
}
