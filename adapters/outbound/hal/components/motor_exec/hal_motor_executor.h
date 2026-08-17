/**
 * @file hal_motor_executor.h
 * @brief 电机执行器组合件 —— 运动状态机与硬件端口注入接口。
 *
 * 与具体机型/厂商无关：所有硬件操作经端口函数指针表注入，计时经 motor_clock_t。
 * 项目 bindings 静态分配 motor_executor_t，经 motor_init 后以
 * hal_motor_exec_t * 注入 domain 模式；本组合件的 .c 同时提供 hal_motor_exec_port 的 hal_motor_* 符号。
 *
 * 量纲约定：速度频率单位 0.01Hz(厘赫)；时间单位 ms；位置单位 脉冲；
 * 电流由适配层约定。执行器与配置采用编译期上限的定长存储，不做动态内存分配。
 */
#ifndef ADAPTERS_OUTBOUND_HAL_COMPONENTS_MOTOR_EXEC_HAL_MOTOR_EXECUTOR_H
#define ADAPTERS_OUTBOUND_HAL_COMPONENTS_MOTOR_EXEC_HAL_MOTOR_EXECUTOR_H

#include "common/sw_error.h"
#include "domain/ports/outbound/motor/hal_motor_exec_port.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------- 编译期容量上限 ------------------------- */
/*
 * MOTOR_MAX_MOTORS / DRIVERS / INTERLOCKS = 16：覆盖当前隧道/龙门多轴接入余量
 * （输送、侧刷、顶刷、风机、行车/升降等逻辑轴合计），并控制静态 RAM。
 * 互锁条数与轴数同级。
 */
#define MOTOR_MAX_MOTORS      16 /**< 单执行器最多管理的电机数 */
#define MOTOR_MAX_DRIVERS     16 /**< 单执行器最多管理的物理驱动器数 */
#define MOTOR_MAX_INTERLOCKS  16 /**< 互锁规则条数上限 */
/** 事件队列容量（环形缓冲）；满时优先丢同电机最旧，避免无人消费的轴挤掉其它轴。 */
#define MOTOR_EVENT_QUEUE_CAP 64

/* 回原点默认低速频率（配置未提供 slowFreq 时使用），单位厘赫 */
#define MOTOR_HOME_DEFAULT_FREQ_CENTI_HZ 100
/* 编码器同步清零最大重试次数 */
#define MOTOR_ZERO_MAX_TRIES             3

/** @brief 端口层健康状态。 */
typedef enum { MOTOR_PORT_OK = 0, MOTOR_PORT_FATAL = 1 } motor_port_status_t;

/**
 * @brief 驱动器预备/运行巡检结果（异步三态）。
 *
 * prepare：READY 可进入运行；BUSY 留在 WAITING_START；FAILED 进入 PREPARE_FAILED。
 * poll：READY 继续跑；BUSY 确认中本拍忽略；FAILED 进入 PREPARE_FAILED。
 */
typedef enum { MOTOR_PREPARE_READY = 0, MOTOR_PREPARE_BUSY, MOTOR_PREPARE_FAILED } motor_prepare_result_t;

/* ------------------------- 硬件端口接口 ------------------------- */

/**
 * @brief 时间源端口。模块所有计时基于它。
 * @note now_ms 必须非空；返回单调递增的毫秒计数。
 */
typedef struct {
    uint64_t (*now_ms)(void *ctx); /**< 返回当前毫秒计数 */
    void *ctx;                     /**< 透传给回调的上下文 */
} motor_clock_t;

/**
 * @brief 物理驱动器端口（可被多台电机共享）。
 *
 * 必填：set_output/cutoff/reset/is_running/current。
 * 选填（可置 NULL，采用默认行为）：prepare(默认 READY)、poll(默认 READY)、
 * temperature(默认不支持)、voltage(默认不支持)、status(默认 OK)。
 *
 * @note prepare/poll 的 motor 为逻辑电机索引，便于共享驱动区分路径。
 *       poll 只维持驱动器侧不变量（如接触器路径），不得改写速度给定；
 *       is_running 只回答功率级是否在转；status 只回答端口是否致命。
 */
