/**
 * @file    motor_internal.h
 * @brief   motor 模块内部共享声明
 * @author  HUWANGWEI
 * @date    2026-04-14
 *
 * @note    仅供 motor*.c 包含。跨文件共享的运行时上下文、常量与内部 API 均声明于此。
 *          带 _locked 后缀的接口要求调用方已持有 s_mutex。
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

/* -------------------------------------------------------------------------
 * 常量
 * ------------------------------------------------------------------------- */

/** tick 线程周期（ms） */
#define MOTOR_TICK_PERIOD_MS               10U
/** 编码器 HW 清零最大重试次数 */
#define MOTOR_ENCODER_CLEAR_RETRY_MAX      3U
/** 编码器 HW 清零重试间隔（us） */
#define MOTOR_ENCODER_CLEAR_RETRY_DELAY_US 2000U
/** 驱动器硬件未运行故障确认时间（ms） */
#define MOTOR_MON_NOT_RUNNING_CONFIRM_MS   3000U

/* -------------------------------------------------------------------------
 * 启动请求与运行时上下文
 * ------------------------------------------------------------------------- */

/**
 * @brief  电机启动请求
 * @note   频率直启、挡位直启、PENDING 到期启动三条路径统一使用。
 */
typedef struct
{
    bool          is_gear;   /**< true=挡位模式，false=频率模式 */
    int           freq_ref;  /**< 频率模式速度参考；挡位模式恒为 0 */
    int8_t        gear;      /**< 挡位模式：正=正转，负=反转，abs=挡位号 */
    motor_state_t target;    /**< 目标状态：MOTOR_STATE_HOLD / MOTOR_STATE_MOVE */
} motor_start_req_t;

/**
 * @brief  单台电机运行时上下文
 */
typedef struct
{
    motor_state_t state;              /**< 当前状态机状态 */
    int           run_cmd;            /**< 当前有效指令：频率模式为 Hz 参考；挡位模式为带符号挡位 */
    int           last_hal_output;    /**< 最近一次下发 HAL 的值（频率或挡位，调试用） */
    int           run_dir;            /**< 编码器计数方向：+1 正转，-1 反转，0 停止 */
    uint32_t      start_ms;           /**< 本次运行起始时刻（ms） */
    uint32_t      last_output_off_ms; /**< 最近一次 VFD 输出归零时刻；0 表示从未停过 */

    int32_t       encoder_pos;              /**< 软件累计编码器位置 */
    uint32_t      encoder_hw_last;          /**< 上次 HW 脉冲读数 */
    bool          encoder_hw_last_valid;    /**< encoder_hw_last 是否有效 */
    bool          encoder_counting;         /**< 是否处于编码器计数阶段 */

    bool          clear_retry_pending;      /**< HW 清零失败，下一 tick 重试 */

    int32_t       encoder_check_snapshot;   /**< 编码器异常检测快照位置 */
    uint32_t      encoder_check_ms;         /**< 上次编码器异常检测时刻 */
    uint8_t       encoder_no_change_cnt;    /**< 连续无变化计数 */
    bool          encoder_err_reported;     /**< 是否已上报编码器异常 */

    uint16_t      vfd_load_current;         /**< VFD 负载电流缓存（0.1A） */
    uint32_t      current_fault_accum_ms;   /**< 过/欠流异常累计时间（ms） */
    uint32_t      vfd_not_running_accum_ms; /**< VFD 硬件未运行累计时间（ms） */

    motor_start_req_t pending_start;        /**< PENDING 态排队中的启动请求 */
} motor_ctx_t;

/**
 * @brief  共享驱动器监测通道（0=NONE，1..HAL_VFD_ID_MAX 对应各 vfd_id+1）
 * @note   同一驱动器实例的多台电机关联到同一 channel，tick 每拍只采样一次。
 *         通道值由 motor_mon_ch_of() 从 motor_cfg_t.vfd_id 推导，不硬编码机型名称。
 */
typedef enum
{
    MOTOR_MON_CH_NONE = 0,                    /**< 不参与驱动器监测 */
    MOTOR_MON_CH_MAX  = (HAL_VFD_ID_MAX + 1), /**< 槽位总数 */
} motor_mon_ch_t;

/**
 * @brief  单驱动器通道的 tick 采样任务
 */
