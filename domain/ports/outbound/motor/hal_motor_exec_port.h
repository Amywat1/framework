/**
 * @file    hal_motor_exec_port.h
 * @brief   电机执行器出站端口。
 *
 * domain/mechanism/patterns 与 projects 侧 domain/mechanism 下的机构模块只依赖
 * 本端口的不透明句柄与类型，不感知执行器实现细节。
 * 真实执行器由适配器内部持有，项目 bindings 层只保存
 * hal_motor_exec_t 指针并注入各机构模块；默认实现位于
 * adapters/outbound/hal/components/motor_exec/。
 */
#ifndef DOMAIN_PORTS_OUTBOUND_MOTOR_HAL_MOTOR_EXEC_PORT_H
#define DOMAIN_PORTS_OUTBOUND_MOTOR_HAL_MOTOR_EXEC_PORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 电机执行器类型安全的不透明句柄，真实存储由 provider 持有。 */
typedef struct hal_motor_exec hal_motor_exec_t;

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

/** @brief 硬限位监视掩码位（与 hal_motor_limit_kind_t 对齐）。 */
#define HAL_MOTOR_LIMIT_MASK_POS    (1u << HAL_MOTOR_LIMIT_POS)
#define HAL_MOTOR_LIMIT_MASK_NEG    (1u << HAL_MOTOR_LIMIT_NEG)
#define HAL_MOTOR_LIMIT_MASK_ORIGIN (1u << HAL_MOTOR_LIMIT_ORIGIN)
#define HAL_MOTOR_LIMIT_MASK_ALL    (HAL_MOTOR_LIMIT_MASK_POS | HAL_MOTOR_LIMIT_MASK_NEG | HAL_MOTOR_LIMIT_MASK_ORIGIN)

/** @brief 判断掩码是否包含指定硬限位。 */
static inline bool hal_motor_limit_mask_has(uint8_t mask, hal_motor_limit_kind_t kind)
{
    return (mask & (uint8_t)(1u << (unsigned)kind)) != 0u;
}

