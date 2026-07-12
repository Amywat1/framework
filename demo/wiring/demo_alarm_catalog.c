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
     .code             = DEMO_ALARM_MAJOR,
     .level            = ALARM_LEVEL_MAJOR,
     .response         = RESP_COMPLETE_THEN_ASSESS,
     .clear            = ALARM_CLEAR_MANUAL_RESET,
     .source_kind      = ALARM_SOURCE_LEVEL,
     .reeval_group     = ALARM_REEVAL_GROUP_NONE,
     .immediate_cutout = false,
     .desc             = "demo major",
     },
    {
     .code             = DEMO_ALARM_ESTOP,
     .level            = ALARM_LEVEL_CRITICAL,
     .response         = RESP_STOP_IMMEDIATELY,
     .clear            = ALARM_CLEAR_AUTO_STATIC,
     .source_kind      = ALARM_SOURCE_LEVEL,
     .reeval_group     = ALARM_REEVAL_GROUP_NONE,
     .immediate_cutout = false,
     .desc             = "demo estop",
     },
};

static bool demo_skip_estop(uint32_t code)
{
    return code == DEMO_ALARM_ESTOP;
}

/**
 * @brief  加载 Demo 报警目录
 */
sw_err_t demo_alarm_catalog_load(void)
{
    alarm_registry_set_estop_skip_fn(demo_skip_estop);
    return alarm_registry_load_catalog(s_demo_catalog, sizeof(s_demo_catalog) / sizeof(s_demo_catalog[0]));
}