typedef struct
{
    bool     used;            /**< 本 tick 是否需要采样该通道 */
    int      proxy_motor_id;  /**< 代表电机 ID（用于 HAL read_current / read_status） */
    bool     need_current;    /**< 任一关联电机配置了电流阈值时为 true */
    bool     current_valid;   /**< 本 tick 电流读取是否成功 */
    bool     hw_running_valid; /**< 本 tick 驱动器运行状态读取是否成功 */
    uint16_t current;         /**< 采样到的电流值 */
    bool     hw_is_running;   /**< 驱动器是否确认在运行（hw_running_valid 为 true 时有效） */
} motor_monitor_job_t;

/**
 * @brief  编码器 HW IO 任务（在 tick 无锁区执行）
 */
typedef struct
{
    bool     do_read;                     /**< 本 tick 是否读取 HW 脉冲 */
    sw_err_t read_ret;                    /**< 读取结果：SW_OK / SW_ERR_COMM / 其它 */
    uint32_t read_value;                  /**< 读取到的 HW 脉冲值 */
    bool     do_clear;                    /**< 本 tick 是否执行 HW 清零 */
    bool     encoder_board_offline;       /**< 编码器板通信离线 */
    sw_err_t clear_ret;                   /**< 清零结果 */
    bool     hw_pulse_after_clear_valid;  /**< 清零后回读是否成功 */
    uint32_t hw_pulse_after_clear;        /**< 清零后 HW 脉冲基准值 */
} encoder_hw_job_t;

/* -------------------------------------------------------------------------
 * 模块共享全局变量
 * ------------------------------------------------------------------------- */

extern pthread_mutex_t    s_mutex;                    /**< 模块互斥锁 */
extern motor_ctx_t        s_ctx[MOTOR_ID_MAX];        /**< 各电机运行时上下文 */
extern bool               s_initialized;              /**< 模块是否已完成 motor_init */

extern motor_pre_start_cb_t s_pre_start_fns[MOTOR_ID_MAX]; /**< pre_start 回调表 */
extern void              *s_pre_start_ctxs[MOTOR_ID_MAX];  /**< pre_start 用户上下文表 */

/* -------------------------------------------------------------------------
 * 配置与状态查询（持锁调用）
 * ------------------------------------------------------------------------- */

/**
 * @brief  获取电机配置（持锁）
 * @param  id  电机 ID
 * @return 配置指针；ID 非法时返回 NULL
 */
const motor_cfg_t *motor_get_cfg_locked(int id);

/**
 * @brief  判断是否为 MOVE 状态
 * @param  state  待判断状态
 */
bool motor_is_move_state(motor_state_t state);

/**
 * @brief  判断是否为运行态（HOLD 或 MOVE）
 * @param  state  待判断状态
 */
bool motor_is_running_state(motor_state_t state);

/**
 * @brief  获取电机对应的驱动器监测通道
 * @param  cfg  电机配置
 * @return 监测通道；无驱动器或未映射时返回 MOTOR_MON_CH_NONE
 */
motor_mon_ch_t motor_mon_ch_of(const motor_cfg_t *cfg);

/**
 * @brief  计算本次运行已持续时长（持锁）
 * @param  ctx     电机上下文
 * @param  now_ms  当前时刻（ms）
 * @return 已运行毫秒数；未运行或 start_ms 为 0 时返回 0
 */
uint32_t motor_calc_elapsed_ms_locked(const motor_ctx_t *ctx, uint32_t now_ms);

/**
 * @brief  判断电机是否需要编码器 HW IO
 * @param  cfg  电机配置
 * @return true 表示 backend 为 MOTOR_ENCODER_COUNTER
 */
bool motor_needs_encoder_hw_io(const motor_cfg_t *cfg);

/* -------------------------------------------------------------------------
 * 状态机与故障（持锁调用）
 * ------------------------------------------------------------------------- */

/**
 * @brief  清零运行期 VFD 监测累计量
 * @param  ctx  电机上下文
 * @note   重置电流/状态异常计时与 vfd_load_current 缓存。
 */
void motor_reset_vfd_runtime_stats_locked(motor_ctx_t *ctx);

/**
 * @brief  进入故障态并切断输出
 * @param  id   电机 ID
 * @param  cfg  电机配置
 * @param  ops  HAL 操作表
 */
void motor_enter_fault_locked(int id,
                              const motor_cfg_t *cfg,
                              const hal_motor_ops_t *ops);

