#ifndef APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H
#define APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H

#include <stdbool.h>

#include "domain/model/vehicle_type.h"
#include "domain/model/program_snapshot.h"
#include "domain/model/state_transition_record.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_program.h"
#include "domain/model/wash_session.h"
#include "domain/model/wash_trigger_event.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/event_logger_port.h"
#include "domain/ports/program_repository_port.h"
#include "domain/ports/sensor_port.h"
#include "shared/timeouts.h"

/**
 * @file system_context.h
 * @brief 定义主控运行期共享上下文。
 */

/**
 * @brief 聚合主控运行期共享状态、端口和运行时对象。
 *
 * @note 本结构由主控进程在单个进程生命周期内长期持有。
 * @note 本期新增的全局故障字段只用于记录空闲故障、阻断启动和显式清除。
 * @note `global_fault_*` 仅允许由 `process_wash_trigger_execute()` 改写。
 * @note `last_result_code`、`last_reason_code` 与 `last_transition_record` 应通过统一记录辅助逻辑刷新。
 * @note `status` 查询只允许读取本结构中的状态快照，不得借查询动作改写最近结果或故障状态。
 */
typedef struct system_context_t {
    wash_program_t wash_program;
    vehicle_type_t vehicle_type;
    wash_session_t wash_session;
    wash_execution_t wash_execution;
    wait_condition_t wait_condition;
    program_snapshot_t program_snapshot;
    runtime_snapshot_t runtime_snapshot;
    state_transition_record_t last_transition_record;
    wash_trigger_event_t pending_triggers[MAX_PENDING_TRIGGER_COUNT];
    unsigned int pending_trigger_count;
    unsigned long current_time_ms;
    unsigned long next_session_sequence;
    unsigned long next_execution_sequence;
    unsigned long next_wait_condition_sequence;
    bool global_fault_present;
    char global_fault_code[64];
    char global_fault_reason[128];
    char last_result_code[32];
    char last_reason_code[64];
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    event_logger_port_t event_logger_port;
    program_repository_port_t program_repository_port;
} system_context_t;

#endif
