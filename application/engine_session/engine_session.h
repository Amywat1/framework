/**
 * @file    engine_session.h
 * @brief   通用方案引擎会话（装载/启动/tick/中止，不含具体用例）
 * @author  HUWANGWEI
 * @date    2026-07-17
 *
 * @note    一次 init 对应一个 worker；每次 start 传入本次运行参数（路径/超时/回调）。
 *          本模块不包含任何机型方案或固定程序。
 */

#ifndef APPLICATION_ENGINE_SESSION_H
#define APPLICATION_ENGINE_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/program_engine/engine/engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 会话结束结果 */
typedef struct {
    engine_run_state_t final_state; /**< 引擎终态 */
    bool               timed_out;   /**< 总超时 */
    bool               aborted;     /**< 外部中止 */
    bool               success;     /**< DONE 且未超时、未中止 */
} engine_session_result_t;

/**
 * @brief  会话静态配置（init 时拷贝；指针须在会话生命周期内有效）
 */
typedef struct {
    const char *thread_name;     /**< worker 线程名 */
    size_t      stack_size;      /**< worker 栈大小 */
    uint32_t    tick_ms;         /**< tick 周期 */
    uint32_t    startup_wait_ms; /**< start() 等待 engine_start 超时 */
    bool        integrity_check; /**< 是否校验 manifest */
} engine_session_config_t;

/**
 * @brief  单次运行参数（start 时拷贝回调指针；program_path 会拷入会话缓冲）
 */
typedef struct {
    const char *program_path;         /**< 方案 JSON 路径 */
    uint32_t    total_timeout_ms;     /**< 总超时（0=不限）*/
    unsigned    max_phase_recoveries; /**< phase halt 最大恢复次数；0=视为失败 */

    void (*on_started)(void *user);
    /** @brief 当前方案阶段变化；phase_id 仅在回调期间有效。 */
    void (*on_phase_changed)(void *user, const char *phase_id, engine_direction_t direction);
    void (*on_finished)(void *user, const engine_session_result_t *result);
    void (*on_stop_outputs)(void *user);
    void *user;
} engine_session_run_t;

/** @brief 会话对象字节大小（供静态存储）*/
size_t engine_session_size(void);

/**
 * @brief  初始化会话并注册 worker 线程
 * @param  storage  至少 engine_session_size() 字节的存储
 * @param  cfg      静态配置
 */
sw_err_t engine_session_init(void *storage, const engine_session_config_t *cfg);

/**
 * @brief  启动一次方案运行（阻塞至 engine_start 成功/失败或超时）
 * @param  storage  init 时传入的存储
 * @param  run      本次运行参数
 */
sw_err_t engine_session_start(void *storage, const engine_session_run_t *run);

/**
 * @brief  请求中止当前运行
 */
void engine_session_abort(void *storage);

/**
 * @brief  查询是否正在运行
 */
bool engine_session_is_busy(const void *storage);

/**
 * @brief  查询当前阶段行进方向（空闲时为 ENGINE_DIR_NONE）
 */
engine_direction_t engine_session_direction(const void *storage);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_ENGINE_SESSION_H */
