#ifndef TESTS_STUBS_MCC_MOTOR_CONTROL_CORE_FAKE_H
#define TESTS_STUBS_MCC_MOTOR_CONTROL_CORE_FAKE_H

#include "adapters/outbound/hal/providers/mcc/motor_executor.h"

typedef enum {
    MCC_FAKE_CALL_NONE = 0,
    MCC_FAKE_CALL_RUN_CONTINUOUS,
    MCC_FAKE_CALL_MOVE_TO,
    MCC_FAKE_CALL_STOP,
    MCC_FAKE_CALL_SET_SPEED,
    MCC_FAKE_CALL_HOME,
    MCC_FAKE_CALL_RECOVER
} mcc_fake_call_t;

typedef struct {
    mcc_fake_call_t       call;
    motor_executor_t     *exec;
    int                   motor;
    motor_speed_t         speed;
    motor_direction_t     dir;
    motor_move_spec_t     spec;
    bool                  has_spec;
    motor_recovery_step_t recovery_step;
} mcc_fake_last_call_t;

/** 伪造事件队列容量（够覆盖单条测试内的连续事件即可）*/
#define MCC_FAKE_EVENT_CAP 8U

void mcc_fake_reset(void);
void mcc_fake_set_cmd_result(motor_cmd_status_t status, const char *reason);
void mcc_fake_set_query(motor_phase_t phase, int64_t position, motor_direction_t dir, motor_fault_code_t fault);
const mcc_fake_last_call_t *mcc_fake_last_call(void);

/**
 * @brief 向伪造事件队列压入一条事件，供 motor_pop_event 取出
 * @param ev 待压入的事件；队列已满时丢弃
 */
void mcc_fake_push_event(const motor_event_t *ev);

#endif /* TESTS_STUBS_MCC_MOTOR_CONTROL_CORE_FAKE_H */
