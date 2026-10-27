/**
 * @file    demo_alarm_catalog.c
 * @brief   Demo 最小报警目录
 *
 * @note    经 alarm_binding_port 装载，不直接调 alarm_registry。
 *          产品目录由项目表生成，同样只走这一端口。
 */

#include "wiring/demo_defs.h"

#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "common/sw_error.h"

#include <stddef.h>

static const alarm_def_t s_demo_catalog[] = {
    {
     .code         = DEMO_ALARM_MAJOR,
     .level        = ALARM_LEVEL_MAJOR,
     .lifecycle    = ALARM_LIFECYCLE_LATCH,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "demo major",
     },
    {
     .code         = DEMO_ALARM_ESTOP,
     .level        = ALARM_LEVEL_CRITICAL,
     .lifecycle    = ALARM_LIFECYCLE_FOLLOW_SIGNAL,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "demo estop",
     },
};

/**
 * @brief  加载 Demo 报警目录
 *
 * @retval SW_OK           已装载
 * @retval SW_ERR_NOT_INIT 报警绑定端口尚未注册（bind 阶段顺序错了）
 * @retval 其它            目录校验失败，见 alarm_registry_load_catalog
 *
 * @note   bootstrap 的 bind 阶段保证 alarm_bridge_bind() 先于本函数执行。
 */
sw_err_t demo_alarm_catalog_load(void)
{
    const alarm_binding_ops_t *binding = alarm_binding_get_ops();

    if ((binding == NULL) || (binding->load_catalog == NULL)) {
        return SW_ERR_NOT_INIT;
    }
    return binding->load_catalog(s_demo_catalog, sizeof(s_demo_catalog) / sizeof(s_demo_catalog[0]));
}
