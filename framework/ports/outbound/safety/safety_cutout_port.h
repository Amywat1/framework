/**
 * @file    safety_cutout_port.h
 * @brief   硬件急停快速切断端口（safety_thread 热路径）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    必须无阻塞、不持领域层 mutex、不执行总线 flush。
 *          项目层提供强符号；默认弱实现仅做最小域请求。
 */

#ifndef PORTS_SAFETY_CUTOUT_PORT_H
#define PORTS_SAFETY_CUTOUT_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  立即切断动力输出（缓冲写入 + 驱动 cutoff，不 flush 总线）
 */
void safety_cutout_execute(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_SAFETY_CUTOUT_PORT_H */
