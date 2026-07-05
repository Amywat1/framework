/**
 * @file    hal_do_group_mapper.h
 * @brief   DO 组×槽位 HAL 内部接口（仅供机型适配层绑定槽位）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    仅供 projects/<project>/wiring/ 在 bootstrap 阶段调用；
 *          业务层仍通过 hal_do_group_port 访问。
 *          本文件不含任何平台专属 SDK 依赖，可用于任何已注册
 *          hal_io_port 的目标平台。
 *          仅用于注册通用 DO 组实现到 hal_do_group_port；通道绑定请使用
 *          hal_do_group_port.h 中 hal_do_group_ops_t.bind()。
 */

#ifndef ADAPTERS_HAL_COMPONENTS_DO_GROUP_MAPPER_HAL_DO_GROUP_MAPPER_H
#define ADAPTERS_HAL_COMPONENTS_DO_GROUP_MAPPER_HAL_DO_GROUP_MAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief  注册通用 DO 组 HAL 实现到 hal_do_group_port */
void hal_do_group_mapper_register(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_HAL_COMPONENTS_DO_GROUP_MAPPER_HAL_DO_GROUP_MAPPER_H */
