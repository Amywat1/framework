#include "application/coordinators/wash_cycle_coordinator.h"

#include <stdio.h>
#include <string.h>

#include "domain/model/program_validation.h"
#include "domain/services/brush_control_state_machine.h"
#include "domain/services/chemical_dosing_state_machine.h"
#include "domain/services/gantry_motion_state_machine.h"
#include "domain/services/precheck_service.h"

static void log_cycle_message(system_context_t *system_context, event_type_t event_type, const char *message)
{
    if (system_context != 0 && system_context->event_logger_port.log_message != 0) {
        system_context->event_logger_port.log_message(system_context->event_logger_port.context, event_type, message);
    }
}

operation_result_t wash_cycle_coordinator_run(system_context_t *system_context, const char *program_id)
{
    int index;
    operation_result_t result;

    if (system_context == 0 || program_id == 0) {
        return operation_result_fail(ERROR_CODE_INVALID_ARGUMENT);
    }

    if (system_context->program_repository_port.load_program(system_context->program_repository_port.context, program_id, &system_context->wash_program) != 0) {
        return operation_result_fail(ERROR_CODE_IO_FAILED);
    }

    result = program_validation_validate(&system_context->wash_program);
    if (!result.ok) {
        return result;
    }

    wash_cycle_init(&system_context->wash_cycle, "cycle-001", program_id);
    wash_cycle_set_state(&system_context->wash_cycle, CYCLE_STATE_PRECHECK);
    log_cycle_message(system_context, EVENT_TYPE_CYCLE_STATE_CHANGED, "进入预检");

    result = precheck_service_run(&system_context->sensor_port);
    if (!result.ok) {
        wash_cycle_set_state(&system_context->wash_cycle, CYCLE_STATE_SAFE_STOP);
        system_context->wash_cycle.result_code = RESULT_CODE_START_FAILED;
        log_cycle_message(system_context, EVENT_TYPE_FAULT_RAISED, "预检失败");
        return result;
    }

    wash_cycle_set_state(&system_context->wash_cycle, CYCLE_STATE_RUNNING);
    log_cycle_message(system_context, EVENT_TYPE_CYCLE_STATE_CHANGED, "进入运行");

    for (index = 0; index < system_context->wash_program.stage_count; ++index) {
        system_context->wash_cycle.current_stage_index = index;
        result = gantry_motion_state_machine_execute(&system_context->actuator_port, &system_context->wash_program.stages[index]);
        if (!result.ok) {
            wash_cycle_set_state(&system_context->wash_cycle, CYCLE_STATE_SAFE_STOP);
            log_cycle_message(system_context, EVENT_TYPE_FAULT_RAISED, "龙门运动异常");
            return result;
        }

        result = brush_control_state_machine_execute(&system_context->actuator_port, &system_context->wash_program.stages[index]);
        if (!result.ok) {
            wash_cycle_set_state(&system_context->wash_cycle, CYCLE_STATE_SAFE_STOP);
            log_cycle_message(system_context, EVENT_TYPE_FAULT_RAISED, "刷组动作异常");
            return result;
        }

        result = chemical_dosing_state_machine_execute(&system_context->actuator_port, &system_context->wash_program.stages[index]);
        if (!result.ok) {
            wash_cycle_set_state(&system_context->wash_cycle, CYCLE_STATE_DEGRADED);
            log_cycle_message(system_context, EVENT_TYPE_FAULT_RAISED, "药剂动作异常");
            return result;
        }
    }

    wash_cycle_finish(&system_context->wash_cycle, RESULT_CODE_SUCCESS);
    log_cycle_message(system_context, EVENT_TYPE_CYCLE_FINISHED, "洗车周期完成");
    return operation_result_ok();
}

