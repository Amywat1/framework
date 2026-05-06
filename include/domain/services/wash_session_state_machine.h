#ifndef DOMAIN_SERVICES_WASH_SESSION_STATE_MACHINE_H
#define DOMAIN_SERVICES_WASH_SESSION_STATE_MACHINE_H

#include "application/coordinators/system_context.h"
#include "shared/result_types.h"

/**
 * @file wash_session_state_machine.h
 * @brief 定义会话状态迁移规则。
 */
operation_result_t wash_session_state_machine_start(system_context_t *system_context, const char *program_id);
operation_result_t wash_session_state_machine_complete(system_context_t *system_context, result_code_t result_code, trigger_type_t trigger_type);
operation_result_t wash_session_state_machine_abort(system_context_t *system_context, result_code_t result_code, const char *reason_code, trigger_type_t trigger_type);

#endif
