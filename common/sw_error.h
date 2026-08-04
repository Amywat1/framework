/**
 * @file    sw_error.h
 * @brief   项目统一错误码定义与处理分类
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    数值稳定性约束：错误码数值经 protobuf 跨进程传给观测进程
 *          （`observation.proto` 的 effect_error 字段），也会进日志与云端上报。
 *          因此**已分配的数值一律不得重排或复用**——新增错误码只能在末尾追加，
 *          废弃的错误码保留数值并标注，不得删除后让后续值前移。
 */

#ifndef SW_ERROR_H
#define SW_ERROR_H

/* -------------------------------------------------------------------------
 * 错误码类型
 * ------------------------------------------------------------------------- */
typedef enum {
    SW_OK           = 0,   /* 成功 */
    SW_ERR_PARAM    = -1,  /* 参数非法：调用方传入的值不满足接口契约 */
    SW_ERR_TIMEOUT  = -2,  /* 超时：等待外部响应超过约定时限 */
    SW_ERR_HW       = -3,  /* 硬件错误：底层驱动或外设返回失败 */
    SW_ERR_BUSY     = -4,  /* 资源忙：目标暂时被占用，稍后可重试 */
    SW_ERR_NOMEM    = -5,  /* 内存不足 */
    SW_ERR_OVERFLOW = -6,  /* 容量溢出：缓冲区、队列或注册表已满 */
    SW_ERR_STATE    = -7,  /* 状态错误：当前状态不允许此操作 */
    SW_ERR_CRC      = -8,  /* 校验失败：数据完整性不符 */
    SW_ERR_STORAGE  = -9,  /* 存储读写失败 */
    SW_ERR_COMM     = -10, /* 通信错误：链路层收发失败 */
    /* -11 曾为 SW_ERR_UPGRADE（升级错误）。升级功能未落地，暂无使用者；
     * 数值保留不复用，待升级链路实现时再启用同一数值。 */
    SW_ERR_NOT_INIT = -12, /* 模块未初始化或依赖未注册 */
    /* -13 曾为 SW_ERR_NOT_SUPPORT（功能不支持）。当前无使用者；
     * 数值保留不复用。能力缺失场景目前统一用 SW_ERR_NOT_INIT 表达。 */
    SW_ERR_NOT_FOUND = -14, /* 目标记录或资源不存在 */
} sw_err_t;

/* -------------------------------------------------------------------------
 * 处理分类
 *
 * 调用方拿到错误码后要决定：重试、降级、还是终止。这三类判断此前没有统一
 * 依据，各调用点自行 switch，同一个错误码在不同地方被当成不同性质处理。
 *
 * | 分类 | 错误码 | 调用方应有的处理 |
 * |------|--------|------------------|
 * | 瞬时可重试 | BUSY / TIMEOUT / COMM | 按既定重试次数与间隔重试，超限后升级为故障 |
 * | 持久性失败 | HW / STORAGE / CRC / NOMEM / OVERFLOW | 不重试；记录并进入降级或故障路径 |
 * | 调用方缺陷 | PARAM / STATE / NOT_FOUND | 不重试；这是代码或配置错误，重试不会改变结果 |
 * | 接入缺失 | NOT_INIT | 启动期视为致命（应由契约校验拦住）；运行期视为降级 |
 *
 * 判定用下面的 helper，不要在调用点重新枚举错误码——那正是语义分散的来源。
 *
 * 关于 SW_ERR_PARAM 的收窄：它当前占全部返回的约一半（192 处），部分场合被
 * 当作通用失败码。新代码应优先选择更精确的码：
 *   - 状态不允许 → SW_ERR_STATE
 *   - 查不到目标 → SW_ERR_NOT_FOUND
 *   - 容量不足   → SW_ERR_OVERFLOW
 *   - 依赖未注册 → SW_ERR_NOT_INIT
 * SW_ERR_PARAM 只用于"入参本身不合契约"（空指针、越界下标、非法枚举值）。
 * ------------------------------------------------------------------------- */

#include <stdbool.h>

/**
 * @brief  是否为瞬时错误（重试有意义）
 * @param  err  错误码
 * @retval true 调用方可按既定重试策略重试
 */
static inline bool sw_err_is_transient(sw_err_t err)
{
    return (err == SW_ERR_BUSY) || (err == SW_ERR_TIMEOUT) || (err == SW_ERR_COMM);
}

/**
 * @brief  是否为调用方缺陷（重试无意义，属代码或配置错误）
 * @param  err  错误码
 * @retval true 不应重试，应修正调用方
 */
static inline bool sw_err_is_caller_fault(sw_err_t err)
{
    return (err == SW_ERR_PARAM) || (err == SW_ERR_STATE) || (err == SW_ERR_NOT_FOUND);
}

/**
 * @brief  是否表示接入缺失（依赖未注册或未初始化）
 * @param  err  错误码
 * @retval true 启动期应视为致命，运行期视为该能力不可用
 */
static inline bool sw_err_is_missing_binding(sw_err_t err)
{
    return err == SW_ERR_NOT_INIT;
}

/**
 * @brief  错误码可读名称（日志与诊断用）
 * @param  err  错误码
 * @return 名称字符串；未知值返回 "SW_ERR_UNKNOWN"
 */
static inline const char *sw_err_name(sw_err_t err)
{
    switch (err) {
    case SW_OK:
        return "SW_OK";
    case SW_ERR_PARAM:
        return "SW_ERR_PARAM";
    case SW_ERR_TIMEOUT:
        return "SW_ERR_TIMEOUT";
    case SW_ERR_HW:
        return "SW_ERR_HW";
    case SW_ERR_BUSY:
        return "SW_ERR_BUSY";
    case SW_ERR_NOMEM:
        return "SW_ERR_NOMEM";
    case SW_ERR_OVERFLOW:
        return "SW_ERR_OVERFLOW";
    case SW_ERR_STATE:
        return "SW_ERR_STATE";
    case SW_ERR_CRC:
        return "SW_ERR_CRC";
    case SW_ERR_STORAGE:
        return "SW_ERR_STORAGE";
    case SW_ERR_COMM:
        return "SW_ERR_COMM";
    case SW_ERR_NOT_INIT:
        return "SW_ERR_NOT_INIT";
    case SW_ERR_NOT_FOUND:
        return "SW_ERR_NOT_FOUND";
    default:
        return "SW_ERR_UNKNOWN";
    }
}

#endif /* SW_ERROR_H */
