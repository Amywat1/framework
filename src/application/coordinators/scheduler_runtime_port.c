#include "application/coordinators/scheduler_runtime_port.h"

#include "application/coordinators/control_tick.h"
#include "application/coordinators/device_runtime.h"

static unsigned long adapter_current_time_ms(void *context)
{
    return device_runtime_current_time_ms((device_runtime_t)context);
}

static unsigned int adapter_pending_trigger_count(void *context)
{
    return device_runtime_pending_trigger_count((device_runtime_t)context);
}

static bool adapter_has_pending_work(void *context)
{
    return device_runtime_has_pending_work((device_runtime_t)context);
}

static const event_logger_port_t *adapter_event_logger_port(void *context)
{
    return device_runtime_event_logger_port((device_runtime_t)context);
}

static void adapter_advance_time(void *context, unsigned long elapsed_ms)
{
    control_tick_advance_time((device_runtime_t)context, elapsed_ms);
}

static operation_result_t adapter_run_control_tick(void *context)
{
    return control_tick_run((device_runtime_t)context);
}

/**
 * @brief 从 device_runtime 实例填充调度器运行时端口。
 * @param port 待写入端口，不能为空。
 * @param device_runtime 主控运行时句柄，不能为空。
 */
void scheduler_runtime_port_init_from_device_runtime(scheduler_runtime_port_t *port,
                                                     device_runtime_t device_runtime)
{
    if (port == 0)
    {
        return;
    }
    port->context = (void *)device_runtime;
    port->current_time_ms = adapter_current_time_ms;
    port->pending_trigger_count = adapter_pending_trigger_count;
    port->has_pending_work = adapter_has_pending_work;
    port->event_logger_port = adapter_event_logger_port;
    port->advance_time = adapter_advance_time;
    port->run_control_tick = adapter_run_control_tick;
}
