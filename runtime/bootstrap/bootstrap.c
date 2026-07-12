/**
 * @file    bootstrap.c
 * @brief   系统启动序列实现（wash-device-framework 仿真子集）
 * @author  HUWANGWEI
 * @date    2026-07-12
 */

#include "runtime/bootstrap/bootstrap.h"

#include "application/alarm_event_bridge.h"
#include "application/command_gateway.h"
#include "application/op_mode_bridge.h"
#include "application/self_check_service.h"
#include "common/log.h"
#include "common/time_util.h"
#include "domain/command_gateway/operational_mode.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/safety_posture/safety_posture.h"
#include "ports/outbound/hal/hal_io_port.h"
#include "ports/outbound/hal/hal_vfd_port.h"
#include "ports/outbound/hal/hal_voice_port.h"
#include "ports/outbound/storage/deploy_store.h"
#include "runtime/bootstrap/project_hooks.h"
#include "runtime/bootstrap/wiring.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/platform/safety_thread.h"
#include "runtime/scheduler/scheduler.h"
#include "runtime/scheduler/thread_registry.h"
#include "services/param/svc_param.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>

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
    project_assert_safe_outputs();
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
    if (io->register_panic_cb != NULL) {
        io->register_panic_cb(project_assert_safe_outputs);
    }
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

static sw_err_t bootstrap_init_infra(void)
{
    time_util_init();

    BOOT_CHECK(event_bus_init(), "event_bus_init");
    event_bus_set_fatal_cb(system_panic_safe_stop);

    BOOT_CHECK(wiring(), "wiring");
    BOOT_CHECK(hal_io_bootstrap_init(), "hal_io_bootstrap");
    BOOT_CHECK(hal_vfd_bootstrap_init(), "hal_vfd_bootstrap");
    BOOT_CHECK(hal_voice_bootstrap_init(), "hal_voice_bootstrap");
    BOOT_CHECK(project_hal_extra_setup(), "project_hal_extra_setup");

    {
        sw_err_t r = svc_param_init();

        if ((r != SW_OK) && (r != SW_ERR_STORAGE)) {
            LOG_ERROR("bootstrap: svc_param_init ret=%d", (int)r);
            return r;
        }
    }

    return SW_OK;
}

static sw_err_t bootstrap_init_application(void)
{
    BOOT_CHECK(project_machine_setup(), "project_machine_setup");
    BOOT_CHECK(alarm_registry_init(), "alarm_registry_init");
    BOOT_CHECK(safety_posture_init(), "safety_posture_init");
    BOOT_CHECK(project_alarm_catalog_init(), "project_alarm_catalog_init");
    BOOT_CHECK(alarm_event_bridge_init(), "alarm_event_bridge_init");
    BOOT_CHECK(safety_thread_init(), "safety_thread_init");
    BOOT_CHECK(operational_mode_init(), "operational_mode_init");
    BOOT_CHECK(command_gateway_init(), "command_gateway_init");
    BOOT_CHECK(self_check_service_init(), "self_check_service_init");
    BOOT_CHECK(op_mode_bridge_init(), "op_mode_bridge_init");
    BOOT_CHECK(project_report_scheduler_init(), "project_report_scheduler_init");
    return SW_OK;
}

static sw_err_t bootstrap_init_adapters(void)
{
    const deploy_store_ops_t *ds = deploy_store_get_ops();

    if (ds != NULL) {
        sw_err_t r = ds->load();

        if ((r != SW_OK) && (r != SW_ERR_STORAGE)) {
            LOG_ERROR("bootstrap: deploy_store load ret=%d", (int)r);
            return r;
        }
    }

    BOOT_CHECK(project_adapters_init(), "project_adapters_init");
    return SW_OK;
}

static sw_err_t bootstrap_start_threads(void)
{
    BOOT_CHECK(thread_register("event_dispatch", event_dispatch_thread_fn, SCHED_OTHER, 0, THD_EVENT_DISPATCH_STACK),
               "register event_dispatch");
    BOOT_CHECK(project_start_threads(), "project_start_threads");
    BOOT_CHECK(scheduler_start_all(), "scheduler_start_all");
    return SW_OK;
}

sw_err_t bootstrap_run(void)
{
    sw_err_t ret;

    ret = bootstrap_init_infra();
    if (ret != SW_OK) {
        return ret;
    }

    ret = project_safety_init();
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_init_application();
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_init_adapters();
    if (ret != SW_OK) {
        return ret;
    }

    ret = bootstrap_start_threads();
    if (ret != SW_OK) {
        return ret;
    }

    LOG_INFO("bootstrap: started");
    return SW_OK;
}
