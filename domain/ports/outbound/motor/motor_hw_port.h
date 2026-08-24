/**
 * @file    motor_hw_port.h
 * @brief   电机硬件出站端口：驱动器、编码器与限位。
 *
 * 领域执行器经这些函数指针表访问硬件；项目 wiring / vendor adapter
 * 只负责把具体驱动绑定进来。机构模式（motor_axis）不包含本头。
 * 急停输入不在本表：电机与流体共用 `safety_output_hold`。
 */
#ifndef DOMAIN_PORTS_OUTBOUND_MOTOR_MOTOR_HW_PORT_H
#define DOMAIN_PORTS_OUTBOUND_MOTOR_MOTOR_HW_PORT_H

#include "common/sw_error.h"
#include "domain/ports/outbound/motor/motor_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * 选填（可置 NULL，采用默认行为）：request_stop(默认 cutoff)、prepare(默认 READY)、
 * poll(默认 READY)、temperature(默认不支持)、voltage(默认不支持)、status(默认 OK)。
 *
 * @note prepare/poll 的 motor 为逻辑电机索引，便于共享驱动区分路径。
 *       poll 只维持驱动器侧不变量（如接触器路径），不得改写速度给定；
 *       is_running 只回答功率级是否在转；status 只回答端口是否致命。
 */
typedef struct {
    sw_err_t (*set_output)(void *ctx, motor_speed_t speed, motor_dir_t dir); /**< 速度给定+方向 */
    sw_err_t (*cutoff)(void *ctx);                                           /**< 立即切断输出 */
    sw_err_t (*request_stop)(void *ctx);                     /**< 受控停止；可为 NULL，此时 Stop 退化为 cutoff */
    bool (*reset)(void *ctx);                                /**< 驱动器侧故障复位，false=失败 */
    motor_prepare_result_t (*prepare)(void *ctx, int motor); /**< 启动前预备；可为 NULL */
    motor_prepare_result_t (*poll)(void *ctx, int motor);    /**< RUNNING 每拍巡检；可为 NULL */
    bool (*is_running)(void *ctx);                           /**< 功率级运行反馈 */
    int (*current)(void *ctx);                               /**< 负载电流（与阈值同量纲） */
    bool (*temperature)(void *ctx, int *out);                /**< 可选温度；可为 NULL */
    bool (*voltage)(void *ctx, int *out);                    /**< 可选母线电压；可为 NULL */
    motor_port_status_t (*status)(void *ctx);                /**< 端口层健康；可为 NULL */
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
    bool (*limit)(void *ctx, int motor, motor_limit_kind_t kind); /**< 返回开关状态 */
    void *ctx;
} motor_sensors_t;

/**
 * @brief 端口集合。drivers/encoders 为指针数组。
 * @note encoders 数组长度必须等于电机数，无编码器的电机对应项置 NULL。
 */
typedef struct {
    const motor_clock_t    *clock;    /**< 时间源，必填 */
    motor_driver_t *const  *drivers;  /**< 驱动器指针数组，长度=cfg.driver_count */
    motor_encoder_t *const *encoders; /**< 编码器指针数组，长度=电机数，无则填 NULL */
    const motor_sensors_t  *sensors;  /**< 限位采集，必填 */
} motor_ports_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_MOTOR_MOTOR_HW_PORT_H */
