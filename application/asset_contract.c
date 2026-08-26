/**
 * @file    asset_contract.c
 * @brief   必需资产启动期集中校验实现
 * @author  HUWANGWEI
 * @date    2026-08-03
 */

#include "application/asset_contract.h"

#include "common/log.h"
#include "domain/cloud/cloud_model.h"
#include "domain/ports/outbound/program_engine/engine_environment_port.h"
#include "domain/safety/alarm_registry/alarm_registry.h"

#include <stdbool.h>
#include <stddef.h>

/** 资产是否就位的探测函数 */
typedef bool (*asset_present_fn_t)(void);

typedef struct {
    asset_requirement_t flag;
    const char         *name;
    asset_present_fn_t  present;
} asset_contract_entry_t;

static bool alarm_catalog_present(void)
{
    return alarm_registry_catalog_count() > 0U;
}

static bool cloud_point_table_present(void)
{
    return cloud_model_point_count() > 0U;
}

/* 目录本身可注册但两个数组皆空——那与未注册等价：方案加载期无法校验任何
 * signal/axis 名称，非法名称会一路通过直到求值期。故要求至少一项非空。 */
static bool engine_io_catalog_present(const engine_environment_t *environment)
{
    const engine_io_catalog_t *cat;

    if ((environment == NULL) || (engine_environment_validate(environment) != SW_OK)) {
        return false;
    }
    cat = engine_io_catalog(environment->io);
    return (cat->signal_count > 0U) || (cat->axis_count > 0U);
}

/* 表驱动：新增资产只在此追加一行，校验逻辑本身不变 */
static const asset_contract_entry_t k_entries[] = {
    {ASSET_REQ_ALARM_CATALOG,     "alarm_catalog",     alarm_catalog_present    },
    {ASSET_REQ_CLOUD_POINT_TABLE, "cloud_point_table", cloud_point_table_present},
};

#define ASSET_CONTRACT_ENTRY_COUNT (sizeof(k_entries) / sizeof(k_entries[0]))

const char *asset_contract_name(asset_requirement_t requirement)
{
    unsigned i;

    for (i = 0U; i < ASSET_CONTRACT_ENTRY_COUNT; i++) {
        if (k_entries[i].flag == requirement) {
            return k_entries[i].name;
        }
    }
    if (requirement == ASSET_REQ_ENGINE_IO_CATALOG) {
        return "engine_io_catalog";
    }
    return "unknown";
}

sw_err_t asset_contract_validate(uint32_t required, const engine_environment_t *engine_environment)
{
    unsigned missing_count = 0U;
    unsigned checked_count = 0U;
    unsigned i;
    uint32_t known_mask = 0U;

    if (required == 0U) {
        LOG_INFO("asset_contract: no required assets declared, skip");
        return SW_OK;
    }

    /* 逐项检查并全部报出，而不是遇到首个缺失就返回：
     * 接入调试时一次看到完整缺失清单，比反复启动逐个发现更省时间。 */
    for (i = 0U; i < ASSET_CONTRACT_ENTRY_COUNT; i++) {
        const asset_contract_entry_t *e = &k_entries[i];

        known_mask |= (uint32_t)e->flag;

        if ((required & (uint32_t)e->flag) == 0U) {
            continue;
        }

        checked_count++;
        if (!e->present()) {
            LOG_ERROR("asset_contract: required asset missing or empty [%s]", e->name);
            missing_count++;
        }
    }

    known_mask |= (uint32_t)ASSET_REQ_ENGINE_IO_CATALOG;
    if ((required & (uint32_t)ASSET_REQ_ENGINE_IO_CATALOG) != 0U) {
        checked_count++;
        if (!engine_io_catalog_present(engine_environment)) {
            LOG_ERROR("asset_contract: required asset missing or empty [engine_io_catalog]");
            missing_count++;
        }
    }

    /* 声明了本框架版本无法识别的位：多为项目与框架版本不一致，
     * 静默忽略会让"我声明了却没被检查"难以察觉。 */
    if ((required & ~known_mask) != 0U) {
        LOG_WARN("asset_contract: unknown asset bit 0x%08X ignored", (unsigned)(required & ~known_mask));
    }

    if (missing_count > 0U) {
        LOG_ERROR("asset_contract: validate failed, %u/%u required assets missing", missing_count, checked_count);
        return SW_ERR_NOT_INIT;
    }

    LOG_INFO("asset_contract: ok, %u required assets present", checked_count);
    return SW_OK;
}
