/**
 * @file    drv_vfd.h
 * @brief   变频器驱动接口（Modbus RTU + IO 数字量原语）
 * @author  HUWANGWEI
 * @date    2026-04-08
 *
 * @note    驱动层只描述 VFD 的 Modbus 同步读写与 IO 引脚/挡位组合输出，
 *          不包含复位脉冲、正反向切换等待、周期采样或事件上报。
 *          时序与通信监测由上层 generic/hal_vfd 承接。
 *          厂商寄存器差异由项目侧通过只读 profile 注入。
 *          共用同一 serial_port 的实例在 drv 内自动共享 Modbus 互斥锁。
 *
 * 速度控制模型：
 *   - 可配置 0~2 路速度 IO；各挡位的 IO 组合可全低。
 *   - 挡位值：正=正转，负=反转，0=停止；绝对值为速度挡（1~VFD_GEAR_MAX）。
 *   - profile 支持频率写入时，可通过 Modbus 单独设置频率，与挡位 IO 控制相互独立。
 */

#ifndef ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VFD_H
#define ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "adapters/outbound/hal/providers/snack/modbus/drv_modbus_link.h"
#include "common/io_handle.h"
#include "common/sw_error.h"
#include "domain/ports/outbound/hal/hal_vfd_port.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 驱动层速度 IO 约束（hal_vfd_port.h 不含此硬件细节）
 * ------------------------------------------------------------------------- */
#define VFD_GEAR_MAX 3U

/** @brief 速度挡位 IO 编码：bit0=spd1，bit1=spd2。 */
#define VFD_SPD_IO(s1, s2) ((uint8_t)(((s2) ? 0x02U : 0U) | ((s1) ? 0x01U : 0U)))

/** DO 写回调类型，由上层注入，用于驱动操作底层引脚 */
typedef sw_err_t (*drv_vfd_do_set_fn)(io_do_t pin, bool val);

/** @brief VFD profile 单个 Modbus 点位的访问方式。 */
typedef enum {
    DRV_VFD_MODBUS_NONE = 0,     /**< 不支持该能力。 */
    DRV_VFD_MODBUS_READ_HOLDING, /**< 读取保持寄存器。 */
    DRV_VFD_MODBUS_READ_INPUT,   /**< 读取输入寄存器。 */
    DRV_VFD_MODBUS_WRITE_SINGLE, /**< 写单个保持寄存器。 */
} drv_vfd_modbus_access_t;

/**
 * @brief VFD profile 单个 Modbus 点位描述。
 * @note 读取时逻辑值 = 原始值 * scale_num / scale_den；频率写入时执行逆向换算。
 *       fixed_value 仅用于固定值命令，例如 Modbus 清故障。
 */
typedef struct {
    /** @brief 访问方式，NONE 表示该点位不受支持。 */
    drv_vfd_modbus_access_t access;
    /** @brief Modbus PDU 寄存器地址。 */
    uint16_t address;
    /** @brief 逻辑值换算比例的分子。 */
    uint32_t scale_num;
    /** @brief 逻辑值换算比例的分母。 */
    uint32_t scale_den;
    /** @brief 固定写命令的数据值，仅 clear_fault 使用。 */
    uint16_t fixed_value;
} drv_vfd_modbus_point_t;

/**
 * @brief VFD 厂商 Modbus profile，由项目配置层定义并以静态只读对象注入。
 * @note state / fault_code / current 必须可读；frequency / clear_fault 可为 NONE。
 */
typedef struct {
    /** @brief profile 名称，仅用于启动日志和诊断。 */
    const char *name;
    /** @brief 运行状态读取点。 */
    drv_vfd_modbus_point_t state;
    /** @brief 故障码读取点。 */
    drv_vfd_modbus_point_t fault_code;
    /** @brief 输出电流读取点，换算后单位须为 0.01A。 */
    drv_vfd_modbus_point_t current;
    /** @brief 目标频率写入点，逻辑值单位为 0.01Hz。 */
    drv_vfd_modbus_point_t frequency;
    /** @brief Modbus 清故障固定命令。 */
    drv_vfd_modbus_point_t clear_fault;
} drv_vfd_modbus_profile_t;

/** @brief VFD 驱动不透明句柄，真实存储仅由 Snack provider 内部持有。 */
typedef struct drv_vfd drv_vfd_t;

