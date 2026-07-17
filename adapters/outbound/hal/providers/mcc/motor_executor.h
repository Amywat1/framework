/**
 * @file motor_executor.h
 * @brief 通用电机运动控制模块 —— 领域层公共接口（C 版本）。
 *
 * 对应《通用电机运动控制模块功能说明（修订版）》。领域层不含任何机型特定代码，
 * 所有硬件操作经端口函数指针表注入，所有计时经可注入时间源(motor_clock_t)。
 *
 * 量纲约定：速度频率单位 0.01Hz(厘赫)；时间单位 ms；位置单位 脉冲；
 * 电流由适配层约定。执行器与配置采用编译期上限的定长存储，
 * 由调用方静态分配，领域层内部不做动态内存分配。
 */
#ifndef MOTOR_EXECUTOR_H
#define MOTOR_EXECUTOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------- 编译期容量上限 ------------------------- */
#define MOTOR_MAX_MOTORS       8   /**< 单执行器最多管理的电机数 */
#define MOTOR_MAX_DRIVERS      8   /**< 单执行器最多管理的物理驱动器数 */
#define MOTOR_MAX_INTERLOCKS   8   /**< 互锁规则条数上限 */
#define MOTOR_MAX_GEARS        16  /**< 挡位映射表长度上限 */
#define MOTOR_EVENT_QUEUE_CAP  64  /**< 事件队列容量（环形缓冲） */

/* 回原点默认低速频率（配置未提供 slowFreq 时使用），单位厘赫 */
#define MOTOR_HOME_DEFAULT_FREQ_CENTI_HZ  100
/* 编码器同步清零最大重试次数 */
#define MOTOR_ZERO_MAX_TRIES              3

/* ------------------------- 基础类型 / 枚举 ------------------------- */

/** @brief 运动方向。 */
typedef enum {
    MOTOR_DIR_FORWARD = 0, /**< 正向 */
    MOTOR_DIR_REVERSE = 1  /**< 反向 */
} motor_direction_t;

/** @brief 限位/原点采集种类。 */
typedef enum {
    MOTOR_LIMIT_POS = 0,   /**< 正向端限位 */
    MOTOR_LIMIT_NEG = 1,   /**< 反向端限位 */
    MOTOR_LIMIT_ORIGIN = 2 /**< 原点 */
} motor_limit_kind_t;

/** @brief 电机状态（对应功能说明 二、电机状态与状态转移）。 */
typedef enum {
    MOTOR_PHASE_STOPPED = 0,   /**< 停止（上电默认态、故障安全态） */
    MOTOR_PHASE_WAITING_START, /**< 等待启动（冷却/预备/互锁排队） */
    MOTOR_PHASE_REVERSAL_WAIT, /**< 换向等待 */
    MOTOR_PHASE_RUNNING,       /**< 运行中 */
    MOTOR_PHASE_PAUSED,        /**< 暂停 */
    MOTOR_PHASE_DECELERATING,  /**< 减速停止中 */
    MOTOR_PHASE_FAULT,         /**< 故障 */
    MOTOR_PHASE_ESTOP          /**< 急停 */
} motor_phase_t;

/** @brief 运动结束/触发条件（写入事件载荷）。 */
typedef enum {
    MOTOR_END_NONE = 0,
    MOTOR_END_LIMIT,      /**< 限位 */
    MOTOR_END_POSITION,   /**< 按位置 */
    MOTOR_END_SOFT_LIMIT, /**< 软限位 */
    MOTOR_END_TIME,       /**< 按时间 */
    MOTOR_END_TIMEOUT     /**< 超时兜底（错误结果） */
} motor_end_condition_t;

/** @brief 事件类型。 */
typedef enum {
    MOTOR_EVENT_ARRIVED = 0, /**< 正常到位 */
    MOTOR_EVENT_TIMEOUT,     /**< 超时（错误结果） */
    MOTOR_EVENT_STOPPED,     /**< 被显式停止 */
    MOTOR_EVENT_FAULT,       /**< 故障 */
    MOTOR_EVENT_ESTOP,       /**< 急停 */
    MOTOR_EVENT_WARNING      /**< 告警（不切断） */
} motor_event_type_t;

/** @brief 故障分级。 */
typedef enum {
    MOTOR_LEVEL_WARNING = 0, /**< 告警：不切断 */
    MOTOR_LEVEL_FAULT,       /**< 故障：切断锁定，三步可恢复 */
    MOTOR_LEVEL_FATAL        /**< 致命：须重新初始化 */
} motor_fault_level_t;

