#include "application/coordinators/system_context.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "domain/model/program_snapshot.h"
#include "domain/model/vehicle_type.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_program.h"
#include "domain/model/wash_session.h"
#include "domain/services/wait_timeout_service.h"
#include "src/application/coordinators/system_context_private.h"

static system_context_pool_state_t s_system_context_pool;
static struct system_context_handle s_system_context_handles
    [SYSTEM_CONTEXT_POOL_CAPACITY][SYSTEM_CONTEXT_HANDLE_GENERATION_WINDOW];

#define SYSTEM_CONTEXT_HANDLE_SLOT_BITS 4u
#define SYSTEM_CONTEXT_HANDLE_SLOT_MASK ((uintptr_t)((1u << SYSTEM_CONTEXT_HANDLE_SLOT_BITS) - 1u))

_Static_assert(SYSTEM_CONTEXT_POOL_CAPACITY > 0u, "system_context pool capacity must be positive");
_Static_assert(SYSTEM_CONTEXT_POOL_CAPACITY <= SYSTEM_CONTEXT_HANDLE_SLOT_MASK,
    "system_context handle slot bits must cover pool capacity");
_Static_assert(SYSTEM_CONTEXT_HANDLE_GENERATION_WINDOW > 1u,
    "system_context generation window must provide at least one active generation");

static void reset_runtime_state(system_context_runtime_t *runtime)
{
    wash_program_init(&runtime->wash_program, "", "");
    vehicle_type_init(&runtime->vehicle_type, "", "");
    wash_session_reset(&runtime->wash_session);
    wash_execution_reset(&runtime->wash_execution);
    wait_condition_reset(&runtime->wait_condition);
    program_snapshot_reset(&runtime->program_snapshot);
    memset(&runtime->runtime_snapshot, 0, sizeof(runtime->runtime_snapshot));
    memset(&runtime->last_transition_record, 0, sizeof(runtime->last_transition_record));
    memset(&runtime->pending_triggers, 0, sizeof(runtime->pending_triggers));
    runtime->pending_trigger_count = 0u;
    runtime->current_time_ms = 0ul;
    runtime->next_session_sequence = 0ul;
    runtime->next_execution_sequence = 0ul;
    runtime->next_wait_condition_sequence = 0ul;
    runtime->global_fault_present = false;
    memset(runtime->global_fault_code, 0, sizeof(runtime->global_fault_code));
    memset(runtime->global_fault_reason, 0, sizeof(runtime->global_fault_reason));
    memset(runtime->last_result_code, 0, sizeof(runtime->last_result_code));
    memset(runtime->last_reason_code, 0, sizeof(runtime->last_reason_code));
}

static void initialize_runtime_object(system_context_runtime_t *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    reset_runtime_state(runtime);
}

static uintptr_t encode_handle_token(unsigned int slot_index, uintptr_t generation)
{
    return (generation << SYSTEM_CONTEXT_HANDLE_SLOT_BITS) | ((uintptr_t)slot_index + 1u);
}

static bool decode_handle_token(const system_context_t system_context,
    unsigned int *slot_index,
    unsigned int *generation)
{
    uintptr_t handle_address;
    uintptr_t pool_base_address;
    uintptr_t pool_end_address;
    uintptr_t handle_token;
    uintptr_t encoded_slot_index;
    uintptr_t offset;

    if (system_context == 0) {
        return false;
    }

    handle_address = (uintptr_t)system_context;
    pool_base_address = (uintptr_t)&s_system_context_handles[0][0];
    pool_end_address = pool_base_address + sizeof(s_system_context_handles);
    if (handle_address < pool_base_address || handle_address >= pool_end_address) {
        return false;
    }
    offset = handle_address - pool_base_address;
    if ((offset % sizeof(s_system_context_handles[0][0])) != 0u) {
        return false;
    }

    handle_token = system_context->opaque_handle_token;
    encoded_slot_index = handle_token & SYSTEM_CONTEXT_HANDLE_SLOT_MASK;
    if (encoded_slot_index == 0u || encoded_slot_index > SYSTEM_CONTEXT_POOL_CAPACITY) {
        return false;
    }

    if (slot_index != 0) {
        *slot_index = (unsigned int)(encoded_slot_index - 1u);
    }
    if (generation != 0) {
        *generation = (unsigned int)(handle_token >> SYSTEM_CONTEXT_HANDLE_SLOT_BITS);
    }
    return (handle_token >> SYSTEM_CONTEXT_HANDLE_SLOT_BITS) != 0u;
}

