/**
 * @file    safety_port.h
 * @brief   安全输出与急停端口（注册表式，替代原弱符号绑定）
 * @author  HUWANGWEI
 * @date    2026-08-03
 *
 * @note    为何不再用弱符号：
 *          弱符号是链接期绑定，项目漏实现时不会有任何提示，而这四个入口
 *          恰好是风险最高的——空实现的 safety_cutout_execute 意味着急停
 *          不切断动力输出，且启动日志里看不出异常。改为注册表后，漏注册
 *          可由 port_contract_validate(PORT_REQ_SAFETY) 在启动期拦住。
 *
 * @note    线程约束：
 *          cutout 在急停热路径（通常为 SCHED_FIFO 高优先级线程）调用，
 *          实现必须无阻塞、幂等、不申请动态内存、不等待其他线程；
 *          deferred_stop 在 event_dispatch 线程调用，可做完备收敛。
 *          注册须在 scheduler 启动线程之前完成（wiring/bind 阶段），
 *          之后 ops 指针只读，故热路径无需加锁。
 */

#ifndef PORTS_OUTBOUND_SAFETY_SAFETY_PORT_H
#define PORTS_OUTBOUND_SAFETY_SAFETY_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 安全端口操作集合
 *
 * 四个字段均为必填：本端口的存在意义就是这四条安全路径都有实处可去，
 * 缺任意一个都会让某条安全链路静默失效，因此注册时一律拒绝部分填充。
 */
typedef struct {
    /**
     * @brief  立即切断动力输出（急停热路径，最佳努力）
     * @note   由急停采集通路在检测到有效边沿时调用；必须幂等。
     */
    void (*cutout)(void);

    /**
     * @brief  读取硬件急停输入当前状态
     * @retval true  急停处于激活状态
     */
    bool (*estop_is_active)(void);

    /**
     * @brief  判断报警码是否代表急停类报警
     * @param  alarm_code  报警码
     * @retval true  该码属于急停语义，运行模式据此进入急停处理
     * @note   报警编码表属于项目资产，故由项目回答。
     */
    bool (*alarm_is_estop)(uint32_t alarm_code);

    /**
     * @brief  延后完备停机（领域状态收敛 + 全量安全输出）
     * @note   在 event_dispatch 线程调用，允许比 cutout 做更多工作。
     */
    void (*deferred_stop)(void);
} safety_ops_t;

/**
 * @brief  注册安全端口实现
 * @param  ops  操作表；传 NULL 解除注册
 * @retval SW_OK        注册或解除成功
 * @retval SW_ERR_PARAM ops 非空但存在空字段，保持原注册不变
 * @note   注册语义与其他端口一致，详见 ports/port_registry.h。
 */
sw_err_t safety_port_register(const safety_ops_t *ops);

/**
 * @brief  获取已注册的安全端口操作表
 * @return 操作表指针；未注册时返回 NULL
 */
const safety_ops_t *safety_port_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_OUTBOUND_SAFETY_SAFETY_PORT_H */