/** @brief 故障码。 */
typedef enum {
    MOTOR_FAULT_NONE = 0,
    MOTOR_FAULT_OVERCURRENT,      /**< 过载 */
    MOTOR_FAULT_UNDERCURRENT,     /**< 空转/断带 */
    MOTOR_FAULT_DRIVER_FEEDBACK,  /**< 驱动器运行反馈异常 */
    MOTOR_FAULT_OVERTEMP,         /**< 过温 */
    MOTOR_FAULT_UNDERVOLTAGE,     /**< 欠压 */
    MOTOR_FAULT_PREPARE_FAILED,   /**< 预备动作失败 */
    MOTOR_FAULT_ENCODER_SIGNAL,   /**< 编码器信号质量 */
    MOTOR_FAULT_WATCHDOG,         /**< 看门狗（tick 缺拍） */
    MOTOR_FAULT_DRIVER_PORT_FATAL,/**< 端口层致命错误 */
    MOTOR_FAULT_SHARED_DRIVER     /**< 共享驱动器联动 */
} motor_fault_code_t;

/** @brief 端口层健康状态。 */
typedef enum {
    MOTOR_PORT_OK = 0,
    MOTOR_PORT_FATAL = 1
} motor_port_status_t;

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
 * 选填（可置 NULL，采用默认行为）：prepare(默认成功)、temperature(默认不支持)、
 * voltage(默认不支持)、status(默认 OK)。
 */
typedef struct {
    void (*set_output)(void *ctx, int freq_centi_hz, motor_direction_t dir); /**< 频率给定+方向 */
    void (*cutoff)(void *ctx);                     /**< 立即切断输出 */
    bool (*reset)(void *ctx);                      /**< 驱动器侧故障复位，false=失败 */
    bool (*prepare)(void *ctx);                    /**< 预备动作，false=失败；可为 NULL */
    bool (*is_running)(void *ctx);                 /**< 运行反馈 */
    int  (*current)(void *ctx);                    /**< 负载电流（与阈值同量纲） */
    bool (*temperature)(void *ctx, int *out);      /**< 可选温度；可为 NULL */
    bool (*voltage)(void *ctx, int *out);          /**< 可选母线电压；可为 NULL */
    motor_port_status_t (*status)(void *ctx);      /**< 端口层健康；可为 NULL */
    void *ctx;
} motor_driver_t;

/**
 * @brief 编码器端口（仅带编码器的电机使用）。
 * @note raw 返回硬件累计脉冲幅值（方向由领域层按运动方向施加）。
 */
typedef struct {
    int64_t (*raw)(void *ctx);  /**< 硬件累计脉冲计数 */
    bool (*zero)(void *ctx);    /**< 同步清零硬件计数器，false=失败 */
    void *ctx;
} motor_encoder_t;

/** @brief 限位/原点采集端口。 */
typedef struct {
    bool (*limit)(void *ctx, int motor, motor_limit_kind_t kind); /**< 返回开关状态 */
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
    const motor_clock_t     *clock;    /**< 时间源，必填 */
    motor_driver_t  * const *drivers;  /**< 驱动器指针数组，长度=cfg.driver_count */
    motor_encoder_t * const *encoders; /**< 编码器指针数组，长度=电机数，无则填 NULL */
    const motor_sensors_t   *sensors;  /**< 限位采集，必填 */
    const motor_estop_t     *estop;    /**< 急停输入，必填 */
} motor_ports_t;

/* ------------------------- 配置 ------------------------- */

/** @brief 单电机监测项配置（各项独立，未启用不参与判定）。 */
typedef struct {
    bool monitor_current;   /**< 是否启用负载电流监测 */
    int  cur_max_accel;     /**< 加速段电流上限 */
    int  cur_min_accel;     /**< 加速段电流下限 */
    int  cur_max_steady;    /**< 匀速段电流上限 */
    int  cur_min_steady;    /**< 匀速段电流下限 */
    int  cur_confirm_ms;    /**< 越限持续确认门限 */
    int  startup_delay_ms;  /**< 启动后延迟再启用电流判定 */

    bool monitor_feedback;  /**< 是否启用运行反馈监测 */
    int  fb_confirm_ms;     /**< 反馈异常单次确认时间 */
    int  fb_retries;        /**< 瞬态自动重试次数（发作次数 > 此值才判故障） */

    bool monitor_temp;      /**< 是否启用温度监测 */
    int  temp_max;          /**< 温度上限 */
    int  temp_confirm_ms;   /**< 温度确认门限 */

    bool monitor_voltage;   /**< 是否启用母线电压监测 */
    int  volt_min;          /**< 电压下限 */
    int  volt_confirm_ms;   /**< 电压确认门限 */
} motor_monitor_cfg_t;