/**
 * @brief  停止电机并判断是否需通知上层完成
 * @param  id      电机 ID
 * @param  result  完成结果码
 * @return true 表示需由 tick 在锁外触发 motor_done_cb_t
 */
bool motor_stop_and_mark_done_locked(int id, sw_err_t result);

/**
 * @brief  触发已注册的动作完成回调
 * @param  id      电机 ID
 * @param  result  完成结果码
 * @note   必须在 s_mutex 释放后调用。
 */
void motor_notify_done(int id, sw_err_t result);

/**
 * @brief  从 PENDING 状态真正启动电机
 * @param  id      电机 ID
 * @param  now_ms  当前时间戳（ms）
 * @return SW_OK 成功；SW_ERR_STATE 非 PENDING 态
 * @note   tick 在 pre_start 回调之后调用；启动参数取自 ctx->pending_start。
 */
sw_err_t motor_apply_pending_start_locked(int id, uint32_t now_ms);

/* -------------------------------------------------------------------------
 * 编码器子模块
 * ------------------------------------------------------------------------- */

/**
 * @brief  更新编码器软状态（计数启停判定）
 * @param  cfg     电机配置
 * @param  ctx     电机上下文
 * @param  now_ms  当前时刻（ms）
 */
void motor_encoder_update_locked(const motor_cfg_t *cfg,
                                 motor_ctx_t *ctx,
                                 uint32_t now_ms);

/**
 * @brief  登记本 tick 编码器 HW 任务
 * @param  jobs  各电机 HW 任务数组
 * @param  ops   HAL 操作表
 * @param  id    电机 ID
 * @param  cfg   电机配置
 * @param  ctx   电机上下文
 */
void motor_encoder_schedule_hw_job_locked(encoder_hw_job_t jobs[MOTOR_ID_MAX],
                                          int id,
                                          const motor_cfg_t *cfg,
                                          const motor_ctx_t *ctx);

/**
 * @brief  执行编码器 HW IO（无锁）
 * @param  ops  HAL 操作表
 * @param  id   电机 ID
 * @param  job  HW 任务（输入 do_read/do_clear，输出读写结果）
 */
void motor_encoder_execute_hw_job(const hal_motor_ops_t *ops,
                                  int id,
                                  encoder_hw_job_t *job);

/**
 * @brief  写回编码器 HW 清零结果
 * @param  cfg  电机配置
 * @param  ctx  电机上下文
 * @param  job  已执行的 HW 任务
 */
void motor_encoder_apply_clear_result_locked(const motor_cfg_t *cfg,
                                             motor_ctx_t *ctx,
                                             const encoder_hw_job_t *job);

/**
 * @brief  写回编码器 HW 采样结果并做异常检测
 * @param  cfg     电机配置
 * @param  ctx     电机上下文
 * @param  now_ms  当前时刻（ms）
 * @param  job     本 tick HW 任务（可为 NULL）
 */
void motor_encoder_finalize_locked(const motor_cfg_t *cfg,
                                   motor_ctx_t *ctx,
                                   uint32_t now_ms,
                                   const encoder_hw_job_t *job);

/* -------------------------------------------------------------------------
 * 驱动器监测子模块
 * ------------------------------------------------------------------------- */

/**
 * @brief  登记本 tick 驱动器监测采样任务
 * @param  jobs  各驱动器通道任务数组
 * @param  cfg   电机配置
 * @param  ctx   电机上下文
 */
void motor_monitor_schedule_job_locked(motor_monitor_job_t jobs[MOTOR_MON_CH_MAX],
                                       const motor_cfg_t *cfg,
                                       const motor_ctx_t *ctx);

/**
 * @brief  执行驱动器电流/运行状态采样（无锁）
 * @param  jobs  各驱动器通道任务数组
 * @param  ops   HAL 操作表
 */
void motor_monitor_collect_samples(motor_monitor_job_t jobs[MOTOR_MON_CH_MAX],
                                   const hal_motor_ops_t *ops);

/**
 * @brief  应用驱动器监测结果并传播故障态
 * @param  jobs  各驱动器通道任务数组（含采样结果）
 * @param  ops   HAL 操作表
 */
void motor_monitor_apply_faults_locked(const motor_monitor_job_t jobs[MOTOR_MON_CH_MAX],
                                       const hal_motor_ops_t *ops);

#endif /* DOMAIN_DEVICE_MOTOR_INTERNAL_H */
