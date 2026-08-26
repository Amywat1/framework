/**
 * @file    motor_executor_internal.h
 * @brief   电机执行器内部类型与跨编译单元接口
 *
 * @note    仅供 motor_executor*.c 互调，不对外包含。
 *          对外契约见 motor_executor.h 与 motor_exec.h。
 */

#ifndef DOMAIN_MECHANISM_MOTOR_MOTOR_EXECUTOR_INTERNAL_H
#define DOMAIN_MECHANISM_MOTOR_MOTOR_EXECUTOR_INTERNAL_H

#include "domain/mechanism/motor/motor_executor.h"

#include <pthread.h>

/* ------------------------- 内部类型 ------------------------- */

/** @brief 挂起或换向后待执行的运动目标。 */
typedef struct {
    bool              is_move; /**< true 表示带到位条件，false 为连续运行 */
    motor_speed_t     speed;   /**< 目标速度 */
    motor_dir_t       dir;     /**< 目标方向 */
    motor_move_spec_t spec;    /**< 到位条件；连续运行时忽略 */
} motor_pending_cmd_t;

/** @brief 单电机事件环形槽（深度 MOTOR_EVENT_SLOT_CAP）。 */
typedef struct {
    motor_event_t q[MOTOR_EVENT_SLOT_CAP]; /**< 事件缓冲 */
    int           head;                    /**< 队首下标 */
    int           count;                   /**< 当前条数 */
} motor_event_slot_t;

/**
 * @brief 单电机运行时状态
 * @note  下一目标只有一份 pending：WAITING_START / STOPPING 用 queued 标记有效，
 *        REVERSAL_WAIT 由状态本身表示 pending 有效。两态不会同时持有目标。
 */
typedef struct {
    /* —— 运动会话 —— */
    motor_exec_state_t exec_state; /**< 当前运行状态 */
    motor_dir_t        dir;        /**< 当前/待输出方向 */
    motor_speed_t      speed;      /**< 目标速度 */
    motor_speed_t      applied_speed;  /**< 上次成功写入驱动的速度 */
    bool               output_applied; /**< 本拍是否已建立功率级输出 */
    bool               move_active;    /**< 是否处于带到位条件的运动会话 */
    motor_move_spec_t  spec;           /**< 当前运动到位条件 */
    motor_limit_kind_t end_limit;      /**< 本次硬限位终止时触发的种类 */
    uint64_t           move_start_ms;  /**< 本段 RUNNING 起点（用于累加 elapsed） */
    uint64_t           elapsed_ms;     /**< 已冻结的运行时长（ms） */
    bool               emit_stop_on_halt;  /**< 停机完成后是否推 STOPPED 事件 */
    bool               stop_issued;        /**< 是否已下发受控停止 */
    uint64_t           stopping_since_ms;  /**< 进入 STOPPING 后等待功率级停下的起点 */
    uint64_t           cooldown_until;     /**< 冷却结束时刻 */
    uint64_t           reversal_until;     /**< 换向切断后允许再启动的时刻 */
    bool               queued;             /**< WAITING_START / STOPPING 是否有待启动目标 */
    motor_pending_cmd_t pending;           /**< 下一运动目标 */

    /* —— 位置基准 —— */
    int64_t position;         /**< 逻辑位置（脉冲） */
    int64_t last_raw;         /**< 上一拍编码器 raw */
    bool    baseline_trusted; /**< 位置基准是否可信 */
    int     enc_stall;        /**< 编码器连续无变化拍数 */
    bool    enc_warned;       /**< 本运动是否已发过编码器 WARNING */
    bool    enc_healthy;      /**< 编码器读数是否可信 */

    /* —— 故障 —— */
    bool                    fatal;             /**< 是否致命故障（须 reinit） */
    motor_exec_fault_code_t fault_code;        /**< 当前故障码 */
    bool                    driver_reset_done; /**< 恢复流程中驱动器复位是否已完成 */

    /* —— 监测累加 —— */
    uint64_t start_ms;     /**< 本次启动时刻（监测用） */
    int      cur_over_ms;         /**< 过流累计确认时间 */
    int      cur_under_ms;        /**< 欠流累计确认时间 */
    uint32_t cur_stop_ms;         /**< 电流到位累计确认时间 */
    int      last_current;        /**< 最近一次监测采样电流 */
    int      current_trip_limit;  /**< 过流/欠流触发时的阈值；未触发为 0 */
    int      fb_bad_ms;    /**< 运行反馈异常累计时间 */
    int      fb_strikes;   /**< 反馈异常发作次数 */
    int      temp_bad_ms;  /**< 过温累计确认时间 */
    int      volt_bad_ms;  /**< 欠压累计确认时间 */
} motor_mstate_t;

