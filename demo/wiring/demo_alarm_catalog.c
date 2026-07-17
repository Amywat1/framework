/**
 * @file    demo_alarm_catalog.c
 * @brief   Demo 最小报警目录
 */

#include "common/sw_error.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"

#define DEMO_ALARM_MAJOR 201101U
#define DEMO_ALARM_ESTOP 201709U

static const alarm_def_t s_demo_catalog[] = {
    {
     .code         = DEMO_ALARM_MAJOR,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "demo major",
     },
    {
     .code         = DEMO_ALARM_ESTOP,
     .level        = ALARM_LEVEL_CRITICAL,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "demo estop",
     },
};

/**
 * @brief  加载 Demo 报警目录
 */
sw_err_t demo_alarm_catalog_load(void)
{
    return alarm_registry_load_catalog(s_demo_catalog, sizeof(s_demo_catalog) / sizeof(s_demo_catalog[0]));
}
