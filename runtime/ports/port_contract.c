/**
 * @file    port_contract.c
 * @brief   必需端口启动期集中校验实现
 * @author  HUWANGWEI
 * @date    2026-08-03
 */

#include "runtime/ports/port_contract.h"

#include "application/ports/inbound/cloud/property/property_port.h"
#include "application/ports/inbound/command/command_port.h"
#include "application/ports/inbound/safety/alarm_binding_port.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "application/ports/outbound/cloud/report/report_port.h"
#include "common/log.h"
#include "domain/ports/outbound/device/device_ops_port.h"
#include "domain/ports/outbound/hal/hal_io_port.h"
#include "domain/ports/outbound/hal/hal_sensor_port.h"
#include "domain/ports/outbound/hal/hal_vfd_port.h"
#include "domain/ports/outbound/hal/hal_voice_port.h"
#include "domain/ports/outbound/safety/safety_port.h"
#include "domain/ports/outbound/storage/deploy_store.h"
#include "domain/ports/outbound/storage/engine_program_loader_port.h"
#include "domain/ports/outbound/storage/param_store.h"

#include <stdbool.h>
#include <stddef.h>

/** 端口是否已注册的探测函数 */
typedef bool (*port_present_fn_t)(void);

typedef struct {
    port_requirement_t flag;
    const char        *name;
    port_present_fn_t  present;
} port_contract_entry_t;

static bool io_present(void)
{
    return hal_io_get_ops() != NULL;
}
static bool sensor_present(void)
{
    return hal_sensor_get_ops() != NULL;
}
static bool vfd_present(void)
{
    return hal_vfd_get_ops() != NULL;
}
static bool voice_present(void)
{
    return hal_voice_get_ops() != NULL;
}
static bool command_present(void)
{
    return device_command_port_get_ops() != NULL;
}
static bool alarm_binding_present(void)
{
    return alarm_binding_get_ops() != NULL;
}
static bool device_ops_present(void)
{
    return device_ops_get() != NULL;
}
static bool cloud_link_present(void)
{
    return cloud_link_get_ops() != NULL;
}
static bool cloud_report_present(void)
{
    return cloud_report_get_ops() != NULL;
}
static bool cloud_property_present(void)
{
    return cloud_property_get_ops() != NULL;
}
static bool deploy_store_present(void)
{
    return deploy_store_get_ops() != NULL;
}
static bool param_store_present(void)
{
    return param_store_get_ops() != NULL;
}
static bool program_loader_present(void)
{
    return engine_program_loader_get_ops() != NULL;
}
static bool safety_present(void)
{
    return safety_port_get_ops() != NULL;
}

/* 表驱动：新增端口只在此追加一行，校验逻辑本身不变 */
static const port_contract_entry_t k_entries[] = {
    {PORT_REQ_HAL_IO,         "hal_io",                io_present            },
    {PORT_REQ_HAL_SENSOR,     "hal_sensor",            sensor_present        },
    {PORT_REQ_HAL_VFD,        "hal_vfd",               vfd_present           },
    {PORT_REQ_HAL_VOICE,      "hal_voice",             voice_present         },
    {PORT_REQ_DEVICE_COMMAND, "device_command",        command_present       },
    {PORT_REQ_ALARM_BINDING,  "alarm_binding",         alarm_binding_present },
    {PORT_REQ_DEVICE_OPS,     "device_ops",            device_ops_present    },
    {PORT_REQ_CLOUD_LINK,     "cloud_link",            cloud_link_present    },
    {PORT_REQ_CLOUD_REPORT,   "cloud_report",          cloud_report_present  },
    {PORT_REQ_CLOUD_PROPERTY, "cloud_property",        cloud_property_present},
    {PORT_REQ_DEPLOY_STORE,   "deploy_store",          deploy_store_present  },
    {PORT_REQ_PARAM_STORE,    "param_store",           param_store_present   },
    {PORT_REQ_PROGRAM_LOADER, "engine_program_loader", program_loader_present},
    {PORT_REQ_SAFETY,         "safety",                safety_present        },
};

#define PORT_CONTRACT_ENTRY_COUNT (sizeof(k_entries) / sizeof(k_entries[0]))

const char *port_contract_name(port_requirement_t requirement)
{
    unsigned i;

    for (i = 0U; i < PORT_CONTRACT_ENTRY_COUNT; i++) {
        if (k_entries[i].flag == requirement) {
            return k_entries[i].name;
        }
    }
    return "unknown";
}

sw_err_t port_contract_validate(uint32_t required)
{
    unsigned missing_count = 0U;
    unsigned checked_count = 0U;
    unsigned i;
    uint32_t known_mask = 0U;

    if (required == 0U) {
        LOG_INFO("port_contract: 未声明必需端口，跳过校验");
        return SW_OK;
    }

    /* 逐项检查并全部报出，而不是遇到首个缺失就返回：
     * 接入调试时一次看到完整缺失清单，比反复启动逐个发现更省时间。 */
    for (i = 0U; i < PORT_CONTRACT_ENTRY_COUNT; i++) {
        const port_contract_entry_t *e = &k_entries[i];

        known_mask |= (uint32_t)e->flag;

        if ((required & (uint32_t)e->flag) == 0U) {
            continue;
        }

        checked_count++;
        if (!e->present()) {
            LOG_ERROR("port_contract: 必需端口未注册 [%s]", e->name);
            missing_count++;
        }
    }

    /* 声明了本框架版本无法识别的位：多为项目与框架版本不一致，
     * 静默忽略会让"我声明了却没被检查"难以察觉。 */
    if ((required & ~known_mask) != 0U) {
        LOG_WARN("port_contract: 声明中含未知端口位 0x%08X，已忽略", (unsigned)(required & ~known_mask));
    }

    if (missing_count > 0U) {
        LOG_ERROR("port_contract: 校验失败，%u/%u 个必需端口缺失", missing_count, checked_count);
        return SW_ERR_NOT_INIT;
    }

    LOG_INFO("port_contract: 校验通过，%u 个必需端口均已注册", checked_count);
    return SW_OK;
}