/** @brief 执行器完整运行时对象（配置、端口、各轴状态与全局闩锁）。 */
typedef struct {
    motor_config_t cfg;                    /**< 绑定时的配置副本 */
    motor_ports_t  ports;                  /**< 绑定时的端口表副本 */
    motor_mstate_t m[MOTOR_MAX_MOTORS];    /**< 各电机状态 */
    int            motor_count;            /**< 有效电机数 */

    uint64_t now;             /**< 本拍时钟（ms） */
    uint64_t last_tick_ms;    /**< 上一拍 tick 时刻 */
    bool     last_tick_valid; /**< 是否已有有效上一拍 */
    bool     estop_latched;   /**< 本实例急停闩锁 */
    bool     safe_latched;    /**< 看门狗安全闩锁 */

    motor_event_slot_t ev[MOTOR_MAX_MOTORS]; /**< 各电机事件队列 */

    pthread_mutex_t lock;      /**< 命令与 tick 串行化 */
    bool            lock_ready;/**< 互斥量是否已初始化 */
} motor_executor_t;

/* ------------------------- 数值工具 ------------------------- */

/**
 * @brief  64 位有符号绝对值
 * @param  v  输入值
 * @return 绝对值
 */
int64_t motor_iabs64(int64_t v);

/**
 * @brief  判断两位置是否在给定距离容差内
 * @param  left     左界
 * @param  right    右界
 * @param  distance 容差（脉冲）
 * @return true 在容差内
 */
bool within_distance(int64_t left, int64_t right, int distance);

/**
 * @brief  带下界保护的减法（用于正向到位下界）
 * @param  value  基准值
 * @param  margin 减量
 * @return value - margin，不越过 INT64_MIN
 */
int64_t lower_bound(int64_t value, int margin);

/**
 * @brief  带上界保护的加法（用于反向到位上界）
 * @param  value  基准值
 * @param  margin 增量
 * @return value + margin，不越过 INT64_MAX
 */
int64_t upper_bound(int64_t value, int margin);

/* ------------------------- 并发 ------------------------- */

/** @brief 初始化执行器递归互斥量。 */
void motor_lock_init(motor_executor_t *e);

/** @brief 销毁执行器互斥量。 */
void motor_lock_destroy(motor_executor_t *e);

/** @brief 加锁（命令与 tick 路径）。 */
void motor_lock(motor_executor_t *e);

/** @brief 解锁。 */
void motor_unlock(motor_executor_t *e);

/* ------------------------- 端口封装 ------------------------- */

/**
 * @brief  取逻辑电机绑定的物理驱动器
 * @param  e  执行器
 * @param  i  电机索引
 * @return 驱动器端口；配置非法时行为由调用方保证索引合法
 */
motor_driver_t *motor_drv(motor_executor_t *e, int i);

/** @brief 调用驱动器 set_output。 */
sw_err_t drv_set_output(motor_driver_t *d, motor_speed_t speed, motor_dir_t dir);

/** @brief 调用驱动器 cutoff。 */
sw_err_t drv_cutoff(motor_driver_t *d);

