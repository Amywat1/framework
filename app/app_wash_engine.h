/**
 * @file    app_wash_engine.h
 * @brief   龙门洗车流程引擎接口
 * @author  胡望伟
 * @date    2026-04-08
 */

#ifndef APP_WASH_ENGINE_H
#define APP_WASH_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app/app_wash_steps.h"
#include "common/sw_error.h"

/**
 * @brief  初始化洗车引擎
 * @retval SW_OK
 */
sw_err_t wash_engine_init(void);

/**
 * @brief  开始一次洗车流程（非阻塞，内部开启洗车线程）
 * @param  mode  洗车模式
 * @retval SW_OK / SW_ERR_BUSY / SW_ERR_STATE
 */
sw_err_t wash_engine_start(WashMode_t mode);

/**
 * @brief  紧急停止（立即停止所有执行机构）
 */
void wash_engine_emergency_stop(void);

/**
 * @brief  查询洗车是否完成
 * @retval true=已完成（或未开始）
 */
bool wash_engine_is_done(void);

/**
 * @brief  查询当前步骤
 */
WashStep_t wash_engine_get_step(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_WASH_ENGINE_H */
