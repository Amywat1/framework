/**
 * @file    demo_main.c
 * @brief   Demo 项目入口，验证框架模块集成
 */

#include "common/log.h"
#include "common/time_util.h"
#include "ports/outbound/hal/hal_io_port.h"
#include "runtime/bootstrap/wiring.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"
#include "runtime/scheduler/scheduler.h"
#include "services/param/svc_param.h"
#include "sw_version.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static volatile int s_scheduler_tick_count;

static void demo_periodic_tick(void *ctx)
{
    (void)ctx;
    s_scheduler_tick_count++;
}

int main(void)
{
    printf("========================================\n");
    printf("  Demo - Framework Integration\n");
    printf("  Framework: %s\n", SW_PROJECT_NAME);
    printf("  Version: %s (%s)\n", SW_VERSION_STR, SW_GIT_HASH);
    printf("========================================\n");

    time_util_init();

    sw_err_t ret = event_bus_init();
    if (ret != SW_OK) {
        fprintf(stderr, "[Demo] event_bus_init failed ret=%d\n", (int)ret);
        return 1;
    }

    ret = wiring();
    if (ret != SW_OK) {
        fprintf(stderr, "[Demo] wiring failed ret=%d\n", (int)ret);
        return 1;
    }

    const hal_io_ops_t *io = hal_io_get_ops();
    if ((io != NULL) && (io->init != NULL)) {
        ret = io->init();
        if (ret != SW_OK) {
            fprintf(stderr, "[Demo] hal_io init failed ret=%d\n", (int)ret);
            return 1;
        }
    }

    ret = svc_param_init();
    if ((ret != SW_OK) && (ret != SW_ERR_STORAGE)) {
        fprintf(stderr, "[Demo] svc_param_init failed ret=%d\n", (int)ret);
        return 1;
    }

    ret = svc_param_set_int(PARAM_KEY_WASH_MODE, 1);
    if (ret != SW_OK) {
        fprintf(stderr, "[Demo] svc_param_set_int failed ret=%d\n", (int)ret);
        return 1;
    }

    if (svc_param_get_int(PARAM_KEY_WASH_MODE, 0) != 1) {
        fprintf(stderr, "[Demo] svc_param_get_int mismatch\n");
        return 1;
    }

    ret = svc_param_save();
    if (ret != SW_OK) {
        fprintf(stderr, "[Demo] svc_param_save failed ret=%d\n", (int)ret);
        return 1;
    }

    ret = periodic_task_register("demo_tick", 10U, demo_periodic_tick, NULL, SCHED_OTHER, 0, THD_SENSOR_POLL_STACK);
    if (ret != SW_OK) {
        fprintf(stderr, "[Demo] periodic_task_register failed ret=%d\n", (int)ret);
        return 1;
    }

    ret = scheduler_start_all();
    if (ret != SW_OK) {
        fprintf(stderr, "[Demo] scheduler_start_all failed ret=%d\n", (int)ret);
        return 1;
    }

    usleep(50000U);
    if (s_scheduler_tick_count < 1) {
        fprintf(stderr, "[Demo] scheduler periodic task did not run\n");
        return 1;
    }

    LOG_INFO("Demo: framework integration OK");
    printf("[Demo] All checks passed.\n");
    return 0;
}
