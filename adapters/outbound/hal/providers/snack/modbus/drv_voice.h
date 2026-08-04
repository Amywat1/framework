/**
 * @file    drv_voice.h
 * @brief   语音模块驱动接口（Modbus RTU，按需通讯，无后台轮询）
 * @author  HUWANGWEI
 * @date    2026-06-29
 *
 * @note    驱动层只负责寄存器读写与通信状态管理，不含项目业务语义。
 *          各项目的曲目编号映射（如"欢迎语"→track 3）由上层 HAL adapter 完成。
 *          通信失败/恢复事件在操作调用点同步触发，不依赖后台线程。
 */

#ifndef ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VOICE_H
#define ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VOICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "adapters/outbound/hal/providers/snack/modbus/drv_modbus_link.h"
#include "common/sw_error.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 事件码：通过 event_cb 上报给上层
 * ------------------------------------------------------------------------- */
#define DRV_VOICE_EVT_COMM_LOST     1 /**< 连续通信失败，判定为通信丢失 */
#define DRV_VOICE_EVT_COMM_RESTORED 2 /**< 通信恢复正常 */

/**
 * 语音模块实例句柄，由调用方分配静态或全局存储，通过指针传入各接口。
 * 以下字段标注"内部"者，外部代码只读，禁止直接修改。
 */
typedef struct {
    drv_modbus_link_t link;              /**< Modbus RTU 链路（连接/总线锁/失败重连） */
    bool              comm_ok;           /**< 内部：当前通信是否正常 */
    uint16_t          notify_fail_count; /**< 内部：连续失败计数，仅用于通信丢失/恢复通知判定 */
    pthread_mutex_t   notify_mutex;      /**< 内部：保护 comm_ok/notify_fail_count/event_cb */
    void (*event_cb)(int event_code);    /**< 事件回调，NULL 表示未注册 */
} drv_voice_t;

/**
 * @brief  初始化语音模块实例，建立 Modbus 上下文
 * @param[in]  v            语音实例指针，由调用方提供存储，不可为 NULL
 * @param[in]  serial_port  Modbus RTU 串口路径（如 "/dev/ttyS1"），不可为 NULL
 * @param[in]  baud         串口波特率
 * @param[in]  modbus_addr  Modbus 从站地址（1~247）
 * @retval     SW_OK        初始化成功
 * @retval     SW_ERR_PARAM v 或 serial_port 为 NULL，或 modbus_addr 非法
 * @retval     SW_ERR_HW    mutex 初始化失败或 Modbus 上下文创建失败
 * @note   Modbus connect 失败不中止 init（defer-link），后续操作自动重连
 */
sw_err_t drv_voice_init(drv_voice_t *v, const char *serial_port, int baud, int modbus_addr);

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
 * @param[in]  cb  回调函数，传入事件码（DRV_VOICE_EVT_*）；传 NULL 可注销
 * @note   回调在各操作函数的调用线程中触发，须保证线程安全；
 *         回调内禁止反向调用本驱动写接口（死锁风险）
 */
void drv_voice_register_event_cb(drv_voice_t *v, void (*cb)(int event_code));

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VOICE_H */