static void initialize_pool_if_needed(void)
{
    unsigned int index;

    if (s_system_context_pool.initialized) {
        return;
    }

    memset(&s_system_context_pool, 0, sizeof(s_system_context_pool));
    s_system_context_pool.initialized = true;
    s_system_context_pool.free_head_index = 0u;
    s_system_context_pool.free_count = SYSTEM_CONTEXT_POOL_CAPACITY;
    for (index = 0u; index < SYSTEM_CONTEXT_POOL_CAPACITY; ++index) {
        unsigned int generation;

        s_system_context_pool.slots[index].in_use = false;
        s_system_context_pool.slots[index].next_free_index = (index + 1u < SYSTEM_CONTEXT_POOL_CAPACITY)
            ? (index + 1u)
            : SYSTEM_CONTEXT_POOL_INVALID_INDEX;
        s_system_context_pool.slots[index].generation = 0u;
        initialize_runtime_object(&s_system_context_pool.slots[index].runtime);
        for (generation = 0u; generation < SYSTEM_CONTEXT_HANDLE_GENERATION_WINDOW; ++generation) {
            s_system_context_handles[index][generation].opaque_handle_token =
                generation == 0u ? 0u : encode_handle_token(index, generation);
        }
    }
}

static system_context_pool_slot_t *slot_from_handle(const system_context_t system_context)
{
    unsigned int generation;
    unsigned int index;

    initialize_pool_if_needed();
    if (!decode_handle_token(system_context, &index, &generation)) {
        return 0;
    }
    if (index >= SYSTEM_CONTEXT_POOL_CAPACITY) {
        return 0;
    }
    if (generation != s_system_context_pool.slots[index].generation) {
        return 0;
    }
    return &s_system_context_pool.slots[index];
}

static operation_result_t require_active_slot(const system_context_t system_context, system_context_pool_slot_t **slot)
{
    unsigned int generation;
    unsigned int index;
    system_context_pool_slot_t *resolved_slot;

    initialize_pool_if_needed();
    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    if (!decode_handle_token(system_context, &index, &generation)) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }
    resolved_slot = &s_system_context_pool.slots[index];
    if (generation != resolved_slot->generation || !resolved_slot->in_use) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    if (slot != 0) {
        *slot = resolved_slot;
    }
    return operation_result_ok();
}

operation_result_t system_context_private_require_active(const system_context_t system_context)
{
    return require_active_slot(system_context, 0);
}

operation_result_t system_context_private_bind_scheduler(system_context_t system_context)
{
    operation_result_t result;
    system_context_runtime_t *runtime;

    result = require_active_slot(system_context, 0);
    if (!result.ok) {
        return result;
    }
    runtime = system_context_private_runtime(system_context);
    if (runtime->scheduler_bound) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    runtime->scheduler_bound = true;
    return operation_result_ok();
}

void system_context_private_unbind_scheduler(system_context_t system_context)
{
    system_context_runtime_t *runtime;

    if (!require_active_slot(system_context, 0).ok) {
        return;
    }

    runtime = system_context_private_runtime(system_context);
    runtime->scheduler_bound = false;
}

bool system_context_private_has_scheduler_binding(const system_context_t system_context)
{
    const system_context_runtime_t *runtime;

    if (!require_active_slot(system_context, 0).ok) {
        return false;
    }

    runtime = system_context_private_runtime(system_context);
    return runtime->scheduler_bound;
}

unsigned int system_context_private_slot_index(const system_context_t system_context)
{
    system_context_pool_slot_t *slot;

    if (!require_active_slot(system_context, &slot).ok) {
        return SYSTEM_CONTEXT_POOL_INVALID_INDEX;
    }
    return (unsigned int)(slot - &s_system_context_pool.slots[0]);
}

system_context_runtime_t *system_context_private_runtime_mutable(system_context_t system_context)
{
    system_context_pool_slot_t *slot;

    if (!require_active_slot(system_context, &slot).ok) {
        return 0;
    }
    return &slot->runtime;
}

const system_context_runtime_t *system_context_private_runtime_const(const system_context_t system_context)
{
    system_context_pool_slot_t *slot;

    if (!require_active_slot(system_context, &slot).ok) {
        return 0;
    }
    return &slot->runtime;
}

