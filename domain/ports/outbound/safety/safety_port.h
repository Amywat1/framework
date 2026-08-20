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

#ifndef DOMAIN_PORTS_OUTBOUND_SAFETY_SAFETY_PORT_H
#define DOMAIN_PORTS_OUTBOUND_SAFETY_SAFETY_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 安全端口操作集合
 *
 * 四个核心字段均为必填：本端口的存在意义就是这四条安全路径都有实处可去，
 * 缺任意一个都会让某条安全链路静默失效，因此注册时一律拒绝部分填充。
 * cutout_confirmed 是失败锁存的独立确认能力，项目若无法提供则保持未确认锁存。
 */
typedef struct {
    /**
     * @brief  立即切断动力输出（急停热路径，最佳努力）
     * @retval SW_OK  全部切断动作均成功
     * @retval 其他   至少一路切断失败；返回首个失败的错误码
     * @note   由急停采集通路在检测到有效边沿时调用；必须幂等。
     * @note   实现须「尽力做完全部切断动作再返回」，不得在首个失败处提前
     *         返回——某一路 DO 写失败不构成放弃其余各路的理由。
     * @note   返回非 SW_OK 不代表切断完全未发生，只表示不能确认已完全切断。
     *         框架据此记录并上报，不据此重试：急停热路径上的重试会延长
     *         不确定窗口，且失败通常源于硬件链路本身。
     */
    sw_err_t (*cutout)(void);

    /**
     * @brief  独立确认动力回路已切断（可选，非急停热路径）
     * @retval true  通过独立反馈确认已切断
     * @note   用于清除切断未确认锁存；不得复用急停输入，也不得在热路径调用。
     */
    bool (*cutout_confirmed)(void);

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
 * @retval SW_ERR_PARAM ops 非空但核心字段存在空字段，保持原注册不变
 * @note   注册语义与其他端口一致，详见 runtime/ports/port_registry.h。
 */
sw_err_t safety_port_register(const safety_ops_t *ops);

/**
 * @brief  获取已注册的安全端口操作表
 * @return 操作表指针；未注册时返回 NULL
 */
const safety_ops_t *safety_port_get_ops(void);

/* -------------------------------------------------------------------------
 * 调用侧包装
 *
 * 以下入口由 port_registry_safety.c 实现，转调已注册 ops 的对应字段。
 * 未注册时行为明确（故障安全 + 每条路径首次告警一次），而不是静默空转。
 * 框架内既有调用点直接用这些函数名，不必自行取 ops 再判空。
 * ------------------------------------------------------------------------- */

/**
 * @brief  立即切断动力输出（缓冲写入 + 驱动 cutoff，不 flush 总线）
 *
 * @retval SW_OK           全部切断动作均成功
 * @retval SW_ERR_NOT_INIT 安全端口未注册，切断未执行
 * @retval 其他            至少一路切断失败，返回实现给出的首个失败码
 *
 * @note   急停热路径专用：必须无阻塞、不持领域层 mutex、不执行总线 flush。
 * @note   本函数自身已记录失败日志并累加统计，调用方通常无需再次记录；
 *         返回值供调用方决定是否叠加自己的上报（如报警、事件）。
 */
sw_err_t safety_cutout_execute(void);

/**
 * @brief  读取累计的切断失败次数（诊断与测试用）
 * @return 自进程启动以来 safety_cutout_execute 返回非 SW_OK 的次数
 * @note   未注册导致的「未执行」不计入此处，由未注册告警单独反映，两者语义分开。
 */
unsigned safety_cutout_failure_count(void);

/**
 * @brief  切断失败后是否仍处于未确认锁存
 * @retval true  至少一次切断失败尚未被独立反馈确认
 * @retval false 当前无未确认失败
 */
bool safety_cutout_is_unconfirmed(void);

/**
 * @brief  通过项目独立反馈确认切断结果并清除未确认锁存
 * @retval SW_OK 已确认或当前无未确认锁存
 * @retval SW_ERR_STATE 独立反馈仍未确认
 * @retval SW_ERR_NOT_INIT 未提供独立确认回调
 */
sw_err_t safety_cutout_reconcile(void);

/**
 * @brief  读取硬件急停是否处于激活（按下/断电）状态
 * @retval true   急停激活
 * @retval false  急停未激活或安全端口未注册
 * @note   实现须读取原始 DI，不走传感器滤波防抖链，以保证 ≤5ms 级响应。
 */
bool hw_estop_port_is_active(void);

/**
 * @brief  判断报警码是否为急停报警
 * @param  alarm_code  报警码
 * @retval true   急停报警
 * @retval false  其他报警，或安全端口未注册
 */
bool op_mode_alarm_port_is_estop(uint32_t alarm_code);

/**
 * @brief  执行延后完备停机（领域 API + 可选全量安全输出）
 * @note   在 EVT_HW_ESTOP_ON 消费后于 event_dispatch 线程调用，
 *         可含总线 flush 与领域状态收敛。
 */
void safety_deferred_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_SAFETY_SAFETY_PORT_H */
