/**
 * @file    hal_motor_executor_internal.h
 * @brief   电机执行器内部类型与跨编译单元接口
 *
 * @note    仅供 hal_motor_executor*.c 互调，不对外包含。
 */

#ifndef ADAPTERS_OUTBOUND_HAL_COMPONENTS_MOTOR_EXEC_HAL_MOTOR_EXECUTOR_INTERNAL_H
#define ADAPTERS_OUTBOUND_HAL_COMPONENTS_MOTOR_EXEC_HAL_MOTOR_EXECUTOR_INTERNAL_H

#include "adapters/outbound/hal/components/motor_exec/hal_motor_executor.h"

#define MOTOR_EVENT_QUEUE_CAP 64

typedef struct {
    bool                  is_move;
    hal_motor_speed_t     speed;
    hal_motor_dir_t       dir;
    hal_motor_move_spec_t spec;
} motor_pending_cmd_t;

typedef struct {
    hal_motor_phase_t phase;
    hal_motor_dir_t   dir;

    hal_motor_speed_t speed;
    hal_motor_speed_t applied_speed;
    bool              output_applied;

    bool                   move_active;
    hal_motor_move_spec_t  spec;
    hal_motor_limit_kind_t end_limit;
    uint64_t               move_start_ms;
    uint64_t               elapsed_ms;
    bool                   emit_stop_on_halt;

    int64_t position;
    int64_t last_raw;
    bool    baseline_trusted;

    uint64_t            cooldown_until;
    bool                queued;
    motor_pending_cmd_t pending;

    uint64_t            reversal_until;
    motor_pending_cmd_t after_reversal;

    bool                   fatal;
    hal_motor_fault_code_t fault_code;
    bool                   driver_reset_done;

    uint64_t start_ms;
    int      cur_over_ms;
    int      cur_under_ms;
    uint32_t cur_stop_ms;
    int      fb_bad_ms;
    int      fb_strikes;
    int      temp_bad_ms;
    int      volt_bad_ms;

    int  enc_stall;
    bool enc_warned;
    bool enc_healthy;
} motor_mstate_t;

typedef struct {
    motor_config_t cfg;
    motor_ports_t  ports;
    motor_mstate_t m[MOTOR_MAX_MOTORS];
    int            motor_count;

    uint64_t now;
    uint64_t last_tick_ms;
    bool     last_tick_valid;
    bool     estop_latched;
    bool     safe_latched;

    hal_motor_event_t events[MOTOR_EVENT_QUEUE_CAP];
    int               ev_head;
    int               ev_count;

    motor_event_cb_t cb;
    void            *cb_ctx;
    bool             in_dispatch;

} motor_executor_t;

int64_t motor_iabs64(int64_t v);
bool within_distance(int64_t left, int64_t right, int distance);
int64_t lower_bound(int64_t value, int margin);
int64_t upper_bound(int64_t value, int margin);

motor_driver_t *motor_drv(motor_executor_t *e, int i);
sw_err_t drv_set_output(motor_driver_t *d, hal_motor_speed_t speed, hal_motor_dir_t dir);
sw_err_t drv_cutoff(motor_driver_t *d);
bool drv_reset(motor_driver_t *d);
motor_prepare_result_t drv_prepare(motor_driver_t *d, int motor);
motor_prepare_result_t drv_poll(motor_driver_t *d, int motor);
bool drv_is_running(motor_driver_t *d);
int drv_current(motor_driver_t *d);
bool drv_temperature(motor_driver_t *d, int *out);
bool drv_voltage(motor_driver_t *d, int *out);
motor_port_status_t drv_status(motor_driver_t *d);
int64_t enc_raw(motor_encoder_t *e);
bool enc_zero(motor_encoder_t *e);
motor_encoder_t *motor_enc(motor_executor_t *e, int i);
bool sensor_limit(motor_executor_t *e, int i, hal_motor_limit_kind_t k);
bool estop_active(motor_executor_t *e);
uint64_t clock_now(motor_executor_t *e);

hal_motor_cmd_result_t cmd_make(hal_motor_cmd_status_t st, const char *reason);
hal_motor_cmd_result_t cmd_reject(const char *reason);

void settle_elapsed(motor_executor_t *e, int i);
void push_event(motor_executor_t         *e,
                int                       i,
                hal_motor_event_type_t    t,
                hal_motor_end_condition_t trig,
                hal_motor_fault_code_t    fc);
void motor_dispatch(motor_executor_t *e);

hal_motor_cmd_result_t apply_goal(motor_executor_t *e, int i, const motor_pending_cmd_t *pc);
void complete_move(motor_executor_t *e, int i, hal_motor_event_type_t type, hal_motor_end_condition_t trig);
bool zero_encoder_baseline(motor_executor_t *e, int i);
void motor_tick(motor_executor_t *e);
motor_init_result_t init_err(const char *msg);
motor_init_result_t motor_init(motor_executor_t *e, const motor_config_t *cfg, const motor_ports_t *ports);
motor_init_result_t motor_reinit(motor_executor_t *e);

hal_motor_cmd_result_t motor_run(motor_executor_t            *e,
                                 int                          i,
                                 hal_motor_speed_t            spd,
                                 hal_motor_dir_t              dir,
                                 const hal_motor_move_spec_t *spec);
hal_motor_cmd_result_t motor_stop(motor_executor_t *e, int i);
hal_motor_cmd_result_t motor_home(motor_executor_t *e, int i);
hal_motor_cmd_result_t motor_zero_encoder(motor_executor_t *e, int i);
hal_motor_cmd_result_t motor_confirm_baseline(motor_executor_t *e, int i);
void motor_reset_estop(motor_executor_t *e);
void motor_reset_watchdog(motor_executor_t *e);
hal_motor_cmd_result_t motor_recover(motor_executor_t *e, int i, hal_motor_recovery_step_t step);

hal_motor_phase_t motor_phase(const motor_executor_t *e, int i);
int64_t motor_position(const motor_executor_t *e, int i);
int motor_current_freq(const motor_executor_t *e, int i);
hal_motor_dir_t motor_direction(const motor_executor_t *e, int i);
hal_motor_fault_code_t motor_fault_code(const motor_executor_t *e, int i);
bool motor_baseline_trusted(const motor_executor_t *e, int i);
bool motor_encoder_healthy(const motor_executor_t *e, int i);
bool motor_in_safe_state(const motor_executor_t *e);

#endif /* ADAPTERS_OUTBOUND_HAL_COMPONENTS_MOTOR_EXEC_HAL_MOTOR_EXECUTOR_INTERNAL_H */
