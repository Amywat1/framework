/**
 * @file    brush.h
 * @brief   刷子设备接口（机构级，不依赖具体执行机构类型）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    brush 模块只管理刷子 ID 选择与运行状态；硬件驱动细节
 *          （VFD/继电器/其它）通过 brush_actuator_ops_t 注入，
 *          由 machine 适配层（m8_brush_setup.c）实现。
 *          支持频率和挡位两种速度控制模式，由 ops 实现层决定如何映射到硬件。
 */

#ifndef DOMAIN_DEVICE_BRUSH_H
#define DOMAIN_DEVICE_BRUSH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 刷子 ID
 * ------------------------------------------------------------------------- */
typedef enum
{
    BRUSH_ID_TOP  = 0, /**< 顶刷 */
    BRUSH_ID_SIDE = 1, /**< 侧刷 */
    BRUSH_ID_NONE = 0xFF,
} brush_id_t;

/* -------------------------------------------------------------------------
 * 执行器回调（由 machine 适配层实现并注入）
 * ------------------------------------------------------------------------- */

/**
 * @brief  执行器操作表
 * @note   start_freq / stop 为必填；其余可为 NULL。
 *         start_gear 为 NULL 时 brush_start_gear() 返回 SW_ERR_NOT_SUPPORT。
 *         off 为 NULL 时 brush_off() 退化为调用 stop。
 *         is_fault / get_current 为 NULL 时分别返回 false / 0。
 */
typedef struct
{
    /** 频率模式启动（speed_ref：0.01Hz 或执行机构适用量纲） */
    sw_err_t (*start_freq)(brush_id_t id, uint16_t speed_ref);
    /** 挡位模式启动（gear：通用挡位号，1=最低档；ops 实现负责映射到硬件值） */
    sw_err_t (*start_gear)(brush_id_t id, uint8_t gear);
    /** 停止（保留执行机构选择状态） */
    sw_err_t (*stop)(brush_id_t id);
    /** 停止并释放执行机构资源（洗车结束）；NULL 时退化为 stop */
    sw_err_t (*off)(brush_id_t id);
    /** 查询执行机构是否故障；NULL 时返回 false */
    bool     (*is_fault)(brush_id_t id);
    /** 读取负载电流（0.1A 单位）；NULL 时返回 0 */
    uint16_t (*get_current)(brush_id_t id);
} brush_actuator_ops_t;

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化刷子组件并注入执行器
 * @param  ops  执行器操作表，start_freq / stop 不可为 NULL
 */
sw_err_t brush_init(const brush_actuator_ops_t *ops);

/**
 * @brief  以频率模式启动指定刷子（非阻塞）
 * @param  id         目标刷子 ID
 * @param  speed_ref  速度参考值（含义由执行机构决定，如 VFD 频率 0.01Hz）
 */
sw_err_t brush_start(brush_id_t id, uint16_t speed_ref);

/**
 * @brief  以挡位模式启动指定刷子（非阻塞）
 * @param  id    目标刷子 ID
 * @param  gear  挡位号（1=最低档）；ops.start_gear 为 NULL 时返回 SW_ERR_NOT_SUPPORT
 */
sw_err_t brush_start_gear(brush_id_t id, uint8_t gear);

/**
 * @brief  停止刷子（保留执行机构选择，供下次快速重启）
 */
sw_err_t brush_stop(void);

/**
 * @brief  停止刷子并重置选择状态（洗车结束后调用）
 */
sw_err_t brush_off(void);

/**
 * @brief  查询指定刷子是否正在运行
 */
bool brush_is_running(brush_id_t id);

/**
 * @brief  获取当前选中的刷子 ID（BRUSH_ID_NONE = 无）
 */
brush_id_t brush_get_active(void);

/**
 * @brief  查询指定刷子执行机构是否故障
 */
bool brush_is_fault(brush_id_t id);

/**
 * @brief  获取指定刷子执行机构负载电流（0.1A 单位；不支持时返回 0）
 */
uint16_t brush_get_current(brush_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_BRUSH_H */
