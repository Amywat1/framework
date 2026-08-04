/**
 * @file    hal_motor_exec_port.h
 * @brief   电机执行器出站端口。
 *
 * domain/device_control/patterns 与 projects 侧 domain/mechanism 下的机构模块只依赖
 * 本端口的不透明句柄与类型，不感知第三方电机控制 SDK（Motor Control Core, MCC）。
 * 真实执行器由项目 bindings 层（如 bindings/m8_motor_exec.c）静态分配
 * 并完成 motor_init，随后以 hal_motor_exec_t 指针形式注入各机构模块；MCC provider
 * 适配器位于 adapters/outbound/hal/providers/mcc/hal_motor_exec_adapter.c。
 */
#ifndef PORTS_OUTBOUND_HAL_MOTOR_HAL_MOTOR_EXEC_PORT_H
#define PORTS_OUTBOUND_HAL_MOTOR_HAL_MOTOR_EXEC_PORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 电机执行器不透明句柄，真实存储由项目 bindings 层持有。 */
typedef void hal_motor_exec_t;

/** @brief 运动方向。 */
typedef enum {
    HAL_MOTOR_DIR_FORWARD = 0, /**< 正向 */
    HAL_MOTOR_DIR_REVERSE = 1  /**< 反向 */
} hal_motor_dir_t;

/** @brief 限位/原点采集种类。 */
typedef enum {
    HAL_MOTOR_LIMIT_POS    = 0, /**< 正向端限位 */
    HAL_MOTOR_LIMIT_NEG    = 1, /**< 反向端限位 */
    HAL_MOTOR_LIMIT_ORIGIN = 2  /**< 原点 */
} hal_motor_limit_kind_t;

/** @brief 电机状态。 */
typedef enum {
    HAL_MOTOR_PHASE_STOPPED = 0,   /**< 停止（上电默认态、故障安全态） */
    HAL_MOTOR_PHASE_WAITING_START, /**< 等待启动（冷却/预备/互锁排队） */
    HAL_MOTOR_PHASE_REVERSAL_WAIT, /**< 换向等待 */
    HAL_MOTOR_PHASE_RUNNING,       /**< 运行中 */
    HAL_MOTOR_PHASE_PAUSED,        /**< 暂停 */
    HAL_MOTOR_PHASE_DECELERATING,  /**< 减速停止中 */
    HAL_MOTOR_PHASE_FAULT,         /**< 故障 */
    HAL_MOTOR_PHASE_ESTOP          /**< 急停 */
} hal_motor_phase_t;

/** @brief 故障码。 */
typedef enum {
    HAL_MOTOR_FAULT_NONE = 0,
    HAL_MOTOR_FAULT_OVERCURRENT,       /**< 过载 */
    HAL_MOTOR_FAULT_UNDERCURRENT,      /**< 空转/断带 */
    HAL_MOTOR_FAULT_DRIVER_FEEDBACK,   /**< 驱动器运行反馈异常 */
    HAL_MOTOR_FAULT_OVERTEMP,          /**< 过温 */
    HAL_MOTOR_FAULT_UNDERVOLTAGE,      /**< 欠压 */
    HAL_MOTOR_FAULT_PREPARE_FAILED,    /**< 预备动作失败 */
    HAL_MOTOR_FAULT_ENCODER_SIGNAL,    /**< 编码器信号质量 */
    HAL_MOTOR_FAULT_WATCHDOG,          /**< 看门狗（tick 缺拍） */
    HAL_MOTOR_FAULT_DRIVER_PORT_FATAL, /**< 端口层致命错误 */
    HAL_MOTOR_FAULT_SHARED_DRIVER      /**< 共享驱动器联动 */
} hal_motor_fault_code_t;

/** @brief 三步恢复流程步骤。 */
typedef enum {
    HAL_MOTOR_RECOVERY_DRIVER_RESET = 0, /**< 第一步：驱动器复位 */
    HAL_MOTOR_RECOVERY_MODULE_STOP       /**< 第二步：模块停止（之后可重新启动） */
} hal_motor_recovery_step_t;