/**
 * @brief  初始化 VFD 实例，建立 Modbus 上下文并将所有控制 DO 置为安全低态
 * @param[in]  vfd          provider 内部持有的 VFD 句柄，不可为 NULL
 * @param[in]  serial_port  Modbus RTU 串口路径（如 "/dev/ttyS0"），不可为 NULL
 * @param[in]  baud         串口波特率
 * @param[in]  modbus_addr  Modbus 从站地址（1~247）
 * @param[in]  profile      项目侧注入的厂商 Modbus profile，生命周期须覆盖 VFD 实例
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
sw_err_t drv_vfd_init(drv_vfd_t                      *vfd,
                      const char                     *serial_port,
                      int                             baud,
                      int                             modbus_addr,
                      const drv_vfd_modbus_profile_t *profile,
                      io_do_t                         pin_fwd,
                      io_do_t                         pin_rev,
                      io_do_t                         pin_rst,
                      drv_vfd_do_set_fn               do_set);

/**
 * @brief  配置速度 IO 引脚与挡位映射，必须在首次调用 drv_vfd_apply_gear 非停止挡前完成
 * @param[in]  vfd       已完成 drv_vfd_init 的 VFD 实例
 * @param[in]  pin_spd1   速度 IO1，可为 IO_HANDLE_NULL
 * @param[in]  pin_spd2   速度 IO2，可为 IO_HANDLE_NULL
 * @param[in]  gear_count 有效挡位数，范围 1..VFD_GEAR_MAX
 * @param[in]  spd_cfg    挡位映射数组；全低是合法挡位输出
 * @retval     SW_OK        配置成功，速度 IO 已拉低至安全态
 * @retval     SW_ERR_PARAM 参数非法或映射引用未配置的速度 IO
 * @retval     SW_ERR_NOT_INIT  vfd 未完成初始化
 */
sw_err_t drv_vfd_config_speed_io(drv_vfd_t    *vfd,
                                 io_do_t       pin_spd1,
                                 io_do_t       pin_spd2,
                                 uint8_t       gear_count,
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
 *         本接口不控制 Modbus 频率，频率须单独调用 drv_vfd_apply_frequency
 */
sw_err_t drv_vfd_apply_gear(drv_vfd_t *vfd, hal_vfd_gear_t gear);

/**
 * @brief 通过 Modbus 写入有符号目标频率并控制方向，不修改速度 IO
 * @param[in] vfd 已初始化的 VFD 实例
 * @param[in] frequency_centi_hz 正值正转、负值反转、0 停止，绝对值单位 0.01 Hz
 * @retval SW_OK / SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_COMM / SW_ERR_HW
 */
sw_err_t drv_vfd_apply_frequency(drv_vfd_t *vfd, hal_vfd_frequency_t frequency_centi_hz);

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
 * @brief 查询实例是否配置了故障复位引脚。
 * @param[in] vfd VFD 句柄，可为 NULL。
 * @retval true  已初始化且配置了 RST 引脚。
 * @retval false 句柄无效、未初始化或未配置 RST 引脚。
 */
bool drv_vfd_has_rst_pin(const drv_vfd_t *vfd);

/**
 * @brief 查询当前 profile 是否支持 Modbus 清故障命令。
 * @param[in] vfd VFD 句柄，可为 NULL。
 * @retval true  profile 配置了写单寄存器清故障命令。
 * @retval false 句柄无效、未初始化或 profile 不支持。
 */
bool drv_vfd_has_clear_fault(const drv_vfd_t *vfd);

/**
 * @brief  获取当前运行状态，从 gear 字段派生
 * @param[in]  vfd  VFD 实例指针，可为 NULL（返回 STOPPED）
 * @retval     HAL_VFD_STATE_FWD     gear > 0（正转中）
 * @retval     HAL_VFD_STATE_REV     gear < 0（反转中）
 * @retval     HAL_VFD_STATE_STOPPED gear == 0 或 vfd 为 NULL
 */
hal_vfd_state_t drv_vfd_get_state(drv_vfd_t *vfd);

/**
 * @brief  同步读取寄存器（发起 Modbus IO）
 * @param[in]  vfd  已初始化的 VFD 实例
 * @param[in]  reg  支持 STATE / FAULT_CODE / CURRENT
 * @param[out] p_val 输出值，不可为 NULL
 * @retval  SW_OK / SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_vfd_read(drv_vfd_t *vfd, hal_vfd_reg_t reg, uint16_t *p_val);

/**
 * @brief 通过 profile 定义的固定命令清除故障。
 * @param[in] vfd 已初始化的 VFD 实例。
 * @retval SW_OK / SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_COMM
 */
sw_err_t drv_vfd_clear_fault(drv_vfd_t *vfd);

/**
 * @brief 校验 VFD Modbus profile 是否满足驱动契约。
 * @param[in] profile 待校验 profile，可为 NULL。
 * @retval true  profile 名称、必需读取点及可选写入点均有效。
 * @retval false profile 不满足契约。
 */
bool drv_vfd_profile_is_valid(const drv_vfd_modbus_profile_t *profile);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_PROVIDERS_SNACK_MODBUS_DRV_VFD_H */
