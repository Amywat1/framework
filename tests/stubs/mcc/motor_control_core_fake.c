#include "tests/stubs/mcc/motor_control_core_fake.h"

#include <string.h>

static mcc_fake_last_call_t s_last;
static motor_cmd_result_t   s_result;
static motor_phase_t        s_phase;
static int64_t              s_position;
static motor_direction_t    s_dir;
static motor_fault_code_t   s_fault;

/* 伪造事件队列（FIFO，供 motor_pop_event 消费）*/
static motor_event_t s_events[MCC_FAKE_EVENT_CAP];
static unsigned      s_ev_head;
static unsigned      s_ev_count;

void mcc_fake_reset(void)
{
    memset(&s_last, 0, sizeof(s_last));
    s_result.status = MOTOR_CMD_ACCEPTED;
    s_result.reason = "";
    s_phase         = MOTOR_PHASE_STOPPED;
    s_position      = 0;
    s_dir           = MOTOR_DIR_FORWARD;
    s_fault         = MOTOR_FAULT_NONE;
    memset(s_events, 0, sizeof(s_events));
    s_ev_head  = 0U;
    s_ev_count = 0U;
}

void mcc_fake_push_event(const motor_event_t *ev)
{
    if ((ev == NULL) || (s_ev_count >= MCC_FAKE_EVENT_CAP)) {
        return;
    }
    s_events[(s_ev_head + s_ev_count) % MCC_FAKE_EVENT_CAP] = *ev;
    s_ev_count++;
}

void mcc_fake_set_cmd_result(motor_cmd_status_t status, const char *reason)
{
    s_result.status = status;
    s_result.reason = reason;
}

void mcc_fake_set_query(motor_phase_t phase, int64_t position, motor_direction_t dir, motor_fault_code_t fault)
{
    s_phase    = phase;
    s_position = position;
    s_dir      = dir;
    s_fault    = fault;
}

const mcc_fake_last_call_t *mcc_fake_last_call(void)
{
    return &s_last;
}

motor_cmd_result_t motor_run_continuous(motor_executor_t *exec, int motor, motor_speed_t spd, motor_direction_t dir)
{
    s_last.call  = MCC_FAKE_CALL_RUN_CONTINUOUS;
    s_last.exec  = exec;
    s_last.motor = motor;
    s_last.speed = spd;
    s_last.dir   = dir;
    return s_result;
}

motor_cmd_result_t motor_move_to(motor_executor_t        *exec,
                                 int                      motor,
                                 motor_speed_t            spd,
                                 motor_direction_t        dir,
                                 const motor_move_spec_t *spec)
{
    s_last.call     = MCC_FAKE_CALL_MOVE_TO;
    s_last.exec     = exec;
    s_last.motor    = motor;
    s_last.speed    = spd;
    s_last.dir      = dir;
    s_last.has_spec = (spec != NULL);
    if (spec != NULL) {
        s_last.spec = *spec;
    }
    return s_result;
}

motor_cmd_result_t motor_stop(motor_executor_t *exec, int motor)
{
    s_last.call  = MCC_FAKE_CALL_STOP;
    s_last.exec  = exec;
    s_last.motor = motor;
    return s_result;
}

motor_cmd_result_t motor_set_speed(motor_executor_t *exec, int motor, motor_speed_t spd, motor_direction_t dir)
{
    s_last.call  = MCC_FAKE_CALL_SET_SPEED;
    s_last.exec  = exec;
    s_last.motor = motor;
    s_last.speed = spd;
    s_last.dir   = dir;
    return s_result;
}

motor_cmd_result_t motor_home(motor_executor_t *exec, int motor)
{
    s_last.call  = MCC_FAKE_CALL_HOME;
    s_last.exec  = exec;
    s_last.motor = motor;
    return s_result;
}

motor_cmd_result_t motor_recover(motor_executor_t *exec, int motor, motor_recovery_step_t step)
{
    s_last.call          = MCC_FAKE_CALL_RECOVER;
    s_last.exec          = exec;
    s_last.motor         = motor;
    s_last.recovery_step = step;
    return s_result;
}

motor_phase_t motor_phase(const motor_executor_t *exec, int motor)
{
    (void)exec;
    (void)motor;
    return s_phase;
}

int64_t motor_position(const motor_executor_t *exec, int motor)
{
    (void)exec;
    (void)motor;
    return s_position;
}

motor_direction_t motor_direction(const motor_executor_t *exec, int motor)
{
    (void)exec;
    (void)motor;
    return s_dir;
}

motor_fault_code_t motor_fault_code(const motor_executor_t *exec, int motor)
{
    (void)exec;
    (void)motor;
    return s_fault;
}

bool motor_pop_event(motor_executor_t *exec, motor_event_t *out)
{
    (void)exec;

    if ((out == NULL) || (s_ev_count == 0U)) {
        return false;
    }
    *out      = s_events[s_ev_head];
    s_ev_head = (s_ev_head + 1U) % MCC_FAKE_EVENT_CAP;
    s_ev_count--;
    return true;
}
