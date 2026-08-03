/**
 * @file    hal_voice_port.h
 * @brief   语音模块 HAL 端口接口（模块对外唯一入口）
 * @author  HUWANGWEI
 * @date    2026-06-29
 *
 * @note    业务层与其它 HAL 适配器仅通过本接口访问语音模块；
 *          平台实现（snack_voice_adapter / hal_voice_sim）内部对接 drv_voice 或仿真状态。
 *          语音模块为单实例设备，ops 接口不带 id 参数。
 */

#ifndef PORTS_HAL_VOICE_PORT_H
#define PORTS_HAL_VOICE_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdint.h>

/**
 * 语音模块 HAL 操作集。
 * 曲目编号（track）为透传原始值，项目侧负责定义业务语义到编号的映射。
 */
typedef struct {
    /** @brief  初始化语音模块（建立 Modbus 连接） */
    sw_err_t (*init)(void);

    /**
     * @brief  播放指定曲目
     * @param  track  曲目编号（由项目侧映射到具体音频文件）
     */
    sw_err_t (*play)(uint16_t track);

    /** @brief  停止播放并清空播放列表 */
    sw_err_t (*stop)(void);

    /** @brief  暂停当前播放 */
    sw_err_t (*pause)(void);

    /**
     * @brief  设置绝对音量
     * @param  vol  音量值（写入模块音量寄存器的原始值，范围由模块规格决定）
     */
    sw_err_t (*set_volume)(uint16_t vol);

    /** @brief  音量增加一级（模块内部步进） */
    sw_err_t (*volume_up)(void);

    /** @brief  音量减小一级（模块内部步进） */
    sw_err_t (*volume_down)(void);

    /**
     * @brief  注册通信状态事件回调
     * @param  cb  回调函数，传入事件码（DRV_VOICE_EVT_*）；传 NULL 可注销
     */
    void (*register_event_cb)(void (*cb)(int event_code));
} hal_voice_ops_t;

/** @brief  注册语音模块 HAL 实现（由平台适配器在 wiring 阶段调用） */
sw_err_t hal_voice_register(const hal_voice_ops_t *ops);

/** @brief  获取已注册的语音模块 HAL 操作集；未注册时返回 NULL */
const hal_voice_ops_t *hal_voice_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_VOICE_PORT_H */