/** @brief 单电机配置。 */
typedef struct {
    int  driver_index;        /**< 指向哪个物理驱动器 */
    bool has_encoder;         /**< 是否配置编码器 */
    bool cap_position_move;   /**< 声明具备“按位置移动”能力（需编码器） */

    int  cooldown_ms;         /**< 停机冷却期 */
    int  reversal_stop_ms;    /**< 方向切换停止时间 */
    int  accel_ms;            /**< 加速时间 */
    int  decel_ms;            /**< 减速时间 */

    int  pos_tolerance;       /**< 到位容差（脉冲） */
    int  decel_point;         /**< 定位降速点（距目标脉冲数，0=不启用） */
    int  slow_freq;           /**< 定位低速段频率 */

    bool prep_required;       /**< 启动前需预备动作 */

    bool has_soft_limit;      /**< 是否配置软限位 */
    int64_t soft_min;         /**< 软限位下限（脉冲） */
    int64_t soft_max;         /**< 软限位上限（脉冲） */

    int  default_max_move_ms; /**< 运动到位默认超时兜底（必须 > 0） */

    int  enc_stall_ticks;     /**< 连续多拍无变化→告警（0=不检测） */
    int  enc_jump_max;        /**< 单拍跳变上限→告警（0=不检测） */
    bool enc_escalate;        /**< 编码器告警升级为故障 */

    int  gear_freq[MOTOR_MAX_GEARS]; /**< 挡位→频率映射表 */
    int  gear_count;                 /**< 有效挡位数 */

    motor_monitor_cfg_t mon;  /**< 监测项配置 */
} motor_motor_cfg_t;

/** @brief 互锁类型。 */
typedef enum {
    MOTOR_INTERLOCK_MUTEX = 0,        /**< 互斥：b 运行相关态时 a 不可启动 */
    MOTOR_INTERLOCK_PREREQ_POSITION   /**< 前置位置：a 启动需 b 处于位置区间 */
} motor_interlock_kind_t;

/** @brief 互锁规则。 */
typedef struct {
    motor_interlock_kind_t kind; /**< 互锁类型 */
    int a;                       /**< 受约束电机 */
    int b;                       /**< 关联电机 */
    int64_t pos_min;             /**< PrereqPosition: b 需 >= 此值 */
    int64_t pos_max;             /**< PrereqPosition: b 需 <= 此值 */
} motor_interlock_t;

/** @brief 执行器配置。 */
typedef struct {
    motor_motor_cfg_t motors[MOTOR_MAX_MOTORS]; /**< 各电机配置 */
    int motor_count;                            /**< 电机数 */
    motor_interlock_t interlocks[MOTOR_MAX_INTERLOCKS]; /**< 互锁规则 */
    int interlock_count;                        /**< 互锁条数 */
    int driver_count;                           /**< 物理驱动器数 */
    int watchdog_ms;                            /**< tick 缺拍阈值（>0） */
    int tick_ms;                                /**< 标称 tick 周期（>0） */
    int max_monitor_channels;                   /**< 监测通道容量上限（0=不限制） */
} motor_config_t;

/* ------------------------- 命令 / 结果 ------------------------- */

/** @brief 速度指定方式。 */
typedef enum {
    MOTOR_SPEED_FREQ = 0, /**< 直接频率（厘赫） */
    MOTOR_SPEED_GEAR = 1  /**< 挡位号（1 基） */
} motor_speed_kind_t;

/** @brief 速度指定。 */
typedef struct {
    motor_speed_kind_t kind;
    int value; /**< Freq: 厘赫；Gear: 1..N（0=停止，不可用于 run） */
} motor_speed_t;

/** @brief 构造频率速度。 */
static inline motor_speed_t motor_speed_freq(int centi_hz) {
    motor_speed_t s;
    s.kind = MOTOR_SPEED_FREQ;
    s.value = centi_hz;
    return s;
}

/**
 * @brief  构造挡位速度
 * @param  gear  1 基挡位号（1..N）；0 表示停止，run/move 会拒绝
 */
static inline motor_speed_t motor_speed_gear(int gear) {
    motor_speed_t s;
    s.kind = MOTOR_SPEED_GEAR;
    s.value = gear;
    return s;
}

