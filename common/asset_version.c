/**
 * @file    asset_version.c
 * @brief   项目资产 schema 版本校验实现
 * @author  HUWANGWEI
 * @date    2026-08-03
 */

#include "common/asset_version.h"

#include <stddef.h>
#include <stdio.h>

#define ASSET_VERSION_FIELD_MAX 65535U

/**
 * @brief  解析一段十进制数字，推进指针
 * @param  cursor  输入指针的地址；成功时推进到数字之后
 * @param  out     解析结果
 * @retval true    至少读到一位数字且未溢出
 */
static bool parse_field(const char **cursor, uint16_t *out)
{
    const char *p      = *cursor;
    uint32_t    value  = 0U;
    unsigned    digits = 0U;

    while ((*p >= '0') && (*p <= '9')) {
        value = (value * 10U) + (uint32_t)(*p - '0');
        if (value > ASSET_VERSION_FIELD_MAX) {
            return false;
        }
        digits++;
        p++;
    }

    if (digits == 0U) {
        return false;
    }

    *out    = (uint16_t)value;
    *cursor = p;
    return true;
}

asset_version_t asset_version_parse(const char *text)
{
    asset_version_t out = {0U, 0U, 0U, false};
    const char     *p   = text;

    if (text == NULL) {
        return out;
    }

    if (!parse_field(&p, &out.major)) {
        return (asset_version_t){0U, 0U, 0U, false};
    }
    if (*p != '.') {
        return (asset_version_t){0U, 0U, 0U, false};
    }
    p++;
    if (!parse_field(&p, &out.minor)) {
        return (asset_version_t){0U, 0U, 0U, false};
    }

    /* 修订号可选 */
    if (*p == '.') {
        p++;
        if (!parse_field(&p, &out.patch)) {
            return (asset_version_t){0U, 0U, 0U, false};
        }
    }

    /* 必须恰好在串尾结束：多余字符说明格式不是预期的机器生成结果 */
    if (*p != '\0') {
        return (asset_version_t){0U, 0U, 0U, false};
    }

    out.valid = true;
    return out;
}

bool asset_version_is_compatible(asset_version_t asset, asset_version_t supported)
{
    if (!asset.valid || !supported.valid) {
        return false;
    }
    if (asset.major != supported.major) {
        return false;
    }
    /* 资产次版本高于框架：资产可能含框架不认识的字段，拒绝加载 */
    return asset.minor <= supported.minor;
}

sw_err_t asset_version_check(const char *asset_name,
                             const char *asset_text,
                             const char *supported_text,
                             char       *err,
                             unsigned    errsz)
{
    asset_version_t asset     = asset_version_parse(asset_text);
    asset_version_t supported = asset_version_parse(supported_text);
    const char     *name      = (asset_name != NULL) ? asset_name : "asset";

    if (!supported.valid) {
        /* 框架自身的支持版本串写错，属于框架缺陷而非资产问题 */
        if ((err != NULL) && (errsz > 0U)) {
            (void)snprintf(err,
                           (size_t)errsz,
                           "%s: 框架支持版本串非法 [%s]",
                           name,
                           (supported_text != NULL) ? supported_text : "(null)");
        }
        return SW_ERR_PARAM;
    }

    if (!asset.valid) {
        if ((err != NULL) && (errsz > 0U)) {
            (void)snprintf(err,
                           (size_t)errsz,
                           "%s: 版本串无法解析 [%s]，期望 主.次[.修订]",
                           name,
                           (asset_text != NULL) ? asset_text : "(null)");
        }
        return SW_ERR_PARAM;
    }

    if (!asset_version_is_compatible(asset, supported)) {
        if ((err != NULL) && (errsz > 0U)) {
            (void)snprintf(err,
                           (size_t)errsz,
                           "%s: 版本不兼容，资产 %u.%u.%u 框架支持 %u.%u.x",
                           name,
                           (unsigned)asset.major,
                           (unsigned)asset.minor,
                           (unsigned)asset.patch,
                           (unsigned)supported.major,
                           (unsigned)supported.minor);
        }
        return SW_ERR_STATE;
    }

    return SW_OK;
}