unsigned int system_context_private_debug_free_count(void)
{
    initialize_pool_if_needed();
    return s_system_context_pool.free_count;
}

unsigned int system_context_private_debug_capacity(void)
{
    return SYSTEM_CONTEXT_POOL_CAPACITY;
}

bool system_context_private_debug_is_in_use(const system_context_t system_context)
{
    system_context_pool_slot_t *slot;

    slot = slot_from_handle(system_context);
    return slot != 0 && slot->in_use;
}

operation_result_t system_context_acquire(system_context_t *system_context)
{
    system_context_pool_slot_t *slot;
    unsigned int slot_index;

    if (system_context == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    *system_context = 0;
    initialize_pool_if_needed();
    if (s_system_context_pool.free_head_index == SYSTEM_CONTEXT_POOL_INVALID_INDEX) {
        return operation_result_fail(ERROR_CODE_RESOURCE_UNAVAILABLE);
    }

    slot_index = s_system_context_pool.free_head_index;
    slot = &s_system_context_pool.slots[slot_index];
    s_system_context_pool.free_head_index = slot->next_free_index;
    s_system_context_pool.free_count -= 1u;
    slot->next_free_index = SYSTEM_CONTEXT_POOL_INVALID_INDEX;
    slot->in_use = true;
    slot->generation += 1u;
    if (slot->generation >= SYSTEM_CONTEXT_HANDLE_GENERATION_WINDOW) {
        slot->generation = 1u;
    }
    initialize_runtime_object(&slot->runtime);
    *system_context = &s_system_context_handles[slot_index][slot->generation];
    return operation_result_ok();
}

operation_result_t system_context_release(system_context_t system_context)
{
    operation_result_t result;
    system_context_pool_slot_t *slot;
    unsigned int slot_index;

    result = require_active_slot(system_context, &slot);
    if (!result.ok) {
        return result;
    }
    if (slot->runtime.scheduler_bound) {
        return operation_result_fail(ERROR_CODE_INVALID_STATE);
    }

    initialize_runtime_object(&slot->runtime);
    slot->in_use = false;
    slot_index = (unsigned int)(slot - &s_system_context_pool.slots[0]);
    slot->next_free_index = s_system_context_pool.free_head_index;
    s_system_context_pool.free_head_index = slot_index;
    s_system_context_pool.free_count += 1u;
    return operation_result_ok();
}

operation_result_t system_context_reset(system_context_t system_context)
{
    actuator_port_t actuator_port;
    event_logger_port_t event_logger_port;
    operation_result_t result;
    program_repository_port_t program_repository_port;
    bool scheduler_bound;
    sensor_port_t sensor_port;
    system_context_runtime_t *runtime;

    result = require_active_slot(system_context, 0);
    if (!result.ok) {
        return result;
    }

    runtime = system_context_private_runtime(system_context);
    sensor_port = runtime->sensor_port;
    actuator_port = runtime->actuator_port;
    event_logger_port = runtime->event_logger_port;
    program_repository_port = runtime->program_repository_port;
    scheduler_bound = runtime->scheduler_bound;

    initialize_runtime_object(runtime);
    runtime->scheduler_bound = scheduler_bound;
    runtime->sensor_port = sensor_port;
    runtime->actuator_port = actuator_port;
    runtime->event_logger_port = event_logger_port;
    runtime->program_repository_port = program_repository_port;
    return operation_result_ok();
}

void system_context_set_sensor_port(system_context_t system_context, const sensor_port_t *sensor_port)
{
    system_context_runtime_t *runtime;

    if (!system_context_private_require_active(system_context).ok) {
        return;
    }
    runtime = system_context_private_runtime(system_context);

    if (sensor_port == 0) {
        memset(&runtime->sensor_port, 0, sizeof(runtime->sensor_port));
        return;
    }

    runtime->sensor_port = *sensor_port;
}

void system_context_set_actuator_port(system_context_t system_context, const actuator_port_t *actuator_port)
{
    system_context_runtime_t *runtime;

    if (!system_context_private_require_active(system_context).ok) {
        return;
    }
    runtime = system_context_private_runtime(system_context);

    if (actuator_port == 0) {
        memset(&runtime->actuator_port, 0, sizeof(runtime->actuator_port));
        return;
    }

    runtime->actuator_port = *actuator_port;
}

void system_context_set_event_logger_port(system_context_t system_context, const event_logger_port_t *event_logger_port)
{
    system_context_runtime_t *runtime;

    if (!system_context_private_require_active(system_context).ok) {
        return;
    }
    runtime = system_context_private_runtime(system_context);

    if (event_logger_port == 0) {
        memset(&runtime->event_logger_port, 0, sizeof(runtime->event_logger_port));
        return;
    }

    runtime->event_logger_port = *event_logger_port;
}

void system_context_set_program_repository_port(system_context_t system_context,
    const program_repository_port_t *program_repository_port)
{
    system_context_runtime_t *runtime;

    if (!system_context_private_require_active(system_context).ok) {
        return;
    }
    runtime = system_context_private_runtime(system_context);

    if (program_repository_port == 0) {
        memset(&runtime->program_repository_port, 0, sizeof(runtime->program_repository_port));
        return;
    }

    runtime->program_repository_port = *program_repository_port;
}

const program_repository_port_t *system_context_program_repository_port(const system_context_t system_context)
{
    if (!system_context_private_require_active(system_context).ok) {
        return 0;
    }
    return &system_context_private_runtime(system_context)->program_repository_port;
}

unsigned long system_context_current_time_ms(const system_context_t system_context)
{
    if (!system_context_private_require_active(system_context).ok) {
        return 0ul;
    }
    return system_context_private_runtime(system_context)->current_time_ms;
}

unsigned int system_context_pending_trigger_count(const system_context_t system_context)
{
    if (!system_context_private_require_active(system_context).ok) {
        return 0u;
    }
    return system_context_private_runtime(system_context)->pending_trigger_count;
}

unsigned int system_context_count_pending_triggers_by_id(const system_context_t system_context, const char *trigger_id)
{
    unsigned int count;
    unsigned int index;
    const system_context_runtime_t *runtime;

    if (!system_context_private_require_active(system_context).ok || trigger_id == 0 || trigger_id[0] == '\0') {
        return 0u;
    }

    runtime = system_context_private_runtime(system_context);
    count = 0u;
    for (index = 0u; index < runtime->pending_trigger_count; ++index) {
        if (strcmp(runtime->pending_triggers[index].trigger_id, trigger_id) == 0) {
            count += 1u;
        }
    }
    return count;
}

unsigned int system_context_count_pending_triggers_by_type(const system_context_t system_context, trigger_type_t trigger_type)
{
    unsigned int count;
    unsigned int index;
    const system_context_runtime_t *runtime;

    if (!system_context_private_require_active(system_context).ok) {
        return 0u;
    }

    runtime = system_context_private_runtime(system_context);
    count = 0u;
    for (index = 0u; index < runtime->pending_trigger_count; ++index) {
        if (runtime->pending_triggers[index].trigger_type == trigger_type) {
            count += 1u;
        }
    }
    return count;
}

bool system_context_has_pending_runtime_work(const system_context_t system_context)
{
    if (!system_context_private_require_active(system_context).ok) {
        return false;
    }

    return system_context_private_runtime(system_context)->pending_trigger_count > 0u
        || wait_timeout_service_should_fire(&system_context_private_runtime(system_context)->wait_condition,
            system_context_private_runtime(system_context)->current_time_ms);
}

bool system_context_wait_condition_active(const system_context_t system_context)
{
    if (!system_context_private_require_active(system_context).ok) {
        return false;
    }
    return system_context_private_runtime(system_context)->wait_condition.active;
}

unsigned long system_context_wait_timeout_ms(const system_context_t system_context)
{
    const system_context_runtime_t *runtime;

    if (!system_context_private_require_active(system_context).ok) {
        return 0ul;
    }
    runtime = system_context_private_runtime(system_context);
    if (!runtime->wait_condition.active) {
        return 0ul;
    }
    if (runtime->current_time_ms >= runtime->wait_condition.timeout_at_ms) {
        return 0ul;
    }

    return runtime->wait_condition.timeout_at_ms - runtime->current_time_ms;
}

const char *system_context_last_result_code(const system_context_t system_context)
{
    if (!system_context_private_require_active(system_context).ok) {
        return "";
    }
    return system_context_private_runtime(system_context)->last_result_code;
}

const char *system_context_last_reason_code(const system_context_t system_context)
{
    if (!system_context_private_require_active(system_context).ok) {
        return "";
    }
    return system_context_private_runtime(system_context)->last_reason_code;
}