/** @brief 运动结束的触发条件，说明本次运动“为什么”停下。 */
typedef enum {
    HAL_MOTOR_END_NONE = 0,   /**< 无结束条件（被显式停止或急停切断） */
    HAL_MOTOR_END_LIMIT,      /**< 触发限位开关 */
    HAL_MOTOR_END_POSITION,   /**< 到达目标位置 */
    HAL_MOTOR_END_SOFT_LIMIT, /**< 触发软限位 */
    HAL_MOTOR_END_TIME,       /**< 运行时长达到设定值 */
    HAL_MOTOR_END_TIMEOUT     /**< 超时兜底（错误结果） */
} hal_motor_end_condition_t;

/** @brief 运动结束事件类型。 */
typedef enum {
    HAL_MOTOR_EVENT_ARRIVED = 0, /**< 正常到位 */
    HAL_MOTOR_EVENT_TIMEOUT,     /**< 超时（错误结果） */
    HAL_MOTOR_EVENT_STOPPED,     /**< 被显式停止 */
    HAL_MOTOR_EVENT_FAULT,       /**< 故障 */
    HAL_MOTOR_EVENT_ESTOP,       /**< 急停 */
    HAL_MOTOR_EVENT_WARNING      /**< 告警（不切断） */
} hal_motor_event_type_t;

/** @brief 运动结束事件载荷，供上层记录状态变化原因。 */
typedef struct {
    int                       motor;      /**< 电机号 */
    hal_motor_event_type_t    type;       /**< 事件类型 */
    hal_motor_end_condition_t trigger;    /**< 触发条件 */
    bool                      has_limit;  /**< limit 是否有效（仅限位终止时为真） */
    hal_motor_limit_kind_t    limit;      /**< 触发的限位种类 */
    int64_t                   final_pos;  /**< 结束时位置（脉冲） */
    uint64_t                  elapsed_ms; /**< 本次运动耗时（ms） */
    hal_motor_fault_code_t    fault;      /**< 故障码；无故障为 HAL_MOTOR_FAULT_NONE */
} hal_motor_event_t;

/** @brief 速度指定方式。 */
typedef enum {
    HAL_MOTOR_SPEED_FREQ = 0, /**< 直接频率（厘赫） */
    HAL_MOTOR_SPEED_GEAR = 1  /**< 挡位号（1 基：1..N，0=停止） */
} hal_motor_speed_kind_t;

/** @brief 速度指定。 */
typedef struct {
    hal_motor_speed_kind_t kind;
    int                    value; /**< Freq: 厘赫；Gear: 1..N（0=停止） */
} hal_motor_speed_t;

/** @brief 构造频率速度。 */
static inline hal_motor_speed_t hal_motor_speed_freq(int centi_hz)
{
    hal_motor_speed_t s;
    s.kind  = HAL_MOTOR_SPEED_FREQ;
    s.value = centi_hz;
    return s;
}

/**
 * @brief  构造挡位速度
 * @param  gear  1 基挡位号（1..N）；0 表示停止，run/move 会拒绝
 */
static inline hal_motor_speed_t hal_motor_speed_gear(int gear)
{
    hal_motor_speed_t s;
    s.kind  = HAL_MOTOR_SPEED_GEAR;
    s.value = gear;
    return s;
}

/** @brief 运动到位的结束条件描述（可组合，超时兜底始终生效）。 */
typedef struct {
    bool                   use_limit;      /**< 启用限位到位 */
    hal_motor_limit_kind_t limit;          /**< 限位种类 */
    bool                   use_position;   /**< 启用按位置到位（需编码器与可信基准） */
    int64_t                target_pos;     /**< 目标位置（脉冲） */
    bool                   use_soft_limit; /**< 启用软限位到位 */
    bool                   use_time;       /**< 启用按时间到位 */
    uint64_t               duration_ms;    /**< 运行时长（ms） */
    uint64_t               max_time_ms;    /**< 超时兜底（0=使用配置默认） */
} hal_motor_move_spec_t;

