/**
 * @file    fan.h
 * @brief   风机领域层接口。
 *
 * 通过注入的 IO 操作回调控制风机变频器的启动/停止/复位，
 * 以及读取报警反馈，不依赖具体 HAL 实现。
 *
 * 工作流程：
 *   启动 — 激活 FAN_START DO，变频器立即投入运行；
 *   停止 — 断开 FAN_START DO；
 *   报警 — tick() 检测到 FAN_ALARM DI 后自动停机并进入 FAULT；
 *   复位 — fan_reset() 激活 FAN_RESET DO 并保持 reset_pulse_ms，
 *           脉冲结束后若报警已消则恢复 IDLE，否则继续停留 FAULT。
 *
 * 调用方须以固定节拍调用 fan_tick()（推荐 20ms）。
 */
#ifndef DOMAIN_DEVICE_MECHANISM_FAN_H
#define DOMAIN_DEVICE_MECHANISM_FAN_H

#include "common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- 类型定义 ----------------------- */

/**
 * @brief 风机状态。
 *
 * 状态转移：
 *   IDLE ──(fan_start)──► RUNNING
 *   RUNNING ──(fan_stop)──► IDLE
 *   IDLE/RUNNING ──(FAN_ALARM 触发)──► FAULT
 *   FAULT ──(fan_reset)──► RESETTING ──(脉冲结束且报警消)──► IDLE
 *   FAULT ──(fan_reset)──► RESETTING ──(脉冲结束但报警未消)──► FAULT
 */
typedef enum {
    FAN_STATE_IDLE = 0,  /**< 空闲：FAN_START 断开 */
    FAN_STATE_RUNNING,   /**< 运行中：FAN_START 激活 */
    FAN_STATE_RESETTING, /**< 复位中：FAN_RESET 脉冲激活，等待脉冲超时 */
    FAN_STATE_FAULT,     /**< 故障：FAN_ALARM 触发，须调用 fan_reset() */
} fan_state_t;

/**
 * @brief 风机 IO 操作回调，由机型适配层实现并注入。
 *
 * 所有回调均为非阻塞调用，仅驱动 DO 输出或读取 DI 状态。
 */
typedef struct {
    /** @brief 控制风机启动 DO，on=true 激活。必填。 */
    sw_err_t (*set_start)(void *ctx, bool on);
    /** @brief 控制变频器复位 DO，on=true 激活。必填。 */
    sw_err_t (*set_reset)(void *ctx, bool on);
    /** @brief 读取变频器报警 DI，报警触发返回 true。必填。 */
    bool (*read_alarm)(void *ctx);
    /** @brief 透传给所有回调的上下文指针。 */
    void *ctx;
} fan_io_ops_t;

/**
 * @brief 风机时序配置，由机型适配层注入。
 */
typedef struct {
    uint32_t reset_pulse_ms; /**< FAN_RESET DO 激活持续时间（ms），须大于 0 */
} fan_cfg_t;

/* ----------------------- 公共 API ----------------------- */

/**
 * @brief 初始化风机模块并注入 IO 回调与配置。
 *
 * @param ops  IO 操作回调，所有函数指针均不得为 NULL。
 * @param cfg  配置参数，不得为 NULL，reset_pulse_ms 须大于 0。
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法。
 */
sw_err_t fan_init(const fan_io_ops_t *ops, const fan_cfg_t *cfg);

/**
 * @brief 启动风机（同步激活 FAN_START DO）。
 *
 * @return SW_OK        成功。
 *         SW_ERR_NOT_INIT 模块未初始化。
 *         SW_ERR_STATE    当前处于故障或复位态。
 */
sw_err_t fan_start(void);

/**
 * @brief 停止风机（同步断开 FAN_START DO）。
 *
 * @return SW_OK        成功。
 *         SW_ERR_NOT_INIT 模块未初始化。
 *         SW_ERR_STATE    当前处于故障或复位态。
 */
sw_err_t fan_stop(void);

/**
 * @brief 触发变频器复位脉冲（须在 FAULT 态调用，异步）。
 *
 * 激活 FAN_RESET DO，持续 reset_pulse_ms 后由 fan_tick() 自动断开。
 * 脉冲结束时若报警已消则恢复 IDLE，否则继续停留 FAULT。
 *
 * @return SW_OK        脉冲已触发。
 *         SW_ERR_NOT_INIT 模块未初始化。
 *         SW_ERR_STATE    当前不处于故障态。
 */
sw_err_t fan_reset(void);

/**
 * @brief 风机模块周期处理，须以固定节拍调用（推荐 20ms）。
 *
 * 检测 FAN_ALARM DI 和复位脉冲超时，推进状态机。
 */
void fan_tick(void);

/**
 * @brief 查询风机当前状态。
 * @return 当前 fan_state_t。
 */
fan_state_t fan_state(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MECHANISM_FAN_H */