typedef struct {
    sw_err_t (*set_output)(void *ctx, hal_motor_speed_t speed, hal_motor_dir_t dir); /**< 速度给定+方向 */
    sw_err_t (*cutoff)(void *ctx);                                                 /**< 立即切断输出 */
    bool (*reset)(void *ctx);                                                      /**< 驱动器侧故障复位，false=失败 */
    motor_prepare_result_t (*prepare)(void *ctx, int motor);                       /**< 启动前预备；可为 NULL */
    motor_prepare_result_t (*poll)(void *ctx, int motor);                          /**< RUNNING 每拍巡检；可为 NULL */
    bool (*is_running)(void *ctx);                                                 /**< 功率级运行反馈 */
    int (*current)(void *ctx);                                                     /**< 负载电流（与阈值同量纲） */
    bool (*temperature)(void *ctx, int *out);                                      /**< 可选温度；可为 NULL */
    bool (*voltage)(void *ctx, int *out);                                          /**< 可选母线电压；可为 NULL */
    motor_port_status_t (*status)(void *ctx);                                      /**< 端口层健康；可为 NULL */
    void *ctx;
} motor_driver_t;

/**
 * @brief 编码器语义种类。
 * @note  INCREMENTAL：raw 为累计脉冲，位置由 Δraw×方向积分；
 *        ABSOLUTE：raw 为已标定绝对行程，每拍直接写入 position。
 */
typedef enum {
    MOTOR_ENC_INCREMENTAL = 0,
    MOTOR_ENC_ABSOLUTE    = 1,
} motor_encoder_kind_t;

/**
 * @brief 编码器端口（仅带编码器的电机使用）。
 * @note  INCREMENTAL：raw 返回硬件累计脉冲幅值（方向由领域层施加）；
 *        ABSOLUTE：raw 返回工程行程（静止时亦刷新 position）；
 *        zero 仅对增量轴有意义；绝对轴可空操作并返回 true。
 */
typedef struct {
    int64_t (*raw)(void *ctx); /**< 增量=累计脉冲；绝对=工程行程 */
    bool (*zero)(void *ctx);   /**< 同步清零硬件计数器，false=失败 */
    void *ctx;
} motor_encoder_t;

/** @brief 限位/原点采集端口。 */
typedef struct {
    bool (*limit)(void *ctx, int motor, hal_motor_limit_kind_t kind); /**< 返回开关状态 */
    void *ctx;
} motor_sensors_t;

/** @brief 急停输入端口（全局）。 */
typedef struct {
    bool (*active)(void *ctx); /**< 急停信号是否有效 */
    void *ctx;
} motor_estop_t;

/**
 * @brief 端口集合。drivers/encoders 为指针数组。
 * @note encoders 数组长度必须等于电机数，无编码器的电机对应项置 NULL。
 */
typedef struct {
    const motor_clock_t    *clock;    /**< 时间源，必填 */
    motor_driver_t *const  *drivers;  /**< 驱动器指针数组，长度=cfg.driver_count */
    motor_encoder_t *const *encoders; /**< 编码器指针数组，长度=电机数，无则填 NULL */
    const motor_sensors_t  *sensors;  /**< 限位采集，必填 */
    const motor_estop_t    *estop;    /**< 急停输入，必填 */
} motor_ports_t;

/* ------------------------- 配置 ------------------------- */

/** @brief 单电机监测项配置（各项独立，未启用不参与判定）。 */
typedef struct {
    bool monitor_current;  /**< 是否启用负载电流监测 */
    int  cur_max_accel;    /**< 加速段电流上限 */
    int  cur_min_accel;    /**< 加速段电流下限 */
    int  cur_max_steady;   /**< 匀速段电流上限 */
    int  cur_min_steady;   /**< 匀速段电流下限 */
    int  cur_confirm_ms;   /**< 越限持续确认门限 */
    int  startup_delay_ms; /**< 启动后延迟再启用电流判定 */

    bool monitor_feedback; /**< 是否启用运行反馈监测 */
    int  fb_confirm_ms;    /**< 反馈异常单次确认时间 */
    int  fb_retries;       /**< 瞬态自动重试次数（发作次数 > 此值才判故障） */

    bool monitor_temp;     /**< 是否启用温度监测 */
    int  temp_max;         /**< 温度上限 */
    int  temp_confirm_ms;  /**< 温度确认门限 */

    bool monitor_voltage;  /**< 是否启用母线电压监测 */
    int  volt_min;         /**< 电压下限 */
    int  volt_confirm_ms;  /**< 电压确认门限 */
} motor_monitor_cfg_t;

