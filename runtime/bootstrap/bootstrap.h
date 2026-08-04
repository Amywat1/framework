/**
 * @file    bootstrap.h
 * @brief   系统启动入口接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#ifndef RUNTIME_BOOTSTRAP_BOOTSTRAP_H
#define RUNTIME_BOOTSTRAP_BOOTSTRAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  系统完整启动序列（基础设施 → 安全 → 应用 → 适配器 → 线程）
 * @retval SW_OK 启动成功
 * @retval 其他  某阶段失败；失败阶段名与错误码已记入 ERROR 日志
 *
 * @par 失败后的进程约束（调用方必须遵守）：
 *   返回非 SW_OK 时，**调用方必须终止进程**，不得重试 bootstrap_run()、
 *   也不得继续运行业务逻辑。
 *
 *   原因：启动序列没有回滚路径。失败点之前的副作用全部保留——端口已注册、
 *   报警目录已载入、HAL 可能已初始化、线程可能已登记（但未启动）。框架不提供
 *   teardown 是有意的取舍：为覆盖任意阶段失败而维护一套对称的反初始化路径，
 *   其自身的正确性比"失败即退出"更难保证，而嵌入式设备由进程管理器重启即可
 *   回到确定状态。
 *
 *   重复调用的具体后果：`event_bus_init()` 会在 dispatch 线程可能已运行时重置
 *   队列与订阅表；线程登记表会累积重复条目，`scheduler_start_all()` 随后按整表
 *   创建线程，同一任务被启动多次。两者都不会立即报错，而是表现为难以定位的
 *   运行期异常。
 *
 * @par 与端口契约校验的关系：
 *   多数接入错误应在 validate 阶段由 port_contract_validate() 拦住并给出缺失
 *   清单，此时尚未初始化硬件、也未启动线程，是最干净的失败点。项目应把必需
 *   端口声明写全，使失败尽量落在该阶段。
 */
sw_err_t bootstrap_run(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_BOOTSTRAP_BOOTSTRAP_H */
