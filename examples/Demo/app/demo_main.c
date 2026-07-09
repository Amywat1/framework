/**
 * @file    demo_main.c
 * @brief   Demo 示例入口，验证 framework 同仓集成
 */

#include "config/demo_config.h"
#include "framework/common/log.h"
#include "framework/common/time_util.h"
#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/runtime/bootstrap/wiring.h"
#include "framework/runtime/event_bus/event_bus.h"

#include <stdio.h>

int main(void)
{
    printf("========================================\n");
    printf("  Demo - Framework Integration\n");
    printf("  Version: %s\n", DEMO_VERSION);
    printf("========================================\n");

    time_util_init();

    sw_err_t ret = event_bus_init();
    if (ret != SW_OK)
    {
        fprintf(stderr, "[Demo] event_bus_init failed ret=%d\n", (int)ret);
        return 1;
    }

    ret = wiring();
    if (ret != SW_OK)
    {
        fprintf(stderr, "[Demo] wiring failed ret=%d\n", (int)ret);
        return 1;
    }

    const hal_io_ops_t *io = hal_io_get_ops();
    if ((io != NULL) && (io->init != NULL))
    {
        ret = io->init();
        if (ret != SW_OK)
        {
            fprintf(stderr, "[Demo] hal_io init failed ret=%d\n", (int)ret);
            return 1;
        }
    }

    LOG_INFO("Demo: framework integration OK");
    printf("[Demo] All checks passed.\n");
    return 0;
}
