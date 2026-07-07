/**
 * @file    drv_vfd.h
 * @brief   变频器驱动接口（Modbus RTU + IO 数字量原语）
 * @author  HUWANGWEI
 * @date    2026-04-08
 *
 * @note    驱动层只描述 VFD 的 Modbus 同步读写与 IO 引脚/挡位组合输出，
 *          不包含复位脉冲、正反向切换等待、周期采样或事件上报。
 *          时序与通信监测由上层 generic/hal_vfd 承接。
 *          各厂家寄存器地址在 drv_vfd.c 内以宏区分。
 *          共用同一 serial_port 的实例在 drv 内自动共享 Modbus 互斥锁。
 *
 * 速度控制模型：
 *   - 两路速度 IO（pin_spd1/pin_spd2）组合最多 3 个有效速度挡；
 *     (spd1=0, spd2=0) 保留为停止态，不可作为速度挡。
 *   - 挡位值：正=正转，负=反转，0=停止；绝对值为速度挡（1~VFD_GEAR_MAX）。
 *   - 频率通过 Modbus 单独设置（drv_vfd_write REG_FREQ），与挡位 IO 控制相互独立。
 */

#ifndef DRV_VFD_H
#define DRV_VFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/adapters/outbound/hal/providers/snack/modbus/drv_modbus_link.h"
#include "framework/common/io_handle.h"
#include "framework/common/sw_error.h"
#include "framework/common/vfd_types.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 挡位类型与宏
 * 正值=正转，负值=反转，0=停止；绝对值为速度挡位（1=最低，VFD_GEAR_MAX=最高）
 * 两路速度 IO 可组合 3 个有效挡（(0,0) 保留为停止态）
 * ------------------------------------------------------------------------- */
typedef hal_vfd_gear_t drv_vfd_gear_t;

#define VFD_GEAR_STOP  ((drv_vfd_gear_t)0)
#define VFD_GEAR_FWD_1 ((drv_vfd_gear_t)1)
#define VFD_GEAR_FWD_2 ((drv_vfd_gear_t)2)
#define VFD_GEAR_FWD_3 ((drv_vfd_gear_t)3)
#define VFD_GEAR_REV_1 ((drv_vfd_gear_t) - 1)
#define VFD_GEAR_REV_2 ((drv_vfd_gear_t) - 2)
#define VFD_GEAR_REV_3 ((drv_vfd_gear_t) - 3)
#define VFD_GEAR_MAX   3 /* 最大有效速度挡数（两路 IO 去除停止态后的可用组合数）*/

/**
 * 速度挡位 IO 状态辅助宏：bit0=spd1 电平，bit1=spd2 电平
 * 禁止使用 VFD_SPD_IO(0,0)，该组合保留为停止态
 */
#define VFD_SPD_IO(s1, s2) ((uint8_t)(((s2) ? 0x02U : 0U) | ((s1) ? 0x01U : 0U)))

typedef hal_vfd_state_t drv_vfd_state_t;

/* drv_vfd_reg_t 是 hal_vfd_reg_t 的驱动层别名；DRV_VFD_REG_* 与 HAL_VFD_REG_* 等价 */
typedef hal_vfd_reg_t drv_vfd_reg_t;
#define DRV_VFD_REG_STATE       HAL_VFD_REG_STATE
#define DRV_VFD_REG_FAULT_CODE  HAL_VFD_REG_FAULT_CODE
#define DRV_VFD_REG_CURRENT     HAL_VFD_REG_CURRENT
#define DRV_VFD_REG_FREQ        HAL_VFD_REG_FREQ
#define DRV_VFD_REG_CLEAR_FAULT HAL_VFD_REG_CLEAR_FAULT

/** DO 写回调类型，由 hal_vfd_linux 注入，用于驱动操作底层引脚 */
typedef sw_err_t (*drv_vfd_do_set_fn)(io_do_t pin, bool val);

