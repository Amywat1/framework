/**
 * @file    drv_modbus_link.h
 * @brief   Modbus RTU 链路层（连接管理 + 总线锁共享 + 失败重连），与具体设备无关
 * @author  HUWANGWEI
 * @date    2026-07-07
 *
 * @note    本文件只处理"怎么把字节收发到 Modbus RTU 总线上"，不含任何设备寄存器
 *          地址、业务语义或通信状态的对外通知；这些留给 drv_vfd/drv_voice 等
 *          具体驱动各自实现。
 *          同一 serial_port 上的多个实例在 drv_modbus_link_init() 内部自动共享
 *          一把总线互斥锁（按 serial_port 字符串匹配、ref-count 管理）。
 *          comm_fail_count 仅用于内部触发自动重连，不含"通信丢失/恢复"这类业务
 *          通知语义，调用方需要该语义时须在其上再做一层判定（如 drv_voice 的
 *          notify_fail_count）。
 */

#ifndef ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_MODBUS_LINK_H
#define ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_MODBUS_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Modbus RTU 链路不透明句柄，真实存储仅由 Snack provider 内部持有。 */
typedef struct drv_modbus_link drv_modbus_link_t;

/** @brief Modbus 寄存器读取类型。 */
typedef enum {
    DRV_MODBUS_REG_HOLDING = 0, /**< 保持寄存器，功能码 0x03。 */
    DRV_MODBUS_REG_INPUT,       /**< 输入寄存器，功能码 0x04。 */
} drv_modbus_reg_type_t;

/**
 * @brief  初始化 Modbus RTU 链路：绑定/创建总线锁，建立连接
 * @param[in]  link                 provider 内部持有的链路句柄，不可为 NULL
 * @param[in]  serial_port          Modbus RTU 串口路径（如 "/dev/ttyS0"），不可为 NULL
 * @param[in]  baud                 串口波特率
 * @param[in]  modbus_addr          Modbus 从站地址
 * @param[in]  timeout_us           单次 Modbus 响应超时（us）
 * @param[in]  reconnect_threshold  连续失败达到该次数后自动重建连接
 * @retval     SW_OK        初始化成功
 * @retval     SW_ERR_PARAM link / serial_port 为 NULL
 * @retval     SW_ERR_HW    总线锁表已满或 Modbus 上下文创建失败
 * @note   Modbus 连接失败时不中止 init（defer-link），后续读写会自动重连
 */
sw_err_t drv_modbus_link_init(drv_modbus_link_t *link,
                              const char        *serial_port,
                              int                baud,
                              int                modbus_addr,
                              uint32_t           timeout_us,
                              uint16_t           reconnect_threshold);

/**
 * @brief  查询链路是否已完成初始化（串口/波特率/地址均有效）
 * @param[in]  link  链路句柄，可为 NULL（返回 false）
 */
bool drv_modbus_link_is_ready(const drv_modbus_link_t *link);

/**
 * @brief  同步读寄存器（发起 Modbus IO），内部处理总线锁与失败重连
 * @param[in]  link   已初始化的链路句柄
 * @param[in]  type   寄存器读取类型
 * @param[in]  addr   寄存器地址
 * @param[out] p_val  输出值，不可为 NULL
 * @retval  SW_OK / SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_modbus_link_read_reg(drv_modbus_link_t *link, drv_modbus_reg_type_t type, uint16_t addr, uint16_t *p_val);

/**
 * @brief  同步写寄存器（发起 Modbus IO），内部处理总线锁与失败重连
 * @param[in]  link  已初始化的链路句柄
 * @param[in]  addr  寄存器地址
 * @param[in]  val   写入值
 * @retval  SW_OK / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_modbus_link_write_reg(drv_modbus_link_t *link, uint16_t addr, uint16_t val);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_MODBUS_LINK_H */