/** @brief 电机状态。 */
typedef enum {
    HAL_MOTOR_PHASE_STOPPED = 0,   /**< 停止（上电默认态、故障安全态） */
    HAL_MOTOR_PHASE_WAITING_START, /**< 等待启动（冷却/预备/互锁排队） */
    HAL_MOTOR_PHASE_REVERSAL_WAIT, /**< 换向等待 */
    HAL_MOTOR_PHASE_RUNNING,       /**< 运行中 */
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
    HAL_MOTOR_FAULT_PREPARE_FAILED,    /**< 预备或运行中路径维持失败 */
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
    HAL_MOTOR_END_CURRENT,    /**< 电流到位（正常到位，不报警） */
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
 * @param  gear  1 基挡位号（1..N）；0 表示停止，run 会拒绝
 */
static inline hal_motor_speed_t hal_motor_speed_gear(int gear)
{
    hal_motor_speed_t s;
    s.kind  = HAL_MOTOR_SPEED_GEAR;
    s.value = gear;
    return s;
}

/**
 * @brief 运动到位的结束条件描述（可组合，超时兜底始终生效）。
 * @note  仅电流停、无位置/硬限位时，语义为持续运行至电流到位。
 *        与驱动监测过流故障共存：电流停为正常到位，过流仍为故障。
 */
typedef struct {
    /**
     * @brief 硬限位监视掩码（0=不启用）。
     * @note  置位的 POS/NEG/ORIGIN 任一触发即到位；同拍多路优先 ORIGIN，其次 POS，再次 NEG。
     */
    uint8_t  limit_mask;
    bool     use_position;       /**< 启用按位置到位（需编码器与可信基准） */
    int64_t  target_pos;         /**< 目标位置（脉冲） */
    bool     use_soft_limit;     /**< 启用软限位到位 */
    bool     use_current;        /**< 启用电流到位（正常切断，不报警） */
    int      current_limit;      /**< 电流阈值（与驱动电流同量纲） */
    uint32_t current_confirm_ms; /**< 持续超限确认时间（防抖，0=首拍即判） */
    /** @brief 启动后电流到位判定消隐（ms）；只抑制电流停，不影响过流故障。 */
    uint32_t current_blank_ms;
    bool     use_time;    /**< 启用按时间到位 */
    uint64_t duration_ms; /**< 运行时长（ms） */
    uint64_t max_time_ms; /**< 超时兜底（0=使用配置默认） */
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

/** @brief 锁存运动目标（异步）。spec 为 NULL 表示连续运行，否则按到位条件结束。
 * @note   再调用即更新目标，调用方不必按相位选择命令。
 * @note   FAULT / ESTOP 状态下必须拒绝；调用方须先 recover / 解除急停后再下发。
 * @note   终止事件入共享队列：每台电机须有消费者按节拍排空，否则可能拖累其它电机。
 */
hal_motor_cmd_result_t hal_motor_run(hal_motor_exec_t            *exec,
                                     int                          motor,
                                     hal_motor_speed_t            spd,
                                     hal_motor_dir_t              dir,
                                     const hal_motor_move_spec_t *spec);

/** @brief 减速停止（异步）。 */
hal_motor_cmd_result_t hal_motor_stop(hal_motor_exec_t *exec, int motor);

/**
 * @brief  回原点便利命令（provider 默认慢速/方向 + ORIGIN 限位）
 * @note   触原点后的基准重建由执行器完成；也可用 run 显式指定方向与速度。
 */
hal_motor_cmd_result_t hal_motor_home(hal_motor_exec_t *exec, int motor);

/** @brief 三步恢复：驱动器复位 → 模块停止（之后由调用方重新启动）。 */
hal_motor_cmd_result_t hal_motor_recover(hal_motor_exec_t *exec, int motor, hal_motor_recovery_step_t step);

/* 查询 —— motor 应在 [0, motor_count) 范围内；越界时返回各函数注释中的安全默认值。 */

/** @brief 查询电机当前状态；电机号越界返回 STOPPED。 */
hal_motor_phase_t hal_motor_phase(const hal_motor_exec_t *exec, int motor);

/** @brief 查询电机累计位置（脉冲）；电机号越界返回 0。 */
int64_t hal_motor_position(const hal_motor_exec_t *exec, int motor);

/** @brief 查询电机当前运动方向；电机号越界返回 FORWARD。 */
hal_motor_dir_t hal_motor_direction(const hal_motor_exec_t *exec, int motor);

/** @brief 查询电机当前故障码；无故障或电机号越界时为 HAL_MOTOR_FAULT_NONE。 */
hal_motor_fault_code_t hal_motor_fault_code(const hal_motor_exec_t *exec, int motor);

/**
 * @brief  查询编码器健康状态。
 * @return true 编码器读数可信；false 已检测到停滞或跳变，尚未经归位恢复。
 * @note   电机号越界时返回 false。无编码器的机构恒为 true。这是跨运动的持续状态，供上层作为报警条件；
 *         与"位置基准是否已建立"不同，增量编码器上电时基准未建立但编码器健康。
 */
bool hal_motor_encoder_healthy(const hal_motor_exec_t *exec, int motor);

/**
 * @brief  查询位置基准是否可信。
 * @return true 已通过回原点或执行器 confirm_baseline 建立可信基准；
 *         false 时禁止依赖按位置到位（执行器侧也会拒绝）。
 * @note   电机号越界时返回 false。无编码器的机构恒为 true。与 encoder_healthy 正交：上电后编码器可健康但基准未建。
 */
bool hal_motor_baseline_trusted(const hal_motor_exec_t *exec, int motor);

/**
 * @brief  取出一条运动结束事件，用于记录状态变化原因。
 * @param  exec 电机执行器句柄。
 * @param  out  输出事件；仅在返回 true 时有效。
 * @return true 取出一条事件；false 队列已空。
 * @note   队列容量有限；满时优先丢弃同电机最旧事件，否则丢全局最旧。
 *         每台产生事件的电机都必须有人消费，否则仍可能挤掉其它电机结局。
 * @note   经 motor_axis 管理的电机应由 axis poll 独占消费（见 pop_event_for）；
 *         若注册了会排空队列的执行器事件回调，则与 pop 互斥。
 */
bool hal_motor_pop_event(hal_motor_exec_t *exec, hal_motor_event_t *out);

/**
 * @brief  取出指定电机的下一条事件，保留其它电机事件。
 * @param  exec  电机执行器句柄。
 * @param  motor 电机号。
 * @param  out   输出事件；仅在返回 true 时有效。
 * @return true 取出；false 该电机无待取事件。
 */
bool hal_motor_pop_event_for(hal_motor_exec_t *exec, int motor, hal_motor_event_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_MOTOR_HAL_MOTOR_EXEC_PORT_H */
