/**
 * @file    port_registry_cloud.c
 * @brief   云端 & 存储端口注册表（链路 / 上报 / 属性 / 部署配置 / 参数）
 *
 * @note    注册语义见 runtime/ports/port_registry.h。
 */

#include "application/ports/inbound/cloud/property/property_port.h"
#include "application/ports/outbound/cloud/link/cloud_link_port.h"
#include "application/ports/outbound/cloud/report/report_port.h"
#include "domain/ports/outbound/storage/deploy_store.h"
#include "domain/ports/outbound/storage/param_store.h"
#include "runtime/ports/port_registry.h"

#include <stddef.h>

/* ---- 云端链路 ---- */
static const cloud_link_ops_t *s_link_ops;

sw_err_t cloud_link_register(const cloud_link_ops_t *ops)
{
    /* 各字段由遥测投影 / 上报链路逐个判空，允许注册部分能力的链路实现 */
    s_link_ops = ops;
    return SW_OK;
}

const cloud_link_ops_t *cloud_link_get_ops(void)
{
    return s_link_ops;
}

/* ---- 云端上报 ---- */
static const cloud_report_ops_t *s_report_ops;

sw_err_t cloud_report_register(const cloud_report_ops_t *ops)
{
    s_report_ops = ops;
    return SW_OK;
}

const cloud_report_ops_t *cloud_report_get_ops(void)
{
    return s_report_ops;
}

/* ---- 云端属性下发 ---- */
static const cloud_property_ops_t *s_property_ops;

sw_err_t cloud_property_register(const cloud_property_ops_t *ops)
{
    s_property_ops = ops;
    return SW_OK;
}

const cloud_property_ops_t *cloud_property_get_ops(void)
{
    return s_property_ops;
}

/* ---- 部署配置存储 ---- */
static const deploy_store_ops_t *s_deploy_ops;

sw_err_t deploy_store_register(const deploy_store_ops_t *ops)
{
    /* bootstrap 只判 ops 非空即调 load，缺失会直接空指针解引用 */
    if ((ops != NULL) && (ops->load == NULL)) {
        return SW_ERR_PARAM;
    }
    s_deploy_ops = ops;
    return SW_OK;
}

const deploy_store_ops_t *deploy_store_get_ops(void)
{
    return s_deploy_ops;
}

/* ---- 运行参数存储 ---- */
static const param_store_ops_t *s_param_ops;

sw_err_t param_store_register(const param_store_ops_t *ops)
{
    /* 端口契约要求四个回调齐备：调用方按"取到 ops 即四者可用"使用，
     * 缺任一项都会在调用处空指针解引用，故在注册时整表拒绝 */
    if ((ops != NULL) && ((ops->load == NULL) || (ops->save == NULL) || (ops->get == NULL) || (ops->set == NULL))) {
        return SW_ERR_PARAM;
    }
    s_param_ops = ops;
    return SW_OK;
}

const param_store_ops_t *param_store_get_ops(void)
{
    return s_param_ops;
}

void port_registry_cloud_reset(void)
{
    s_link_ops     = NULL;
    s_report_ops   = NULL;
    s_property_ops = NULL;
    s_deploy_ops   = NULL;
    s_param_ops    = NULL;
}
