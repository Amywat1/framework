/**
 * @file    device_state.h
 * @brief   运行模式枚举定义
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef DOMAIN_DEVICE_STATE_H
#define DOMAIN_DEVICE_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  整机运行模式（OperationalMode 6 态 + 初始化态）
 */
typedef enum
{
    OP_MODE_INIT = 0,   /**< 系统初始化中（operational_mode_init 前）*/
    OP_MODE_IDLE,       /**< 待机，等待指令 */
    OP_MODE_WASHING,    /**< 洗车会话执行中 */
    OP_MODE_MANUAL,     /**< 手动维护模式 */
    OP_MODE_SELF_CHECK, /**< 完整自检模式 */
    OP_MODE_EXCEPTION,  /**< 异常停机模式 */
    OP_MODE_RECOVERING, /**< 恢复过渡状态 */
} operational_mode_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_STATE_H */