/**
 * VFD 实例句柄，由调用方分配静态或全局存储，通过指针传入各接口。
 * gear 字段标注"内部"者，外部代码只读，禁止直接修改。
 */
typedef struct {
    drv_modbus_link_t link;                 /* Modbus RTU 链路（连接/总线锁/失败重连） */
    io_do_t        pin_fwd;
    io_do_t        pin_rev;                 /* IO_HANDLE_NULL 表示不支持反转 */
    io_do_t        pin_rst;
    io_do_t        pin_spd1;                /* 速度 IO1；IO_HANDLE_NULL 表示未配置 */
    io_do_t        pin_spd2;                /* 速度 IO2；IO_HANDLE_NULL 表示未配置 */
    uint8_t        spd_cfg[VFD_GEAR_MAX];   /* 挡位 1~3 对应 IO 状态，由 drv_vfd_config_speed_io 写入 */
    bool           spd_io_ready;            /* 内部：drv_vfd_config_speed_io 已完成配置 */
    drv_vfd_gear_t gear;                    /* 内部：当前已应用到硬件的挡位，0=停止 */
    drv_vfd_do_set_fn do_set;
    pthread_mutex_t io_mutex;               /* 内部：保护 gear 与 IO 写操作 */
} drv_vfd_t;

/**
 * @brief  初始化 VFD 实例，建立 Modbus 上下文并将所有控制 DO 置为安全低态
 * @param[in]  vfd          VFD 实例指针，由调用方提供存储，不可为 NULL
 * @param[in]  serial_port  Modbus RTU 串口路径（如 "/dev/ttyS0"），不可为 NULL
 * @param[in]  baud         串口波特率
 * @param[in]  modbus_addr  Modbus 从站地址（1~247）
 * @param[in]  pin_fwd      正转控制 DO 引脚
 * @param[in]  pin_rev      反转控制 DO 引脚；IO_HANDLE_NULL 表示该 VFD 不支持反转
 * @param[in]  pin_rst      故障复位 DO 引脚
 * @param[in]  do_set       DO 写回调，不可为 NULL
 * @retval     SW_OK        初始化成功
 * @retval     SW_ERR_PARAM vfd / serial_port / do_set 为 NULL
 * @retval     SW_ERR_HW    资源申请失败（串口表已满、mutex 初始化失败）
 * @note   Modbus 连接失败时不中止 init，后续读写会自动重连（defer-link）；
 *         速度 IO 须单独调用 drv_vfd_config_speed_io 配置
 */
sw_err_t drv_vfd_init(drv_vfd_t        *vfd,
                      const char       *serial_port,
                      int               baud,
                      int               modbus_addr,
                      io_do_t           pin_fwd,
                      io_do_t           pin_rev,
                      io_do_t           pin_rst,
                      drv_vfd_do_set_fn do_set);

/**
 * @brief  配置速度 IO 引脚与挡位映射，必须在首次调用 drv_vfd_apply_gear 非停止挡前完成
 * @param[in]  vfd       已完成 drv_vfd_init 的 VFD 实例
 * @param[in]  pin_spd1  速度 IO1，不可为 IO_HANDLE_NULL
 * @param[in]  pin_spd2  速度 IO2，不可为 IO_HANDLE_NULL
 * @param[in]  spd_cfg   长度为 VFD_GEAR_MAX 的挡位映射数组；
 *                       spd_cfg[0..2] 依次对应挡位 1~3，
 *                       每元素用 VFD_SPD_IO(s1,s2) 填写，禁止使用 VFD_SPD_IO(0,0)
 * @retval     SW_OK        配置成功，速度 IO 已拉低至安全态
 * @retval     SW_ERR_PARAM 参数非法（vfd/spd_cfg 为 NULL、引脚为 IO_HANDLE_NULL、
 *                          任一挡位映射值为 0x00）
 * @retval     SW_ERR_NOT_INIT  vfd 未完成初始化
 */