/** @brief 单电机配置。 */
typedef struct {
    int                  driver_index;      /**< 指向哪个物理驱动器 */
    bool                 has_encoder;       /**< 是否配置编码器 */
    motor_encoder_kind_t encoder_kind;      /**< 编码器语义；无编码器时忽略 */

    int cooldown_ms;                        /**< 停机冷却期 */
    int reversal_stop_ms;                   /**< 方向切换停止时间 */
    int accel_ms;                           /**< 电流监测加速段时长（ms） */

    int pos_tolerance;                      /**< 到位容差（脉冲） */
    int decel_point;                        /**< 定位降速点（距目标脉冲数，0=不启用） */
    int position_slow_gear;                 /**< 挡位定位进入减速区后的挡位，0=不切换 */
    int slow_freq;                          /**< 定位低速段频率 */

    bool prep_required;                     /**< 启动前需预备动作 */

    bool    has_soft_limit;                 /**< 是否配置软限位 */
    int64_t soft_min;                       /**< 软限位下限（脉冲） */
    int64_t soft_max;                       /**< 软限位上限（脉冲） */

    int default_max_time_ms;                /**< 运行默认超时兜底（必须 > 0） */

    int  enc_stall_ticks;                   /**< 连续多拍无变化→告警（0=不检测） */
    int  enc_jump_max;                      /**< 单拍跳变上限→告警（0=不检测） */
    bool enc_escalate;                      /**< 编码器告警升级为故障 */

    int gear_count;                         /**< 可用挡位数；执行器仅校验范围，不转换频率 */

    motor_monitor_cfg_t mon;                /**< 监测项配置 */
} motor_motor_cfg_t;

/** @brief 互锁类型。 */
typedef enum {
    MOTOR_INTERLOCK_MUTEX = 0,      /**< 互斥：b 运行相关态时 a 不可启动 */
    MOTOR_INTERLOCK_PREREQ_POSITION /**< 前置位置：a 启动需 b 处于位置区间 */
} motor_interlock_kind_t;

/** @brief 互锁规则。 */
typedef struct {
    motor_interlock_kind_t kind;    /**< 互锁类型 */
    int                    a;       /**< 受约束电机 */
    int                    b;       /**< 关联电机 */
    int64_t                pos_min; /**< PrereqPosition: b 需 >= 此值 */
    int64_t                pos_max; /**< PrereqPosition: b 需 <= 此值 */
} motor_interlock_t;

/** @brief 执行器配置。 */
typedef struct {
    motor_motor_cfg_t motors[MOTOR_MAX_MOTORS];         /**< 各电机配置 */
    int               motor_count;                      /**< 电机数 */
    motor_interlock_t interlocks[MOTOR_MAX_INTERLOCKS]; /**< 互锁规则 */
    int               interlock_count;                  /**< 互锁条数 */
    int               driver_count;                     /**< 物理驱动器数 */
    int               watchdog_ms;                      /**< tick 缺拍阈值（>0） */
    int               tick_ms;                          /**< 标称 tick 周期（>0） */
} motor_config_t;

/** @brief 初始化结果。 */
typedef struct {
    bool        ok;
    const char *error; /**< 失败原因（静态字符串），ok 时为空串 */
} motor_init_result_t;

/* ------------------------- 事件 ------------------------- */

/** @brief 事件回调；载荷即端口 `hal_motor_event_t`。ctx 为注册时透传的上下文。 */
typedef void (*motor_event_cb_t)(const hal_motor_event_t *ev, void *ctx);

/* ------------------------- 执行器内部（由调用方静态分配） ------------------------- */
/*
 * 以下结构体对调用方公开只是为了支持静态分配（无动态内存）。
 * 除通过下方公共 API 外，请勿直接访问其字段。
 */

/** @brief 内部命令描述（锁存的运动目标：排队/换向后/当前运行）。 */
typedef struct {
    bool                  is_move;
    hal_motor_speed_t     speed;
    hal_motor_dir_t       dir;
    hal_motor_move_spec_t spec;
} motor_pending_cmd_t;

