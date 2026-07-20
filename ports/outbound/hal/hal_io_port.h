/**
 * @file    hal_io_port.h
 * @brief   数字 IO HAL 端口接口（模块对外唯一入口）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    业务层与其它 HAL 适配器仅通过本接口访问 IO；
 *          平台实现（m8_io_adapter / hal_io_sim）内部对接 drv_io 或仿真状态。
 */

#ifndef PORTS_HAL_IO_PORT_H
#define PORTS_HAL_IO_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/io_handle.h"
#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/** IO 子板运行时统计（诊断用） */
typedef struct {
    bool     online;
    bool     dirty_pending;
    uint32_t offline_count;
    uint32_t online_recover_count;
    uint32_t input_refresh_count;
    uint32_t output_request_count;
    uint32_t output_flush_count;
    uint64_t last_online_ms;
    uint64_t last_offline_ms;
    uint64_t last_input_refresh_ms;
    uint64_t last_output_req_ms;
    uint64_t last_output_flush_ms;
    uint32_t last_input_snapshot;
    uint32_t last_output_snapshot;
} hal_io_stats_t;

typedef void (*hal_io_debug_input_cb_t)(io_di_t pin, bool state);
typedef void (*hal_io_board_status_cb_t)(int board_id, bool offline);
typedef void (*hal_io_panic_cb_t)(void);

typedef struct {
    /** @brief  初始化 IO 模块内部状态（不启动后台线程） */
    sw_err_t (*init)(void);

    /** @brief  启动 IO 读写后台线程（真机 drv_io 自管，仿真可为空操作） */
    sw_err_t (*start)(void);

    /** @brief  注册全板离线 panic 回调（panic 前尽力落安全输出） */
    void (*register_panic_cb)(hal_io_panic_cb_t cb);

    /** @brief  立即将输出缓冲刷到硬件 */
    sw_err_t (*flush_outputs_now)(void);

    /** @brief  查询 IO 子板是否在线（读缓存，后台线程更新） */
    bool (*board_is_online)(int board_id);

    /**
     * @brief  同步轮询等待所有 IO 子板就绪（启动阶段使用，后台线程启动前可调用）
     * @param  timeout_ms  最长等待时间（ms）
     * @retval SW_OK           所有子板在超时内就绪
     * @retval SW_ERR_TIMEOUT  超时仍有子板离线
     */
    sw_err_t (*wait_boards_online)(uint32_t timeout_ms);

    sw_err_t (*do_set)(io_do_t pin, bool val);
    bool (*di_read)(io_di_t pin);

    void (*register_debug_input_cb)(hal_io_debug_input_cb_t cb);
    void (*register_board_status_cb)(hal_io_board_status_cb_t cb);

    /** @brief  按名称解析 DI/DO（CLI 诊断） */
    bool (*try_parse_di)(const char *name, io_di_t *out);
    bool (*try_parse_do)(const char *name, io_do_t *out);

    /** @brief  句柄可读名称（未知时返回 NULL） */
    const char *(*di_name)(io_di_t pin);
    const char *(*do_name)(io_do_t pin);

    /** @brief  子板数量 */
    int (*board_count)(void);

    /** @brief  子板运行时统计 */
    sw_err_t (*get_stats)(int board_id, hal_io_stats_t *out);

    /** @brief  读取 DI 引脚的硬件脉冲计数器；负值或 0x0FFFFFFF 表示读取失败 */
    int (*pulse_read)(io_di_t pin);

    /** @brief  清零 DI 引脚的硬件脉冲计数器 */
    sw_err_t (*pulse_clear)(io_di_t pin);

    /**
     * @brief  读取 ADC 原始值
     * @param  board_id  子板号，从 1 开始
     * @param  port      ADC 通道号，范围 1~4
     * @retval >= 0   ADC 原始值
     * @retval -99    子板未初始化
     * @retval 其他负值  参数非法或 SDO 读失败
     */
    int (*adc_read)(int board_id, int port);

    /**
     * @brief  读取 ADC 并换算为电压（mV）
     * @param  board_id  子板号，从 1 开始
     * @param  port      ADC 通道号，范围 1~4
     * @retval >= 0   电压值（mV）
     * @retval -99    子板未初始化
     * @retval 其他负值  参数非法或 SDO 读失败
     */
    int (*adc_mv)(int board_id, int port);

    /**
     * @brief  读取 ADC 并换算为电流（mA）
     * @param  board_id  子板号，从 1 开始
     * @param  port      ADC 通道号，范围 1~4
     * @retval >= 0   电流值（mA）
     * @retval -99    子板未初始化
     * @retval 其他负值  参数非法或 SDO 读失败
     */
    int (*adc_ma)(int board_id, int port);
} hal_io_ops_t;

void                hal_io_register(const hal_io_ops_t *ops);
const hal_io_ops_t *hal_io_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_IO_PORT_H */
