/**
 * @file    safety_deferred_stop.h
 * @brief   急停延后完备停机（event_dispatch 线程）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    在 EVT_HW_ESTOP_ON 消费后调用，可含总线 flush 与领域状态收敛。
 */

#ifndef APPLICATION_SAFETY_DEFERRED_STOP_H
#define APPLICATION_SAFETY_DEFERRED_STOP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  执行延后完备停机（领域 API + 可选全量安全输出）
 */
void safety_deferred_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_SAFETY_DEFERRED_STOP_H */