/** @brief 单电机运行时状态（内部）。 */
typedef struct {
    hal_motor_phase_t     phase;
    hal_motor_dir_t dir;

    hal_motor_speed_t speed;
    hal_motor_speed_t applied_speed;
    bool          output_applied;

    bool               move_active;
    hal_motor_move_spec_t  spec;
    hal_motor_limit_kind_t end_limit;    /**< 本次硬限位到位实际触发种类（仅 END_LIMIT 有效） */
    uint64_t           move_start_ms; /**< 当前运行段起点（ms） */
    uint64_t           elapsed_ms;    /**< 已结算的运动耗时（离开 RUNNING 时冻结） */
    bool               emit_stop_on_halt;

    int64_t position;
    int64_t last_raw;
    bool    baseline_trusted;

    uint64_t            cooldown_until;
    bool                queued;
    motor_pending_cmd_t pending;

    uint64_t            reversal_until;
    motor_pending_cmd_t after_reversal;

    bool               fatal;
    hal_motor_fault_code_t fault_code;
    bool               driver_reset_done;

    uint64_t start_ms;
    int      cur_over_ms;
    int      cur_under_ms;
    uint32_t cur_stop_ms; /**< 电流到位防抖累计（ms） */
    int      fb_bad_ms;
    int      fb_strikes;
    int      temp_bad_ms;
    int      volt_bad_ms;

    int  enc_stall;
    bool enc_warned;
    /** @brief 编码器健康状态：上电为真，检测到停滞或跳变置假，归位重建基准后恢复。
     *
     * 与 enc_warned 不同：后者是单次运动内的告警去重标志，每次启动即复位；
     * 本字段是跨运动的持续状态，供上层作为报警条件与降级依据。
     * 也与 baseline_trusted 不同：增量编码器上电时基准尚未建立但编码器是健康的。
     */
    bool enc_healthy;
} motor_mstate_t;

/** @brief 执行器对象（调用方静态分配后传入 motor_init）。 */
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
    int           ev_head;
    int           ev_count;

    motor_event_cb_t cb;
    void            *cb_ctx;
    bool             in_dispatch;

    bool initialized;
} motor_executor_t;

/* ------------------------- 公共 API ------------------------- */

/**
 * @brief 初始化执行器：fail-fast 配置校验 + 状态归零。
 * @param exec  调用方分配的执行器对象。
 * @param cfg   配置（按值拷贝进 exec）。
 * @param ports 端口集合（按值拷贝，内部数组由调用方保证生命周期）。
 * @return ok=true 表示可运行；否则 error 指向失败原因。
 * @note 上电默认态为全部停止且输出关断。
 */
motor_init_result_t motor_init(motor_executor_t *exec, const motor_config_t *cfg, const motor_ports_t *ports);

/**
 * @brief 重新初始化（致命错误后恢复），沿用首次 motor_init 的 cfg/ports。
 */
motor_init_result_t motor_reinit(motor_executor_t *exec);

/**
 * @brief 周期处理。须按 cfg.tick_ms 节拍调用。
 * @note 相邻两次调用间隔超过 watchdog_ms 将进入安全态并切断输出。
 */
void motor_tick(motor_executor_t *exec);

/**
 * @brief 锁存运动目标（异步）。spec 为 NULL 表示连续运行，否则按到位条件结束。
 * @note  再调用即更新目标：已在跑且同向则就地改速度/结束条件，换向走换向安全流程；
 *        冷却/预备/换向等待中更新挂起目标。FAULT / ESTOP 必须拒绝。
 * @note  每台运动会向共享事件队列投递终止事件；该电机须有人按节拍消费
 *        （motor_axis_poll / pop_event_for / 事件回调），否则满队列时虽优先丢本电机
 *        旧事件，仍可能在无本电机旧事件时挤掉其它电机事件。
 */
hal_motor_cmd_result_t motor_run(motor_executor_t            *exec,
                                 int                          motor,
                                 hal_motor_speed_t            spd,
                                 hal_motor_dir_t              dir,
                                 const hal_motor_move_spec_t *spec);

/** @brief 减速停止（异步）。 */
hal_motor_cmd_result_t motor_stop(motor_executor_t *exec, int motor);

/**
 * @brief  回原点便利命令（慢速 + 默认反向 + ORIGIN 限位）
 * @note   触原点建基准由执行器在任意 ORIGIN 限位运动结束路径完成；
 *         也可用 motor_run 显式指定方向/速度达到同等效果。
 */
hal_motor_cmd_result_t motor_home(motor_executor_t *exec, int motor);

/** @brief 手动清零编码器（同步硬件计数器，失败自动重试）。 */
hal_motor_cmd_result_t motor_zero_encoder(motor_executor_t *exec, int motor);