/** @brief 调用驱动器 request_stop；未实现时返回 SW_ERR_NOT_INIT。 */
sw_err_t drv_request_stop(motor_driver_t *d);

/** @brief 调用驱动器 reset。 */
bool drv_reset(motor_driver_t *d);

/** @brief 调用驱动器 prepare；NULL 时视为 READY。 */
motor_prepare_result_t drv_prepare(motor_driver_t *d, int motor);

/** @brief 调用驱动器 poll；NULL 时视为 READY。 */
motor_prepare_result_t drv_poll(motor_driver_t *d, int motor);

/** @brief 调用驱动器 is_running。 */
bool drv_is_running(motor_driver_t *d);

/** @brief 调用驱动器 current。 */
int drv_current(motor_driver_t *d);

/** @brief 调用驱动器 temperature；未实现时返回 false。 */
bool drv_temperature(motor_driver_t *d, int *out);

/** @brief 调用驱动器 voltage；未实现时返回 false。 */
bool drv_voltage(motor_driver_t *d, int *out);

/** @brief 调用驱动器 status；NULL 时返回 MOTOR_PORT_OK。 */
motor_port_status_t drv_status(motor_driver_t *d);

/** @brief 读取编码器 raw。 */
int64_t enc_raw(motor_encoder_t *e);

/** @brief 同步清零编码器硬件计数。 */
bool enc_zero(motor_encoder_t *e);

/**
 * @brief  取电机编码器端口
 * @param  e  执行器
 * @param  i  电机索引
 * @return 编码器端口；无编码器时为 NULL
 */
motor_encoder_t *motor_enc(motor_executor_t *e, int i);

/**
 * @brief  读取硬限位/原点
 * @param  e  执行器
 * @param  i  电机索引
 * @param  k  限位种类
 * @return true 开关有效（触发行程端/原点）
 */
bool sensor_limit(motor_executor_t *e, int i, motor_limit_kind_t k);

/** @brief 读取执行器绑定的时钟 now_ms。 */
uint64_t clock_now(motor_executor_t *e);

/* ------------------------- 命令结果构造 ------------------------- */

/**
 * @brief  构造非拒绝命令结果
 * @param  st      受理状态
 * @param  reason  说明（静态字符串）
 */
motor_cmd_result_t cmd_make(motor_cmd_status_t st, const char *reason);

/**
 * @brief  构造拒绝命令结果
 * @param  reject  拒绝码
 * @param  reason  说明（静态字符串）
 */
motor_cmd_result_t cmd_reject(motor_cmd_reject_t reject, const char *reason);

/* ------------------------- 状态机推进 ------------------------- */

/**
 * @brief  离开 RUNNING 时冻结已运行时长
 * @note   非 RUNNING 时为空操作；可安全重复调用。
 */
void settle_elapsed(motor_executor_t *e, int i);

/**
 * @brief  向指定电机事件槽推送终止/告警事件
 * @param  e    执行器
 * @param  i    电机索引
 * @param  t    事件类型
 * @param  trig 触发条件
 * @param  fc   故障码
 */
void push_event(motor_executor_t       *e,
                int                     i,
                motor_event_type_t      t,
                motor_end_condition_t   trig,
                motor_exec_fault_code_t fc);

/**
 * @brief  锁存运动目标并由状态机收敛
 * @note   调用方不必按当前状态选择不同命令。
 */
motor_cmd_result_t apply_goal(motor_executor_t *e, int i, const motor_pending_cmd_t *pc);

/**
 * @brief  当前电机是否满足互锁（MUTEX / 前置位置）
 * @note   仅启动路径使用；运行中调速不查互锁。
 */
bool interlock_ok(motor_executor_t *e, int i);

/**
 * @brief  运动到位或超时后的统一收尾
 * @param  type  结局事件类型
 * @param  trig  触发条件
 */
void complete_move(motor_executor_t *e, int i, motor_event_type_t type, motor_end_condition_t trig);