/** @brief 运动到位的结束条件描述（可组合，超时兜底始终生效）。 */
typedef struct {
    bool use_limit;               /**< 启用限位到位 */
    motor_limit_kind_t limit;     /**< 限位种类 */
    bool use_position;            /**< 启用按位置到位（需编码器与可信基准） */
    int64_t target_pos;           /**< 目标位置（脉冲） */
    bool use_soft_limit;          /**< 启用软限位到位 */
    bool use_time;                /**< 启用按时间到位 */
    uint64_t duration_ms;         /**< 运行时长（ms） */
    uint64_t max_time_ms;         /**< 超时兜底（0=使用配置默认） */
} motor_move_spec_t;

/** @brief 命令受理状态。 */
typedef enum {
    MOTOR_CMD_ACCEPTED = 0, /**< 已受理 */
    MOTOR_CMD_QUEUED,       /**< 已排队（冷却等） */
    MOTOR_CMD_REJECTED      /**< 被拒绝 */
} motor_cmd_status_t;

/** @brief 命令结果。 */
typedef struct {
    motor_cmd_status_t status;
    const char *reason; /**< 说明（静态字符串，可用于日志） */
} motor_cmd_result_t;

/** @brief 判定命令是否非拒绝。 */
static inline bool motor_cmd_ok(motor_cmd_result_t r) {
    return r.status != MOTOR_CMD_REJECTED;
}

/** @brief 初始化结果。 */
typedef struct {
    bool ok;
    const char *error; /**< 失败原因（静态字符串），ok 时为空串 */
} motor_init_result_t;

/** @brief 三步恢复流程步骤。 */
typedef enum {
    MOTOR_RECOVERY_DRIVER_RESET = 0, /**< 第一步：驱动器复位 */
    MOTOR_RECOVERY_MODULE_STOP       /**< 第二步：模块停止（之后可重新启动） */
} motor_recovery_step_t;

/* ------------------------- 事件 ------------------------- */

/** @brief 事件载荷。 */
typedef struct {
    int motor;                      /**< 电机号 */
    motor_event_type_t type;        /**< 事件类型 */
    motor_end_condition_t trigger;  /**< 触发条件 */
    int64_t final_pos;              /**< 最终位置（脉冲） */
    uint64_t elapsed_ms;            /**< 运动耗时（ms） */
    motor_fault_code_t fault;       /**< 故障码 */
    motor_fault_level_t level;      /**< 故障分级 */
} motor_event_t;

/** @brief 事件回调；ctx 为 motor_set_event_callback 注册时透传的上下文。 */
typedef void (*motor_event_cb_t)(const motor_event_t *ev, void *ctx);

/* ------------------------- 执行器内部（由调用方静态分配） ------------------------- */
/*
 * 以下结构体对调用方公开只是为了支持静态分配（无动态内存）。
 * 除通过下方公共 API 外，请勿直接访问其字段。
 */

/** @brief 内部命令描述（暂存排队/换向后的启动请求）。 */
typedef struct {
    bool is_move;
    int  freq;
    motor_direction_t dir;
    motor_move_spec_t spec;
} motor_pending_cmd_t;

/** @brief 单电机运行时状态（内部）。 */
typedef struct {
    motor_phase_t phase;
    motor_direction_t dir;

    int target_freq;
    int cur_freq;

    bool moveActive;
    motor_move_spec_t spec;
    uint64_t move_start_ms;
    uint64_t paused_elapsed_ms;
    bool emit_stop_on_halt;

    int64_t position;
    int64_t last_raw;
    bool baseline_trusted;
    bool homing;
    bool origin_was_active; /**< 上一拍原点限位电平，用于上升沿清编码器 */

    uint64_t cooldown_until;
    bool queued;
    motor_pending_cmd_t pending;

    uint64_t reversal_until;
    motor_pending_cmd_t after_reversal;

    bool fault;
    bool fatal;
    motor_fault_code_t fault_code;
    bool driver_reset_done;

    uint64_t start_ms;
    int cur_over_ms;
    int cur_under_ms;
    int fb_bad_ms;
    int fb_strikes;
    int temp_bad_ms;
    int volt_bad_ms;

    int enc_stall;
    bool enc_warned;
} motor_mstate_t;

