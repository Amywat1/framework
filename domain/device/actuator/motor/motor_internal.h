/**
 * @file    motor_internal.h
 * @brief   motor 模块内部共享声明
 * @author  HUWANGWEI
 * @date    2026-04-14
 */

#ifndef DOMAIN_DEVICE_MOTOR_INTERNAL_H
#define DOMAIN_DEVICE_MOTOR_INTERNAL_H

#include "domain/device/actuator/motor/motor.h"
#include "machines/m8/config/m8_motor_table.h"
#include "ports/hal/hal_motor_port.h"
#include "common/sw_error.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#define MOTOR_TICK_PERIOD_MS               10U
#define MOTOR_ENCODER_CLEAR_RETRY_MAX      3U
#define MOTOR_ENCODER_CLEAR_RETRY_DELAY_US 2000U
#define MOTOR_VFD_STATUS_RUNNING_MASK      0x0001U
#define MOTOR_VFD_STATE_CONFIRM_MS         3000U

typedef struct
{
    motor_state_t state;
    motor_state_t paused_from;
    int           speed_ref;
    int           applied_speed_ref;
    int           stored_dir;
    int32_t       target_pos;
    uint32_t      move_time_ms;
    uint32_t      start_ms;
    uint32_t      stop_timestamp_ms;
    uint32_t      pause_start_ms;
    uint32_t      paused_total_ms;

    int32_t       encoder_pos;
    uint32_t      encoder_hw_last;
    bool          encoder_hw_last_valid;
    bool          encoder_counting;

    uint8_t       zero_confirm_cnt;
    bool          zero_clear_pending;

    int32_t       encoder_check_snapshot;
    uint32_t      encoder_check_ms;
    uint8_t       encoder_no_change_cnt;
    bool          encoder_err_reported;

    uint16_t      load_current;
    uint32_t      current_anomaly_ms;
    uint32_t      state_mismatch_ms;
} motor_ctx_t;

typedef enum
{
    MOTOR_MON_SRC_NONE = 0,
    MOTOR_MON_SRC_VFD_GANTRY,
    MOTOR_MON_SRC_MAX
} motor_monitor_source_t;

typedef struct
{
    bool     used;
    int      sample_motor_id;
    bool     current_valid;
    bool     status_valid;
    uint16_t current;
    uint16_t status;
} motor_monitor_job_t;

typedef struct
{
    bool     need_read;
    sw_err_t read_ret;    /* SW_OK=成功；SW_ERR_COMM=链路离线；其它=参数/初始化错误 */
    uint32_t read_value;
    bool     need_clear;
    bool     board_offline;
    sw_err_t clear_ret;
    bool     clear_baseline_valid;
    uint32_t clear_baseline;
} encoder_hw_job_t;

extern pthread_mutex_t    s_mutex;
extern motor_ctx_t        s_ctx[MOTOR_ID_MAX];
extern bool               s_initialized;

const motor_cfg_t *motor_get_cfg_locked(int id);
bool               motor_is_move_state(motor_state_t state);
bool               motor_is_running_state(motor_state_t state);
motor_monitor_source_t motor_get_monitor_source(const motor_cfg_t *cfg);
uint32_t           motor_calc_elapsed_ms_locked(const motor_ctx_t *ctx, uint32_t now_ms);
bool               motor_needs_encoder_hw_io(const motor_cfg_t *cfg);
void               motor_enter_fault_locked(int id,
                                            const motor_cfg_t *cfg,
                                            const hal_motor_ops_t *ops);
/* 返回 true 表示需要向上层发出完成通知（由 motor_tick 在锁外调用回调）。 */
bool               motor_finish_locked(int id, sw_err_t result);
/* 在锁外调用，触发已注册的完成回调。 */
void               motor_notify_done(int id, sw_err_t result);

void               motor_encoder_update_locked(int id,
                                               const hal_motor_ops_t *ops,
                                               const motor_cfg_t *cfg,
                                               motor_ctx_t *ctx,
                                               uint32_t now_ms);
void               motor_encoder_schedule_hw_job_locked(encoder_hw_job_t jobs[MOTOR_ID_MAX],
                                                        const hal_motor_ops_t *ops,
                                                        int id,
                                                        const motor_cfg_t *cfg,
                                                        const motor_ctx_t *ctx);
void               motor_encoder_execute_hw_job(const hal_motor_ops_t *ops,
                                                const motor_cfg_t *cfg,
                                                int id,
                                                encoder_hw_job_t *job);
void               motor_encoder_apply_clear_result_locked(int id,
                                                           const motor_cfg_t *cfg,
                                                           motor_ctx_t *ctx,
                                                           const encoder_hw_job_t *job);
void               motor_encoder_finalize_locked(int id,
                                                 const hal_motor_ops_t *ops,
                                                 const motor_cfg_t *cfg,
                                                 motor_ctx_t *ctx,
                                                 uint32_t now_ms,
                                                 const encoder_hw_job_t *job);

void               motor_monitor_schedule_job_locked(motor_monitor_job_t jobs[MOTOR_MON_SRC_MAX],
                                                     const motor_cfg_t *cfg,
                                                     const motor_ctx_t *ctx);
void               motor_monitor_collect_samples(motor_monitor_job_t jobs[MOTOR_MON_SRC_MAX],
                                                 const hal_motor_ops_t *ops);
void               motor_monitor_apply_faults_locked(const motor_monitor_job_t jobs[MOTOR_MON_SRC_MAX],
                                                     const hal_motor_ops_t *ops);

#endif /* DOMAIN_DEVICE_MOTOR_INTERNAL_H */
