/**
 * @file    drv_vfd.h
 * @brief   士林变频器驱动接口（Modbus RTU + IO 数字量控制，handle 参数化）
 * @author  HUWANGWEI
 * @date    2026-04-08
 *
 * @note    驱动层只描述"士林 VFD 的通信协议和 IO 控制方式"，
 *          不包含任何业务机构名称（刷子/龙门）。
 *          调用方（bsp_hal）负责持有实例并提供具体引脚配置。
 */

#ifndef DRV_VFD_H
#define DRV_VFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_types.h"
#include "common/sw_error.h"
#include "driver/drv_io.h"
#include "modbus/modbus.h"

/* -------------------------------------------------------------------------
 * VFD 运行状态
 * ------------------------------------------------------------------------- */
typedef enum
{
    DRV_VFD_STATE_STOPPED = 0,
    DRV_VFD_STATE_FWD,
    DRV_VFD_STATE_REV,
    DRV_VFD_STATE_FAULT,
} drv_vfd_state_t;

/* -------------------------------------------------------------------------
 * VFD 实例句柄（由调用方以静态方式分配，调用 drv_vfd_init 前清零）
 * ------------------------------------------------------------------------- */
typedef struct
{
    modbus_t        *mb;
    drv_io_do_t      pin_fwd;           /* 正转 IO 引脚 */
    drv_io_do_t      pin_rev;           /* 反转 IO 引脚（has_rev=false 时忽略）*/
    bool             has_rev;           /* 是否支持反转 */
    drv_io_do_t      pin_rst;           /* 故障复位 IO 引脚 */
    drv_vfd_state_t  state;
    void           (*event_cb)(int event_code);
} drv_vfd_t;

/* -------------------------------------------------------------------------
 * 接口声明
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化 VFD 实例（建立 Modbus 连接，设置 IO 引脚至安全状态）
 * @param  vfd          调用方提供的静态句柄（不可为 NULL）
 * @param  serial_port  串口设备路径（如 "/dev/ttyS1"）
 * @param  baud         波特率（如 9600）
 * @param  modbus_addr  Modbus 从机地址
 * @param  pin_fwd      正转控制 IO 引脚
 * @param  has_rev      true=支持反转，false=仅正转
 * @param  pin_rev      反转控制 IO 引脚（has_rev=false 时传入任意值）
 * @param  pin_rst      故障复位 IO 引脚
 * @retval SW_OK / SW_ERR_HW
 */
sw_err_t drv_vfd_init(drv_vfd_t   *vfd,
                      const char  *serial_port,
                      int          baud,
                      int          modbus_addr,
                      drv_io_do_t  pin_fwd,
                      bool         has_rev,
                      drv_io_do_t  pin_rev,
                      drv_io_do_t  pin_rst);

/**
 * @brief  设定频率并正转启动
 * @param  vfd      VFD 句柄
 * @param  freq_hz  目标频率（0.01Hz，如 5000=50.00Hz）
 * @retval SW_OK / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_vfd_run_fwd(drv_vfd_t *vfd, uint16_t freq_hz);

/**
 * @brief  设定频率并反转启动
 * @param  vfd      VFD 句柄
 * @param  freq_hz  目标频率
 * @retval SW_OK / SW_ERR_NOT_INIT / SW_ERR_COMM / SW_ERR_PARAM（不支持反转）
 */
sw_err_t drv_vfd_run_rev(drv_vfd_t *vfd, uint16_t freq_hz);

/**
 * @brief  停止输出（VFD 减速停车）
 * @retval SW_OK
 */
sw_err_t drv_vfd_stop(drv_vfd_t *vfd);

/**
 * @brief  VFD 故障复位（复位脉冲 200ms）
 * @retval SW_OK
 */
sw_err_t drv_vfd_fault_reset(drv_vfd_t *vfd);

/**
 * @brief  读取当前运行状态
 */
drv_vfd_state_t drv_vfd_get_state(const drv_vfd_t *vfd);

/**
 * @brief  读取 VFD 故障码（Modbus 寄存器 0x2102）
 * @param  vfd     VFD 句柄
 * @param  p_code  输出故障码（0=无故障；Modbus 通信失败时保持原值不变）
 * @retval SW_OK / SW_ERR_COMM（通信失败，*p_code 不可信，旧实现用 0 掩盖此错误）
 */
sw_err_t drv_vfd_get_fault_code(drv_vfd_t *vfd, uint16_t *p_code);

/**
 * @brief  注册 VFD 事件回调（故障/异常时通知上层）
 * @param  cb  回调函数：参数为事件码
 */
void drv_vfd_register_event_cb(drv_vfd_t *vfd, void (*cb)(int event_code));

#ifdef __cplusplus
}
#endif

#endif /* DRV_VFD_H */