/** @brief 上层显式确认位置基准可信。 */
hal_motor_cmd_result_t motor_confirm_baseline(motor_executor_t *exec, int motor);

/** @brief 急停解除后显式复位。 */
void motor_reset_estop(motor_executor_t *exec);

/** @brief 看门狗恢复正常节拍后复位。 */
void motor_reset_watchdog(motor_executor_t *exec);

/** @brief 三步恢复：驱动器复位 → 模块停止 → （之后由调用方重新启动）。 */
hal_motor_cmd_result_t motor_recover(motor_executor_t *exec, int motor, hal_motor_recovery_step_t step);

/* 查询 —— 所有带 motor 参数的函数均要求 motor 在 [0, motor_count) 范围内 */

/**
 * @brief 查询电机当前状态。
 * @param exec  已完成初始化的执行器。
 * @param motor 电机编号，须在 [0, motor_count) 范围内。
 * @return 当前 hal_motor_phase_t 枚举值。
 */
hal_motor_phase_t motor_phase(const motor_executor_t *exec, int motor);

/**
 * @brief 查询电机累计位置。
 * @param exec  已完成初始化的执行器。
 * @param motor 电机编号，须在 [0, motor_count) 范围内。
 * @return 累计脉冲计数；baseline_trusted 为 false 时该值不具有绝对意义。
 */
int64_t motor_position(const motor_executor_t *exec, int motor);

/**
 * @brief 查询电机当前输出频率。
 * @param exec  已完成初始化的执行器。
 * @param motor 电机编号，须在 [0, motor_count) 范围内。
 * @return 当前给定频率（厘赫）；输出关断时为 0。
 */
int motor_current_freq(const motor_executor_t *exec, int motor);

/**
 * @brief 查询电机当前运动方向。
 * @param exec  已完成初始化的执行器。
 * @param motor 电机编号，须在 [0, motor_count) 范围内。
 * @return 最近一次生效的方向；上电默认为 HAL_MOTOR_DIR_FORWARD。
 */
hal_motor_dir_t motor_direction(const motor_executor_t *exec, int motor);

/**
 * @brief 查询电机当前故障码。
 * @param exec  已完成初始化的执行器。
 * @param motor 电机编号，须在 [0, motor_count) 范围内。
 * @return 当前 hal_motor_fault_code_t；无故障时为 HAL_MOTOR_FAULT_NONE。
 *         仅在 phase 为 HAL_MOTOR_PHASE_FAULT 时有实质含义。
 */
hal_motor_fault_code_t motor_fault_code(const motor_executor_t *exec, int motor);

/**
 * @brief 查询电机位置基准是否可信。
 * @param exec  已完成初始化的执行器。
 * @param motor 电机编号，须在 [0, motor_count) 范围内。
 * @return true 表示已通过回原点或 motor_confirm_baseline 建立可信基准；
 *         false 时禁止使用按位置到位功能。
 * @note   无编码器的机构恒为 true。
 */
bool motor_baseline_trusted(const motor_executor_t *exec, int motor);

/**
 * @brief  查询编码器健康状态。
 * @return true 编码器读数可信或该轴无编码器；false 已检测到停滞或跳变且尚未归位恢复。
 */
bool motor_encoder_healthy(const motor_executor_t *exec, int motor);

/**
 * @brief 查询执行器是否处于看门狗安全态。
 * @param exec 已完成初始化的执行器。
 * @return true 表示 tick 缺拍已触发安全锁定，所有电机已切断输出；
 *         须调用 motor_reset_watchdog 方可恢复。
 */
bool motor_in_safe_state(const motor_executor_t *exec);

/** @brief 注册事件回调；ctx 在回调时透传。回调运行于 tick 上下文，回调内不得下发运动指令。 */
void motor_set_event_callback(motor_executor_t *exec, motor_event_cb_t cb, void *ctx);

/** @brief 队列方式取事件。返回 false 表示队列为空。 */
bool motor_pop_event(motor_executor_t *exec, hal_motor_event_t *out);

/**
 * @brief  取出指定电机的下一条事件，保留其它电机事件。
 * @return true 取出；false 该电机无待取事件。
 */
bool motor_pop_event_for(motor_executor_t *exec, int motor, hal_motor_event_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_COMPONENTS_MOTOR_EXEC_HAL_MOTOR_EXECUTOR_H */
