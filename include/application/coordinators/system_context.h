#ifndef APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H
#define APPLICATION_COORDINATORS_SYSTEM_CONTEXT_H

/**
 * @file system_context.h
 * @brief 声明主控运行期组合根句柄。
 */

/**
 * @brief 主控运行期组合根。
 *
 * @note 该类型在公开头文件中保持不透明，关键运行状态只能通过正式拥有者在私有实现中改写。
 * @note 领域层与外部适配层不应依赖其内部布局。
 */
typedef struct system_context_t system_context_t;

#endif
