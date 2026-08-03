/**
 * @file    bootstrap.c
 * @brief   系统启动序列实现（wash-device-framework 仿真子集）
 * @author  HUWANGWEI
 * @date    2026-07-12
 */

#include "runtime/bootstrap/bootstrap.h"

#include "adapters/inbound/event/alarm_event_bridge.h"
#include "adapters/inbound/event/alarm_lifecycle_bridge.h"
#include "adapters/inbound/event/op_mode_bridge.h"
#include "application/command_gateway.h"
#include "application/orchestrators/abort_home_coordinator.h"
#include "application/orchestrators/safety_cutout_coordinator.h"
#include "application/recovery_service.h"
#include "application/self_check_service.h"
#include "application/telemetry_projection.h"
#include "common/log.h"
#include "common/time_util.h"
#include "domain/op_mode/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "ports/outbound/hal/hal_io_port.h"
#include "ports/outbound/hal/hal_vfd_port.h"
#include "ports/outbound/hal/hal_voice_port.h"
#include "ports/outbound/storage/deploy_store.h"
#include "runtime/bootstrap/project_hooks.h"
#include "runtime/bootstrap/wiring.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/scheduler.h"
#include "runtime/scheduler/thread_registry.h"
#include "services/param/svc_param.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>

static const project_hooks_t *s_hooks;

sw_err_t bootstrap_register_hooks(const project_hooks_t *hooks)
{
    if ((hooks == NULL) || (hooks->configure_storage == NULL) || (hooks->configure_hal == NULL)
        || (hooks->bind_hal == NULL) || (hooks->init_hal == NULL) || (hooks->configure_safety == NULL)
        || (hooks->init_safety == NULL) || (hooks->configure_adapters == NULL) || (hooks->bind_machine == NULL)
        || (hooks->init_machine == NULL) || (hooks->bind_alarm_catalog == NULL) || (hooks->validate == NULL)
        || (hooks->init_adapters == NULL) || (hooks->register_runtime_tasks == NULL) || (hooks->start_runtime == NULL)
        || (hooks->assert_safe_outputs == NULL)) {
        return SW_ERR_PARAM;
    }
    s_hooks = hooks;
    return SW_OK;
}

#define BOOT_CHECK(call, msg)                                                                                          \
    do {                                                                                                               \
        sw_err_t _r = (call);                                                                                          \
        if (_r != SW_OK) {                                                                                             \
            LOG_ERROR("bootstrap: " msg " ret=%d", (int)_r);                                                           \
            return _r;                                                                                                 \
        }                                                                                                              \
    } while (0)

static void system_panic_safe_stop(event_bus_fatal_reason_t reason, int sys_errno)
{
    LOG_ERROR("bootstrap: event_bus fatal reason=%d errno=%d", (int)reason, sys_errno);
    if ((s_hooks != NULL) && (s_hooks->assert_safe_outputs != NULL)) {
        s_hooks->assert_safe_outputs();
    }
    abort();
}

static void *event_dispatch_thread_fn(void *arg)
{
    (void)arg;
    event_bus_dispatch_loop();
    return NULL;
}

static sw_err_t hal_io_bootstrap_init(void)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->init == NULL)) {
        return SW_OK;
    }

    BOOT_CHECK(io->init(), "hal_io_init");
    if ((io->register_panic_cb != NULL) && (s_hooks != NULL)) {
        io->register_panic_cb(s_hooks->assert_safe_outputs);
    }
    return SW_OK;
}

static sw_err_t hal_io_bootstrap_start(void)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->start == NULL)) {
        return SW_OK;
    }

    BOOT_CHECK(io->start(), "hal_io_start");
    return SW_OK;
}

static sw_err_t hal_vfd_bootstrap_init(void)
{
    const hal_vfd_ops_t *vfd = hal_vfd_get_ops();

    if ((vfd == NULL) || (vfd->init == NULL)) {
        return SW_OK;
    }

    BOOT_CHECK(vfd->init(), "hal_vfd_init");
    return SW_OK;
}

static sw_err_t hal_voice_bootstrap_init(void)
{
    const hal_voice_ops_t *voice = hal_voice_get_ops();

    if ((voice == NULL) || (voice->init == NULL)) {
        return SW_OK;
    }

    BOOT_CHECK(voice->init(), "hal_voice_init");
    return SW_OK;
}

static sw_err_t bootstrap_load_storage(void)
{
    const deploy_store_ops_t *ds = deploy_store_get_ops();

    {
        sw_err_t r = svc_param_init();

        if ((r != SW_OK) && (r != SW_ERR_STORAGE)) {
            LOG_ERROR("bootstrap: svc_param_init ret=%d", (int)r);
            return r;
        }
    }

    if (ds != NULL) {
        sw_err_t r = ds->load();

        if ((r != SW_OK) && (r != SW_ERR_STORAGE)) {
            LOG_ERROR("bootstrap: deploy_store load ret=%d", (int)r);
            return r;
        }
    }

    return SW_OK;
}