sw_err_t drv_vfd_config_speed_io(drv_vfd_t    *vfd,
                                 io_do_t       pin_spd1,
                                 io_do_t       pin_spd2,
                                 const uint8_t spd_cfg[VFD_GEAR_MAX]);

/**
 * @brief  立即将挡位应用到硬件 IO（正值正转，负值反转，0 停止）
 * @param[in]  vfd   已完成 drv_vfd_init 的 VFD 实例
 * @param[in]  gear  目标挡位；范围 [-VFD_GEAR_MAX, VFD_GEAR_MAX]，0 等价于 stop_outputs
 * @retval     SW_OK            操作成功
 * @retval     SW_ERR_NOT_INIT  vfd 未初始化
 * @retval     SW_ERR_PARAM     gear 超出范围，或目标为反转但 VFD 不支持反转
 * @note   已配置速度 IO 时，非停止挡会同步应用挡位映射；未配置速度 IO 时，
 *         仅控制方向输出，适用于项目层另有高速/挡位选择 DO 的硬件模型。
 *         正反转切换立即执行，无内部延迟；换向时序由控制层负责；
 *         IO 操作顺序：速度 IO（若启用）先于方向 IO；
 *         本接口不控制 Modbus 频率，频率须单独调用 drv_vfd_write(REG_FREQ)
 */
sw_err_t drv_vfd_apply_gear(drv_vfd_t *vfd, drv_vfd_gear_t gear);

/**
 * @brief  关断所有运行输出（spd/fwd/rev），gear 置为 STOP
 * @param[in]  vfd  已完成 drv_vfd_init 的 VFD 实例
 * @retval     SW_OK           操作成功
 * @retval     SW_ERR_NOT_INIT vfd 未初始化
 */
sw_err_t drv_vfd_stop_outputs(drv_vfd_t *vfd);

/**
 * @brief  设置故障复位引脚电平（仅 IO 原语，不做脉冲时序）
 * @param[in]  vfd    已完成 drv_vfd_init 的 VFD 实例
 * @param[in]  level  true=拉高，false=拉低
 * @retval     SW_OK           操作成功
 * @retval     SW_ERR_NOT_INIT vfd 未初始化
 * @retval     SW_ERR_PARAM    pin_rst 未配置（IO_HANDLE_NULL）
 */
sw_err_t drv_vfd_set_rst(drv_vfd_t *vfd, bool level);

/**
 * @brief  获取当前运行状态，从 gear 字段派生
 * @param[in]  vfd  VFD 实例指针，可为 NULL（返回 STOPPED）
 * @retval     HAL_VFD_STATE_FWD     gear > 0（正转中）
 * @retval     HAL_VFD_STATE_REV     gear < 0（反转中）
 * @retval     HAL_VFD_STATE_STOPPED gear == 0 或 vfd 为 NULL
 */
drv_vfd_state_t drv_vfd_get_state(drv_vfd_t *vfd);

/**
 * @brief  同步读取寄存器（发起 Modbus IO）
 * @param[in]  vfd  已初始化的 VFD 实例
 * @param[in]  reg  支持 STATE / FAULT_CODE / CURRENT；FREQ / CLEAR_FAULT 不可读
 * @param[out] p_val 输出值，不可为 NULL
 * @retval  SW_OK / SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_vfd_read(drv_vfd_t *vfd, drv_vfd_reg_t reg, uint16_t *p_val);

/**
 * @brief  写寄存器（发起 Modbus IO）
 * @param[in]  vfd  已初始化的 VFD 实例
 * @param[in]  reg  FREQ：须厂商定义 VFD_REG_FREQ_SET；
 *                  CLEAR_FAULT：须厂商定义 VFD_REG_CLEAR_FAULT，val 参数忽略
 * @param[in]  val  写入值（CLEAR_FAULT 时忽略，数据由厂商宏定义决定）
 * @retval  SW_OK / SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_vfd_write(drv_vfd_t *vfd, drv_vfd_reg_t reg, uint16_t val);

#ifdef __cplusplus
}
#endif

#endif /* DRV_VFD_H */
