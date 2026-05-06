#ifndef DOMAIN_PORTS_EVENT_LOGGER_PORT_H
#define DOMAIN_PORTS_EVENT_LOGGER_PORT_H

#include "domain/model/domain_enums.h"

struct state_transition_record_t;

/**
 * @file event_logger_port.h
 * @brief 定义事件日志端口契约。
 */

/**
 * @brief 抽象主控关键事件、状态迁移、拒绝和忽略日志的输出能力。
 */
typedef struct event_logger_port_t {
    void *context;
    int (*log_message)(void *context, event_type_t event_type, const char *message);
    int (*log_transition)(void *context, const struct state_transition_record_t *state_transition_record);
    int (*log_rejection)(void *context, const struct state_transition_record_t *state_transition_record);
    int (*log_ignored)(void *context, const struct state_transition_record_t *state_transition_record);
} event_logger_port_t;

#endif
