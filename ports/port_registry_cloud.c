/**
 * @file    port_registry_cloud.c
 * @brief   云端 & 存储端口注册表（链路 / 上报 / 属性 / 部署配置 / 参数）
 */

#include "ports/inbound/cloud/property/property_port.h"
#include "ports/outbound/cloud/link/cloud_link_port.h"
#include "ports/outbound/cloud/report/report_port.h"
#include "ports/outbound/storage/deploy_store.h"
#include "ports/outbound/storage/param_store.h"

/* ---- 云端链路 ---- */
static const cloud_link_ops_t *s_link_ops;
void cloud_link_register(const cloud_link_ops_t *ops) { s_link_ops = ops; }
const cloud_link_ops_t *cloud_link_get_ops(void) { return s_link_ops; }

/* ---- 云端上报 ---- */
static const cloud_report_ops_t *s_report_ops;
void cloud_report_register(const cloud_report_ops_t *ops) { s_report_ops = ops; }
const cloud_report_ops_t *cloud_report_get_ops(void) { return s_report_ops; }

/* ---- 云端属性下发 ---- */
static const cloud_property_ops_t *s_property_ops;
void cloud_property_register(const cloud_property_ops_t *ops) { s_property_ops = ops; }
const cloud_property_ops_t *cloud_property_get_ops(void) { return s_property_ops; }

/* ---- 部署配置存储 ---- */
static const deploy_store_ops_t *s_deploy_ops;
void deploy_store_register(const deploy_store_ops_t *ops) { s_deploy_ops = ops; }
const deploy_store_ops_t *deploy_store_get_ops(void) { return s_deploy_ops; }

/* ---- 运行参数存储 ---- */
static const param_store_ops_t *s_param_ops;
void param_store_register(const param_store_ops_t *ops) { s_param_ops = ops; }
const param_store_ops_t *param_store_get_ops(void) { return s_param_ops; }
