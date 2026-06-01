#include "application/coordinators/scheduler_runtime_port.h"

#include "application/coordinators/control_tick.h"
#include "application/coordinators/control_context.h"

static unsigned long adapter_current_time_ms(void)
{
    return control_context_current_time_ms();
}

static unsigned int adapter_pending_trigger_count(void)
{
    return control_context_pending_trigger_count();
}

static bool adapter_has_pending_work(void)
{
    return control_context_has_pending_work();
}

static void adapter_advance_time(unsigned long elapsed_ms)
{
    control_tick_advance_time(elapsed_ms);
}

static operation_result_t adapter_run_control_tick(void)
{
    return control_tick_run();
}

static operation_result_t adapter_on_bind(void *scheduler_handle)
{
    return control_context_bind_scheduler(scheduler_handle);
}

static void adapter_on_unbind(void *scheduler_handle)
{
    if (control_context_bound_scheduler() == scheduler_handle)
    {
        control_context_unbind_scheduler();
    }
}

/**
 * @brief 填充调度器运行时端口。
 * @param port 待写入端口，不能为空。
 */
void scheduler_runtime_port_init(scheduler_runtime_port_t *port)
{
    if (port == 0)
    {
        return;
    }
    port->current_time_ms = adapter_current_time_ms;
    port->pending_trigger_count = adapter_pending_trigger_count;
    port->has_pending_work = adapter_has_pending_work;
    port->advance_time = adapter_advance_time;
    port->run_control_tick = adapter_run_control_tick;
    port->on_bind = adapter_on_bind;
    port->on_unbind = adapter_on_unbind;
}
