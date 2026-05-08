#ifndef DOMAIN_SERVICES_WASH_EXECUTION_SERVICE_H
#define DOMAIN_SERVICES_WASH_EXECUTION_SERVICE_H

#include <stdbool.h>

#include "domain/model/program_snapshot.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_session.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/sensor_port.h"
#include "shared/result_types.h"

/**
 * @file wash_execution_service.h
 * @brief 定义工步段执行服务。
 */
typedef struct wash_execution_fact_t {
    bool changed;
    char execution_id[32];
    char previous_state[16];
    char current_state[16];
    char result_code[32];
    char reason_code[64];
    char segment_id[32];
} wash_execution_fact_t;

typedef struct wash_execution_service_args_t {
    wash_execution_t *wash_execution;
    wash_session_t *wash_session;
    wait_condition_t *wait_condition;
    const program_snapshot_t *program_snapshot;
    actuator_port_t *actuator_port;
    sensor_port_t *sensor_port;
    unsigned long *next_execution_sequence;
    unsigned long *next_wait_condition_sequence;
    unsigned long current_time_ms;
} wash_execution_service_args_t;

operation_result_t wash_execution_service_begin_next_segment(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact);
operation_result_t wash_execution_service_tick(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact);
operation_result_t wash_execution_service_handle_stop(wash_execution_service_args_t *wash_execution_service_args,
    const char *reason_code,
    wash_execution_fact_t *wash_execution_fact);
operation_result_t wash_execution_service_handle_fault(wash_execution_service_args_t *wash_execution_service_args,
    const char *reason_code,
    wash_execution_fact_t *wash_execution_fact);
operation_result_t wash_execution_service_handle_timeout(wash_execution_service_args_t *wash_execution_service_args,
    wash_execution_fact_t *wash_execution_fact);

#endif