static sw_err_t bootstrap_register(void)
{
    time_util_init();

    BOOT_CHECK(event_bus_init(), "event_bus_init");
    event_bus_set_fatal_cb(system_panic_safe_stop);

    BOOT_CHECK(wiring(), "wiring");
    BOOT_CHECK(project_hooks_register(), "project_hooks_register");
    BOOT_CHECK(thread_register("event_dispatch", event_dispatch_thread_fn, SCHED_OTHER, 0, THD_EVENT_DISPATCH_STACK),
               "register event_dispatch");

    return SW_OK;
}

static sw_err_t bootstrap_configure_storage(void)
{
    BOOT_CHECK(s_hooks->configure_storage(), "project_configure_storage");
    return SW_OK;
}

static sw_err_t bootstrap_configure(void)
{
    BOOT_CHECK(s_hooks->configure_hal(), "project_configure_hal");
    BOOT_CHECK(s_hooks->configure_safety(), "project_configure_safety");
    BOOT_CHECK(s_hooks->configure_adapters(), "project_configure_adapters");

    return SW_OK;
}

static sw_err_t bootstrap_bind(void)
{
    BOOT_CHECK(s_hooks->bind_hal(), "project_bind_hal");
    BOOT_CHECK(s_hooks->bind_machine(), "project_bind_machine");
    BOOT_CHECK(alarm_registry_init(), "alarm_registry_init");
    BOOT_CHECK(s_hooks->bind_alarm_catalog(), "project_bind_alarm_catalog");

    return SW_OK;
}

static sw_err_t bootstrap_validate(void)
{
    BOOT_CHECK(s_hooks->validate(), "project_validate");
    return SW_OK;
}

static sw_err_t bootstrap_init_hal(void)
{
    BOOT_CHECK(hal_io_bootstrap_init(), "hal_io_bootstrap");
    BOOT_CHECK(hal_vfd_bootstrap_init(), "hal_vfd_bootstrap");
    BOOT_CHECK(hal_voice_bootstrap_init(), "hal_voice_bootstrap");
    BOOT_CHECK(s_hooks->init_hal(), "project_init_hal");
    return SW_OK;
}

static sw_err_t bootstrap_init_safety(void)
{
    BOOT_CHECK(s_hooks->init_safety(), "project_init_safety");
    return SW_OK;
}

static sw_err_t bootstrap_init_machine(void)
{
    BOOT_CHECK(s_hooks->init_machine(), "project_init_machine");
    return SW_OK;
}

static sw_err_t bootstrap_init_services(void)
{
    BOOT_CHECK(alarm_event_bridge_init(), "alarm_event_bridge_init");
    BOOT_CHECK(operational_mode_init(), "operational_mode_init");
    BOOT_CHECK(command_gateway_init(), "command_gateway_init");
    BOOT_CHECK(self_check_service_init(), "self_check_service_init");
    BOOT_CHECK(recovery_service_init(), "recovery_service_init");
    BOOT_CHECK(safety_cutout_coordinator_init(), "safety_cutout_coordinator_init");
    BOOT_CHECK(abort_home_coordinator_init(), "abort_home_coordinator_init");
    BOOT_CHECK(alarm_lifecycle_bridge_init(), "alarm_lifecycle_bridge_init");
    BOOT_CHECK(op_mode_bridge_init(), "op_mode_bridge_init");
    BOOT_CHECK(telemetry_projection_init(), "telemetry_projection_init");
    BOOT_CHECK(s_hooks->init_adapters(), "project_init_adapters");
    BOOT_CHECK(s_hooks->register_runtime_tasks(), "project_register_runtime_tasks");
    return SW_OK;
}

static sw_err_t bootstrap_start(void)
{
    BOOT_CHECK(hal_io_bootstrap_start(), "hal_io_bootstrap_start");
    BOOT_CHECK(s_hooks->start_runtime(), "project_start_runtime");
    BOOT_CHECK(scheduler_start_all(), "scheduler_start_all");
    return SW_OK;
}

typedef sw_err_t (*bootstrap_phase_fn_t)(void);

static sw_err_t bootstrap_run_phase(const char *name, bootstrap_phase_fn_t phase)
{
    sw_err_t ret;

    LOG_INFO("bootstrap: phase [%s] begin", name);
    ret = phase();
    if (ret != SW_OK) {
        LOG_ERROR("bootstrap: phase [%s] failed ret=%d", name, (int)ret);
        return ret;
    }
    LOG_INFO("bootstrap: phase [%s] done", name);
    return SW_OK;
}

sw_err_t bootstrap_run(void)
{
    sw_err_t ret;

    ret = bootstrap_run_phase("register", bootstrap_register);
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_run_phase("configure_storage", bootstrap_configure_storage);
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_run_phase("load_storage", bootstrap_load_storage);
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_run_phase("configure", bootstrap_configure);
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_run_phase("bind", bootstrap_bind);
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_run_phase("validate", bootstrap_validate);
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_run_phase("init_hal", bootstrap_init_hal);
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_run_phase("init_machine", bootstrap_init_machine);
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_run_phase("init_safety", bootstrap_init_safety);
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_run_phase("init_services", bootstrap_init_services);
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_run_phase("start", bootstrap_start);
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("bootstrap: started");
    return SW_OK;
}