/**
 * @brief  触原点后建立可信位置基准（增量轴清零，绝对轴仅标可信）
 * @return true 基准已可信；false 清零失败
 */
bool zero_encoder_baseline(motor_executor_t *e, int i);

/** @brief 单拍 tick：看门狗、急停、各轴状态迁移与硬件输出。 */
void motor_tick(motor_executor_t *e);

/**
 * @brief  构造初始化失败结果
 * @param  msg  失败原因（静态字符串）
 */
motor_init_result_t init_err(const char *msg);

/**
 * @brief  首次装载配置与端口并归零状态
 * @param  e     执行器对象
 * @param  cfg   配置（按值存入 e）
 * @param  ports 端口表（按值存入 e）
 */
motor_init_result_t motor_init(motor_executor_t *e, const motor_config_t *cfg, const motor_ports_t *ports);

/**
 * @brief  沿用已存 cfg/ports 重新初始化（致命故障后恢复）
 */
motor_init_result_t motor_reinit(motor_executor_t *e);

/* ------------------------- 命令（motor_exec_* 实现体） ------------------------- */

/**
 * @brief  启动或更新运动
 * @param  spec  NULL 为连续运行
 */
motor_cmd_result_t motor_run(motor_executor_t        *e,
                             int                      i,
                             motor_speed_t            spd,
                             motor_dir_t              dir,
                             const motor_move_spec_t *spec);

/** @brief 受控停止。 */
motor_cmd_result_t motor_stop(motor_executor_t *e, int i);

/** @brief 回原点（方向与慢速取自配置）。 */
motor_cmd_result_t motor_home(motor_executor_t *e, int i);

/** @brief 手动同步清零编码器。 */
motor_cmd_result_t motor_zero_encoder(motor_executor_t *e, int i);

/** @brief 显式确认位置基准可信。 */
motor_cmd_result_t motor_confirm_baseline(motor_executor_t *e, int i);

/** @brief 全局抑制已解除时，将 ESTOP 收成 STOPPED；若仍有故障码或 fatal 则回到 FAULT。 */
void motor_leave_estop_if_unheld(motor_executor_t *e);

/** @brief 清除看门狗安全闩锁（须 tick 节拍已恢复正常）。 */
void motor_reset_watchdog(motor_executor_t *e);

/** @brief 分步故障恢复。 */
motor_cmd_result_t motor_recover(motor_executor_t *e, int i, motor_exec_recovery_step_t step);

/* ------------------------- 查询（motor_exec_* 实现体） ------------------------- */

/** @brief 查询电机运行状态。 */
motor_exec_state_t motor_state(const motor_executor_t *e, int i);

/** @brief 查询逻辑位置（脉冲）。 */
int64_t motor_position(const motor_executor_t *e, int i);

/**
 * @brief  查询当前输出频率（厘赫）
 * @return RUNNING 且速度为频率时返回给定值，否则 0
 */
int motor_current_freq(const motor_executor_t *e, int i);

/** @brief 查询当前运动方向。 */
motor_dir_t motor_direction(const motor_executor_t *e, int i);

/** @brief 查询故障码。 */
motor_exec_fault_code_t motor_fault_code(const motor_executor_t *e, int i);

/**
 * @brief  查询指定故障码在该电机上是否需确认
 * @note   `DRIVER_PORT_FATAL` 恒为需确认。电机号越界时返回 true。
 */
bool motor_fault_requires_confirm(const motor_executor_t *e, int i, motor_exec_fault_code_t code);

/** @brief 查询位置基准是否可信。 */
bool motor_baseline_trusted(const motor_executor_t *e, int i);

/** @brief 查询编码器是否健康。 */
bool motor_encoder_healthy(const motor_executor_t *e, int i);

/** @brief 查询是否处于看门狗安全闩锁。 */
bool motor_in_safe_state(const motor_executor_t *e);

#endif /* DOMAIN_MECHANISM_MOTOR_MOTOR_EXECUTOR_INTERNAL_H */
