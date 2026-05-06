#ifndef APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H
#define APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H

#include "domain/model/vehicle_type.h"
#include "domain/model/program_snapshot.h"
#include "domain/model/state_transition_record.h"
#include "domain/model/wait_condition.h"
#include "domain/model/wash_cycle.h"
#include "domain/model/wash_execution.h"
#include "domain/model/wash_program.h"
#include "domain/model/wash_session.h"
#include "domain/model/wash_trigger_event.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/event_logger_port.h"
#include "domain/ports/program_repository_port.h"
#include "domain/ports/sensor_port.h"
#include "shared/timeouts.h"

typedef struct system_context_t {
    wash_program_t wash_program;
    vehicle_type_t vehicle_type;
    wash_cycle_t wash_cycle;
    wash_session_t wash_session;
    wash_execution_t wash_execution;
    wait_condition_t wait_condition;
    program_snapshot_t program_snapshot;
    state_transition_record_t last_transition_record;
    wash_trigger_event_t pending_triggers[MAX_PENDING_TRIGGER_COUNT];
    unsigned int pending_trigger_count;
    unsigned long current_time_ms;
    unsigned long next_session_sequence;
    unsigned long next_execution_sequence;
    unsigned long next_wait_condition_sequence;
    sensor_port_t sensor_port;
    actuator_port_t actuator_port;
    event_logger_port_t event_logger_port;
    program_repository_port_t program_repository_port;
} system_context_t;

#endif
