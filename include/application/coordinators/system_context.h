#ifndef APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H
#define APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H

#include <stdbool.h>

#include "domain/model/domain_enums.h"
#include "domain/ports/actuator_port.h"
#include "domain/ports/event_logger_port.h"
#include "domain/ports/program_repository_port.h"
#include "domain/ports/sensor_port.h"

/**
 * @file system_context.h
 * @brief 声明主控运行期组合根接口。
 */

/**
 * @brief 主控运行期组合根。
 *
 * @note 该类型在公开头文件中保持不透明。
 * @note 外部适配层与测试辅助只允许通过正式生命周期、装配和查询接口使用该对象。
 */
typedef struct system_context_t system_context_t;

/**
 * @brief 创建一个新的主控上下文。
 *
 * @return 成功时返回新分配的上下文；内存不足时返回 `0`。
 */
system_context_t *system_context_create(void);

/**
 * @brief 初始化一块调用方提供的主控上下文存储。
 *
 * @param system_context 主控上下文存储，不能为空。
 *
 * @note 调用方拥有这块存储；后续调用 `system_context_destroy()` 时只会清空内容，不会释放该存储。
 */
void system_context_initialize(system_context_t *system_context);

/**
 * @brief 销毁主控上下文。
 *
 * @param system_context 主控上下文；允许为 `0`。
 *
 * @note 若对象由 `system_context_create()` 创建，则本接口会释放其存储。
 * @note 若对象由 `system_context_initialize()` 初始化在调用方自有存储上，则本接口只清空内容，不释放存储。
 */
void system_context_destroy(system_context_t *system_context);

/**
 * @brief 重置主控运行态，但保留已装配的外部端口。
 *
 * @param system_context 主控上下文，不能为空。
 */
void system_context_reset(system_context_t *system_context);

/**
 * @brief 将传感器端口装配到主控上下文。
 *
 * @param system_context 主控上下文，不能为空。
 * @param sensor_port 传感器端口；传入 `0` 时清空该端口。
 */
void system_context_set_sensor_port(system_context_t *system_context, const sensor_port_t *sensor_port);

/**
 * @brief 将执行机构端口装配到主控上下文。
 *
 * @param system_context 主控上下文，不能为空。
 * @param actuator_port 执行机构端口；传入 `0` 时清空该端口。
 */
void system_context_set_actuator_port(system_context_t *system_context, const actuator_port_t *actuator_port);

/**
 * @brief 将事件日志端口装配到主控上下文。
 *
 * @param system_context 主控上下文，不能为空。
 * @param event_logger_port 日志端口；传入 `0` 时清空该端口。
 */
void system_context_set_event_logger_port(system_context_t *system_context, const event_logger_port_t *event_logger_port);

/**
 * @brief 将程序仓储端口装配到主控上下文。
 *
 * @param system_context 主控上下文，不能为空。
 * @param program_repository_port 程序仓储端口；传入 `0` 时清空该端口。
 */
void system_context_set_program_repository_port(system_context_t *system_context,
    const program_repository_port_t *program_repository_port);

/**
 * @brief 读取当前已装配的程序仓储端口。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 程序仓储端口只读指针；无上下文时返回 `0`。
 */
const program_repository_port_t *system_context_program_repository_port(const system_context_t *system_context);

/**
 * @brief 读取主控当前时间。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 当前时间；`system_context` 为空时返回 `0`。
 */
unsigned long system_context_current_time_ms(const system_context_t *system_context);

/**
 * @brief 读取待处理触发数量。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 当前待处理触发数量；`system_context` 为空时返回 `0`。
 */
unsigned int system_context_pending_trigger_count(const system_context_t *system_context);

/**
 * @brief 统计指定触发 ID 当前仍在队列中的数量。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @param trigger_id 目标触发 ID；允许为 `0`。
 * @return 匹配数量。
 */
unsigned int system_context_count_pending_triggers_by_id(const system_context_t *system_context, const char *trigger_id);

/**
 * @brief 统计指定触发类型当前仍在队列中的数量。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @param trigger_type 目标触发类型。
 * @return 匹配数量。
 */
unsigned int system_context_count_pending_triggers_by_type(const system_context_t *system_context, trigger_type_t trigger_type);

/**
 * @brief 判断主控当前是否仍有待处理运行工作。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 存在待处理触发或已到期超时时返回 `true`。
 */
bool system_context_has_pending_runtime_work(const system_context_t *system_context);

/**
 * @brief 判断当前是否存在已激活的等待条件。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 等待条件激活时返回 `true`。
 */
bool system_context_wait_condition_active(const system_context_t *system_context);

/**
 * @brief 计算当前等待条件距离超时还剩余多少毫秒。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 剩余毫秒数；无有效等待或已到期时返回 `0`。
 */
unsigned long system_context_wait_timeout_ms(const system_context_t *system_context);

/**
 * @brief 读取最近一次对外结果码。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 最近结果码；无值时返回空字符串。
 */
const char *system_context_last_result_code(const system_context_t *system_context);

/**
 * @brief 读取最近一次对外原因码。
 *
 * @param system_context 主控上下文；允许为 `0`。
 * @return 最近原因码；无值时返回空字符串。
 */
const char *system_context_last_reason_code(const system_context_t *system_context);

#endif