/** @brief 执行器对象（调用方静态分配后传入 motor_init）。 */
typedef struct {
    motor_config_t cfg;
    motor_ports_t ports;
    motor_mstate_t m[MOTOR_MAX_MOTORS];
    int motor_count;

    uint64_t now;
    uint64_t last_tick_ms;
    bool last_tick_valid;
    bool estop_latched;
    bool safe_latched;

    motor_event_t events[MOTOR_EVENT_QUEUE_CAP];
    int ev_head;
    int ev_count;

    motor_event_cb_t cb;
    void *cb_ctx;
    bool in_dispatch;

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
motor_init_result_t motor_init(motor_executor_t *exec,
                               const motor_config_t *cfg,
                               const motor_ports_t *ports);

/**
 * @brief 重新初始化（致命错误后恢复），沿用首次 motor_init 的 cfg/ports。
 */
motor_init_result_t motor_reinit(motor_executor_t *exec);

/**
 * @brief 周期处理。须按 cfg.tick_ms 节拍调用。
 * @note 相邻两次调用间隔超过 watchdog_ms 将进入安全态并切断输出。
 */
void motor_tick(motor_executor_t *exec);

/** @brief 持续运行（异步）。 */
motor_cmd_result_t motor_run_continuous(motor_executor_t *exec, int motor,
                                        motor_speed_t spd, motor_direction_t dir);

/** @brief 运动到位（异步）。spec 描述结束条件，可组合，超时兜底始终生效。 */
motor_cmd_result_t motor_move_to(motor_executor_t *exec, int motor,
                                 motor_speed_t spd, motor_direction_t dir,
                                 const motor_move_spec_t *spec);

/** @brief 减速停止（异步）。 */
motor_cmd_result_t motor_stop(motor_executor_t *exec, int motor);

/** @brief 暂停（保留运动参数）。 */
motor_cmd_result_t motor_pause(motor_executor_t *exec, int motor);

/** @brief 从暂停点恢复。 */
motor_cmd_result_t motor_resume(motor_executor_t *exec, int motor);

/** @brief 运行中调速/改向（改向自动走换向安全流程）。 */
motor_cmd_result_t motor_set_speed(motor_executor_t *exec, int motor,
                                   motor_speed_t spd, motor_direction_t dir);

/** @brief 回原点：向原点运动并在触发原点后建立可信基准。 */
motor_cmd_result_t motor_home(motor_executor_t *exec, int motor);

/** @brief 手动清零编码器（同步硬件计数器，失败自动重试）。 */
motor_cmd_result_t motor_zero_encoder(motor_executor_t *exec, int motor);

/** @brief 上层显式确认位置基准可信。 */
motor_cmd_result_t motor_confirm_baseline(motor_executor_t *exec, int motor);

/** @brief 急停解除后显式复位。 */
void motor_reset_estop(motor_executor_t *exec);

/** @brief 看门狗恢复正常节拍后复位。 */
void motor_reset_watchdog(motor_executor_t *exec);

/** @brief 三步恢复：驱动器复位 → 模块停止 → （之后由调用方重新启动）。 */
motor_cmd_result_t motor_recover(motor_executor_t *exec, int motor,
                                 motor_recovery_step_t step);

/* 查询 —— 所有带 motor 参数的函数均要求 motor 在 [0, motor_count) 范围内 */

/**
 * @brief 查询电机当前状态。
 * @param exec  已完成初始化的执行器。
 * @param motor 电机编号，须在 [0, motor_count) 范围内。
 * @return 当前 motor_phase_t 枚举值。
 */
motor_phase_t motor_phase(const motor_executor_t *exec, int motor);

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
 * @return 最近一次生效的方向；上电默认为 MOTOR_DIR_FORWARD。
 */
motor_direction_t motor_direction(const motor_executor_t *exec, int motor);

/**
 * @brief 查询电机当前故障码。
 * @param exec  已完成初始化的执行器。
 * @param motor 电机编号，须在 [0, motor_count) 范围内。
 * @return 当前 motor_fault_code_t；无故障时为 MOTOR_FAULT_NONE。
 *         仅在 phase 为 MOTOR_PHASE_FAULT 时有实质含义。
 */
motor_fault_code_t motor_fault_code(const motor_executor_t *exec, int motor);

/**
 * @brief 查询电机位置基准是否可信。
 * @param exec  已完成初始化的执行器。
 * @param motor 电机编号，须在 [0, motor_count) 范围内。
 * @return true 表示已通过回原点或 motor_confirm_baseline 建立可信基准；
 *         false 时禁止使用按位置到位功能。
 */
bool motor_baseline_trusted(const motor_executor_t *exec, int motor);

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
bool motor_pop_event(motor_executor_t *exec, motor_event_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_EXECUTOR_H */
