/**
 * @file    safety_cutout_port.h
 * @brief   硬件急停快速切断端口（急停热路径）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    必须无阻塞、不持领域层 mutex、不执行总线 flush。
 *          实现由项目通过 safety_port_register 注册，见 safety_port.h。
 */

#ifndef PORTS_SAFETY_CUTOUT_PORT_H
#define PORTS_SAFETY_CUTOUT_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

/**
 * @brief  立即切断动力输出（缓冲写入 + 驱动 cutoff，不 flush 总线）
 *
 * @retval SW_OK          全部切断动作均成功
 * @retval SW_ERR_NOT_INIT 安全端口未注册，切断未执行
 * @retval 其他            至少一路切断失败，返回实现给出的首个失败码
 *
 * @note   本函数自身已记录失败日志并累加统计，调用方通常无需再次记录；
 *         返回值供调用方决定是否叠加自己的上报（如报警、事件）。
 */
sw_err_t safety_cutout_execute(void);

/**
 * @brief  读取累计的切断失败次数（诊断与测试用）
 * @return 自进程启动以来 safety_cutout_execute 返回非 SW_OK 的次数
 */
unsigned safety_cutout_failure_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_SAFETY_CUTOUT_PORT_H */
