/**
 * @file    alarm_code.h
 * @brief   报警码、等级与清除方式定义
 * @author  HUWANGWEI
 * @date    2026-06-26
 *
 * @note    报警码采用 6 位十进制编码：大类(1) + 具体编号(3) + 故障性质(2)，
 *            code = 大类 * 100000 + 编号 * 100 + 性质（编号≤999，性质≤99）。
 *          大类：1 动力 / 2 感知 / 3 执行 / 4 控制 / 9 软件；编码规则与完整目录
 *          见 doc/报警编码规范.md。
 *          本头文件只定义「类型」（等级/清除方式/定义项），不再罗列具体报警码——
 *          报警目录（code/level/clear/desc）是数据，由机型检测适配器（adapters/machine）
 *          在 init 时通过 alarm_binding_port.load_catalog 注入 alarm_core。
 *          「信号→报警码」的检测绑定属机型层，见 adapters/machine 的检测适配器。
 *          本头位于 domain/model（共享类型层），adapters/machine 可包含它；但禁止
 *          包含 domain/safety 的实现头文件。
 */

#ifndef DOMAIN_MODEL_ALARM_CODE_H
#define DOMAIN_MODEL_ALARM_CODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* -------------------------------------------------------------------------
 * 报警码编码宏：6 位十进制 = 大类(1) + 具体编号(3) + 故障性质(2)
 *   code = 大类 * 100000 + 编号 * 100 + 性质   （编号≤999，性质≤99）
 * ------------------------------------------------------------------------- */
#define ALARM_CODE_MAKE(major, index, nature) \
    ((uint32_t)((major) * 100000U + (index) * 100U + (nature)))

/* 报警描述最大字节数（定长存储，免堆，便于从配置加载） */
#define ALARM_DESC_MAX      48U

/* 报警目录最大条目数（alarm_core 固定容量目录与活跃集上限） */
#define ALARM_CATALOG_MAX   64U

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
    ALARM_CLEAR_AUTO_STATIC = 0, /**< 触发条件消失后自动清除（电平直检类：急停、限位、液位…）*/
    ALARM_CLEAR_LATCHED,         /**< 锁存，须人工复位（过载/通讯/硬件故障/超时类）。
                                  *   本期已定义、暂未启用强制锁存——其复位入口
                                  *   见 doc/报警编码规范.md §5。*/
} alarm_clear_t;

/* -------------------------------------------------------------------------
 * 报警码哨兵
 *   具体报警码不在此枚举，它们是数据：由 JSON 目录定义、由机型检测层引用。
 *   ALARM_CODE_NONE 表示「无活跃报警」，供查询接口返回。
 * ------------------------------------------------------------------------- */
typedef enum
{
    ALARM_CODE_NONE = 0U, /**< 无报警 */
} alarm_code_t;

/* -------------------------------------------------------------------------
 * 报警定义（一条报警的完整定义；来源为 JSON 目录，加载进 alarm_core）
 * ------------------------------------------------------------------------- */
typedef struct
{
    uint32_t      code;                 /**< 6 位十进制报警码 */
    alarm_level_t level;                /**< 报警等级 */
    alarm_clear_t clear;                /**< 清除方式 */
    char          desc[ALARM_DESC_MAX]; /**< 中文描述（日志/上报用）*/
} alarm_def_t;

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_MODEL_ALARM_CODE_H */
