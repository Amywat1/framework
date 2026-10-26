/**
 * @file    drv_voice.h
 * @brief   语音模块驱动接口（Modbus RTU，按需通讯，无后台轮询）
 * @author  HUWANGWEI
 * @date    2026-06-29
 *
 * @note    驱动层只负责寄存器读写与通信状态管理，不含项目业务语义。
 *          寄存器布局随语音模块型号变化，由项目以 drv_voice_modbus_profile_t 注入；
 *          业务语义到曲目编号的映射由项目侧完成。
 *          通信失败/恢复事件在操作调用点同步触发，不依赖后台线程。
 */

#ifndef ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VOICE_H
#define ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VOICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/ports/outbound/hal/hal_voice_port.h"

#include <stdint.h>

/**
 * @brief 语音模块 Modbus profile，由项目配置层按模块型号定义并以静态只读对象注入。
 * @note  动作寄存器（音量加减 / 暂停 / 停止）统一写入 trigger 触发。
 */
typedef struct {
    uint16_t reg_volume;   /**< 绝对音量寄存器 */
    uint16_t reg_play;     /**< 播放寄存器（写入曲目编号） */
    uint16_t reg_vol_up;   /**< 音量增加寄存器 */
    uint16_t reg_vol_down; /**< 音量减小寄存器 */
    uint16_t reg_pause;    /**< 暂停寄存器 */
    uint16_t reg_stop;     /**< 停止播放并清空列表寄存器 */
    uint16_t trigger;      /**< 动作寄存器触发值 */
} drv_voice_modbus_profile_t;

/** @brief 语音驱动不透明句柄，真实存储仅由 Snack provider 内部持有。 */
typedef struct drv_voice drv_voice_t;

/**
 * @brief  初始化语音模块实例，建立 Modbus 上下文
 * @param[in]  v            provider 内部持有的语音句柄，不可为 NULL
 * @param[in]  serial_port  Modbus RTU 串口路径（如 "/dev/ttyS1"），不可为 NULL
 * @param[in]  baud         串口波特率
 * @param[in]  modbus_addr  Modbus 从站地址（1~247）
 * @param[in]  profile      项目侧注入的模块寄存器 profile，生命周期须覆盖实例，不可为 NULL
 * @retval     SW_OK        初始化成功
 * @retval     SW_ERR_PARAM v / serial_port / profile 为 NULL，或 modbus_addr 非法
 * @retval     SW_ERR_HW    mutex 初始化失败或 Modbus 上下文创建失败
 * @note   Modbus connect 失败不中止 init（defer-link），后续操作自动重连
 */
sw_err_t drv_voice_init(drv_voice_t                      *v,
                        const char                       *serial_port,
                        int                               baud,
                        int                               modbus_addr,
                        const drv_voice_modbus_profile_t *profile);

/**
 * @brief  播放指定曲目
 * @param[in]  v      已完成 drv_voice_init 的实例
 * @param[in]  track  曲目编号（写入模块播放寄存器的原始值），由项目侧映射
 * @retval     SW_OK           发送成功
 * @retval     SW_ERR_NOT_INIT 实例未初始化
 * @retval     SW_ERR_COMM     Modbus 写操作失败
 */
sw_err_t drv_voice_play(drv_voice_t *v, uint16_t track);

/**
 * @brief  停止播放并清空播放列表
 * @param[in]  v  已完成 drv_voice_init 的实例
 * @retval     SW_OK / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_voice_stop(drv_voice_t *v);

/**
 * @brief  暂停当前播放
 * @param[in]  v  已完成 drv_voice_init 的实例
 * @retval     SW_OK / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_voice_pause(drv_voice_t *v);

/**
 * @brief  设置模块绝对音量
 * @param[in]  v    已完成 drv_voice_init 的实例
 * @param[in]  vol  音量值（写入模块音量寄存器的原始值，范围由模块规格决定）
 * @retval     SW_OK / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_voice_set_volume(drv_voice_t *v, uint16_t vol);

/**
 * @brief  音量增加一级（模块内部步进）
 * @param[in]  v  已完成 drv_voice_init 的实例
 * @retval     SW_OK / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_voice_volume_up(drv_voice_t *v);

/**
 * @brief  音量减小一级（模块内部步进）
 * @param[in]  v  已完成 drv_voice_init 的实例
 * @retval     SW_OK / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_voice_volume_down(drv_voice_t *v);

/**
 * @brief  注册事件回调，驱动在通信状态变化时调用
 * @param[in]  v   实例指针
 * @param[in]  cb  回调函数，传入事件码（HAL_VOICE_EVT_*）；传 NULL 可注销
 * @note   回调在各操作函数的调用线程中触发，须保证线程安全；
 *         回调内禁止反向调用本驱动写接口（死锁风险）
 */
void drv_voice_register_event_cb(drv_voice_t *v, void (*cb)(int event_code));

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VOICE_H */
