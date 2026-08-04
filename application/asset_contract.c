/**
 * @file    asset_contract.c
 * @brief   必需资产启动期集中校验实现
 * @author  HUWANGWEI
 * @date    2026-08-03
 */

#include "application/asset_contract.h"

#include "common/log.h"
#include "domain/cloud/cloud_model.h"
#include "domain/program_engine/engine/engine_io.h"
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
static bool engine_io_catalog_present(void)
{
    const engine_io_catalog_t *cat = engine_io_get_catalog();

    if (cat == NULL) {
        return false;
    }
    return (cat->signal_count > 0U) || (cat->axis_count > 0U);
}

/* 表驱动：新增资产只在此追加一行，校验逻辑本身不变 */
static const asset_contract_entry_t k_entries[] = {
    {ASSET_REQ_ALARM_CATALOG,     "alarm_catalog",     alarm_catalog_present    },
    {ASSET_REQ_CLOUD_POINT_TABLE, "cloud_point_table", cloud_point_table_present},
    {ASSET_REQ_ENGINE_IO_CATALOG, "engine_io_catalog", engine_io_catalog_present},
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
    return "unknown";
}

sw_err_t asset_contract_validate(uint32_t required)
{
    unsigned missing_count = 0U;
    unsigned checked_count = 0U;
    unsigned i;
    uint32_t known_mask = 0U;

    if (required == 0U) {
        LOG_INFO("asset_contract: 未声明必需资产，跳过校验");
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
            LOG_ERROR("asset_contract: 必需资产缺失或为空 [%s]", e->name);
            missing_count++;
        }
    }

    /* 声明了本框架版本无法识别的位：多为项目与框架版本不一致，
     * 静默忽略会让"我声明了却没被检查"难以察觉。 */
    if ((required & ~known_mask) != 0U) {
        LOG_WARN("asset_contract: 声明中含未知资产位 0x%08X，已忽略", (unsigned)(required & ~known_mask));
    }

    if (missing_count > 0U) {
        LOG_ERROR("asset_contract: 校验失败，%u/%u 个必需资产缺失", missing_count, checked_count);
        return SW_ERR_NOT_INIT;
    }

    LOG_INFO("asset_contract: 校验通过，%u 个必需资产均已就位", checked_count);
    return SW_OK;
}
