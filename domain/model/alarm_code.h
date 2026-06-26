/**
 * @file    alarm_code.h
 * @brief   报警码、等级与清除方式定义
 * @author  HUWANGWEI
 * @date    2026-06-26
 *
 * @note    报警码采用「高 8 位模块 ID + 低位模块内编号」分配：
 *            0x02xxxx = 设备控制（传感器/执行机构）
 *            0x04xxxx = 安全逻辑
 *          本头文件位于 domain/model（共享类型层），adapters/machine 可包含它以
 *          完成「硬件信号→报警码」映射；但禁止包含 domain/safety 的实现头文件。
 *          本期仅定义示例所需的三个报警码，新增报警在此扩展即可。
 */

#ifndef DOMAIN_MODEL_ALARM_CODE_H
#define DOMAIN_MODEL_ALARM_CODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* -------------------------------------------------------------------------
 * 报警等级（数值越大越严重，用于活跃集聚合取最高）
 * ------------------------------------------------------------------------- */
typedef enum
{
    ALARM_LEVEL_MINOR = 0,   /**< 仅记录上报，不影响运行（安全态 OK）*/
    ALARM_LEVEL_MAJOR,       /**< 降级运行（安全态 WARNING）*/
    ALARM_LEVEL_CRITICAL,    /**< 立即停机（安全态 LOCKOUT）*/
} alarm_level_t;

/* -------------------------------------------------------------------------
 * 报警清除方式
 * ------------------------------------------------------------------------- */
typedef enum
{
    ALARM_CLEAR_AUTO_STATIC = 0, /**< 触发条件消失后自动清除（M8 数字量信号天然适配）*/
} alarm_clear_t;

/* -------------------------------------------------------------------------
 * 报警码（示例集）
 * ------------------------------------------------------------------------- */
typedef enum
{
    ALARM_CODE_NONE                = 0x000000U, /**< 无报警 */
    ALARM_CODE_SIDE_BRUSH_OVERLOAD = 0x020001U, /**< 设备：侧刷电机过载 */
    ALARM_CODE_FAN_FAULT           = 0x020002U, /**< 设备：风机报警反馈 */
    ALARM_CODE_ESTOP               = 0x040001U, /**< 安全：急停按钮触发 */
} alarm_code_t;

/* -------------------------------------------------------------------------
 * 报警定义（静态配置项，由 alarm_core 持有的配置表使用）
 * ------------------------------------------------------------------------- */
typedef struct
{
    uint32_t      code;   /**< alarm_code_t 取值 */
    alarm_level_t level;  /**< 报警等级 */
    alarm_clear_t clear;  /**< 清除方式 */
    const char   *desc;   /**< 中文描述（日志/上报用）*/
} alarm_def_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_MODEL_ALARM_CODE_H */
