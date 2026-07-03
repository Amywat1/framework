/**
 * @file    contactor_switch.h
 * @brief   接触器切换时序模块接口。
 *
 * 管理"多个负载共用一台物理驱动器、靠接触器切换选中对象"场景下的安全切换时序：
 * 断开旧接触器（等待弹簧回位）→ 通电新接触器（等待触点吸合稳定），防止带载切换
 * 损坏设备。机型无关，供 motor_driver_t::prepare 回调等非阻塞轮询场景使用。
 *
 * @note 非阻塞：contactor_switch_prepare() 每次调用只推进一步或检查时间戳，
 *       不做任何阻塞等待；须由调用方以固定节拍反复调用直到返回 true。
 */
#ifndef DOMAIN_DEVICE_MECHANISM_CONTACTOR_SWITCH_H
#define DOMAIN_DEVICE_MECHANISM_CONTACTOR_SWITCH_H

#include "common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 接触器操作回调，由适配层实现并注入。
 *
 * 两个回调均为非阻塞调用，仅驱动 DO 输出；稳定等待时间由本模块管理。
 */
typedef struct {
    /** @brief 通电指定输出对象的接触器线圈，必填。 */
    void (*engage)(void *ctx, int output_id);
    /** @brief 断电指定输出对象的接触器线圈，必填。 */
    void (*release)(void *ctx, int output_id);
    /** @brief 透传给两个回调的上下文指针。 */
    void *ctx;
} contactor_ops_t;

/** @brief 接触器切换时序配置。 */
typedef struct {
    uint32_t release_ms; /**< 断电后等待接触器完全释放的时间（ms） */
    uint32_t close_ms;   /**< 通电后等待接触器吸合并稳定的时间（ms） */
} contactor_timing_t;

/** @brief 内部切换阶段（不对外暴露语义，仅用于静态分配）。 */
typedef enum {
    CONTACTOR_PHASE_IDLE = 0,
    CONTACTOR_PHASE_RELEASING,
    CONTACTOR_PHASE_ENGAGING,
} contactor_phase_t;

/** @brief 接触器切换状态对象，调用方静态分配。 */
typedef struct {
    contactor_ops_t    ops;
    contactor_timing_t timing;
    int                current_id;
    contactor_phase_t  phase;
    uint64_t           until_ms;
    bool               initialized;
} contactor_switch_t;

/**
 * @brief 初始化接触器切换状态对象。
 *
 * @param cs               调用方分配的状态对象。
 * @param ops              接触器操作回调，engage 和 release 均不得为 NULL。
 * @param timing           切换时序配置。
 * @param initial_output_id 上电时接触器实际所处的位置（未吸合任何一路时，
 *                          传入约定的默认对象编号，首次切换到其它对象时会
 *                          正常触发 release/engage 序列）。
 * @return SW_OK 成功；SW_ERR_PARAM 参数非法。
 */
sw_err_t contactor_switch_init(contactor_switch_t *cs, const contactor_ops_t *ops,
                               contactor_timing_t timing, int initial_output_id);

/**
 * @brief 非阻塞推进接触器切换到目标对象。
 *
 * 须由调用方以固定节拍反复调用（如 MCC 的 prepare() 回调），直到返回 true。
 * 等待期间若 target_id 发生变化，将从当前所处阶段重新计算目标。
 *
 * @param cs        已初始化的状态对象。
 * @param target_id 目标输出对象编号。
 * @param now_ms    调用方传入的当前时刻（ms），便于测试注入固定时间序列。
 * @return true 接触器已在 target_id 且稳定；false 尚未就绪，须下次再调用。
 */
bool contactor_switch_prepare(contactor_switch_t *cs, int target_id, uint64_t now_ms);

/**
 * @brief 查询接触器当前实际吸合的输出对象编号。
 * @return 当前对象编号；cs 为 NULL 或未初始化时返回 -1。
 */
int contactor_switch_current(const contactor_switch_t *cs);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_DEVICE_MECHANISM_CONTACTOR_SWITCH_H */