/** @brief 命令受理状态。 */
typedef enum {
    HAL_MOTOR_CMD_ACCEPTED = 0, /**< 已受理 */
    HAL_MOTOR_CMD_QUEUED,       /**< 已排队（冷却等） */
    HAL_MOTOR_CMD_REJECTED      /**< 被拒绝 */
} hal_motor_cmd_status_t;

/** @brief 命令结果。 */
typedef struct {
    hal_motor_cmd_status_t status;
    const char            *reason; /**< 说明（静态字符串，可用于日志） */
} hal_motor_cmd_result_t;

/** @brief 判定命令是否非拒绝。 */
static inline bool hal_motor_cmd_ok(hal_motor_cmd_result_t r)
{
    return r.status != HAL_MOTOR_CMD_REJECTED;
}

/** @brief 持续运行（异步）。 */
hal_motor_cmd_result_t hal_motor_run_continuous(hal_motor_exec_t *exec,
                                                int               motor,
                                                hal_motor_speed_t spd,
                                                hal_motor_dir_t   dir);

/** @brief 运动到位（异步）。spec 描述结束条件，可组合，超时兜底始终生效。 */
hal_motor_cmd_result_t hal_motor_move_to(hal_motor_exec_t            *exec,
                                         int                          motor,
                                         hal_motor_speed_t            spd,
                                         hal_motor_dir_t              dir,
                                         const hal_motor_move_spec_t *spec);

/** @brief 减速停止（异步）。 */
hal_motor_cmd_result_t hal_motor_stop(hal_motor_exec_t *exec, int motor);

/** @brief 运行中调速/改向（改向自动走换向安全流程）。 */
hal_motor_cmd_result_t hal_motor_set_speed(hal_motor_exec_t *exec,
                                           int               motor,
                                           hal_motor_speed_t spd,
                                           hal_motor_dir_t   dir);

/** @brief 回原点：向原点运动并在触发原点后建立可信基准。 */
hal_motor_cmd_result_t hal_motor_home(hal_motor_exec_t *exec, int motor);

/** @brief 三步恢复：驱动器复位 → 模块停止（之后由调用方重新启动）。 */
hal_motor_cmd_result_t hal_motor_recover(hal_motor_exec_t *exec, int motor, hal_motor_recovery_step_t step);

/* 查询 —— 所有带 motor 参数的函数均要求 motor 在 [0, motor_count) 范围内 */

/** @brief 查询电机当前状态。 */
hal_motor_phase_t hal_motor_phase(const hal_motor_exec_t *exec, int motor);

/** @brief 查询电机累计位置（脉冲）。 */
int64_t hal_motor_position(const hal_motor_exec_t *exec, int motor);

/** @brief 查询电机当前运动方向。 */
hal_motor_dir_t hal_motor_direction(const hal_motor_exec_t *exec, int motor);

/** @brief 查询电机当前故障码；无故障时为 HAL_MOTOR_FAULT_NONE。 */
hal_motor_fault_code_t hal_motor_fault_code(const hal_motor_exec_t *exec, int motor);

/**
 * @brief  查询编码器健康状态。
 * @return true 编码器读数可信；false 已检测到停滞或跳变，尚未经归位恢复。
 * @note   无编码器的机构恒为 true。这是跨运动的持续状态，供上层作为报警条件；
 *         与"位置基准是否已建立"不同，增量编码器上电时基准未建立但编码器健康。
 */
bool hal_motor_encoder_healthy(const hal_motor_exec_t *exec, int motor);

/**
 * @brief  取出一条运动结束事件，用于记录状态变化原因。
 * @param  exec 电机执行器句柄。
 * @param  out  输出事件；仅在返回 true 时有效。
 * @return true 取出一条事件；false 队列已空。
 * @note   队列容量有限，调用方须按节拍持续排空，否则最旧事件会被覆盖丢弃。
 */
bool hal_motor_pop_event(hal_motor_exec_t *exec, hal_motor_event_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_OUTBOUND_HAL_MOTOR_HAL_MOTOR_EXEC_PORT_H */
