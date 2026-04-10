/**
 * @file    svc_param.c
 * @brief   运行时参数管理实现（基于 cJSON）
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "svc_param.h"
#include "common/log.h"
#include "tools/cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

static cJSON          *s_root  = NULL;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * 内部辅助：从文件加载 JSON
 * ------------------------------------------------------------------------- */
static sw_err_t load_from_file(void)
{
    FILE  *fp;
    long   len;
    char  *buf = NULL;
    sw_err_t ret = SW_ERR_STORAGE;

    fp = fopen(SVC_PARAM_FILE_PATH, "r");
    if (fp == NULL) {
        LOG_WARN("svc_param: param file not found, using defaults");
        return SW_ERR_STORAGE;
    }

    fseek(fp, 0L, SEEK_END);
    len = ftell(fp);
    rewind(fp);

    if (len <= 0L) {
        fclose(fp);
        return SW_ERR_STORAGE;
    }

    buf = (char *)malloc((size_t)len + 1U);
    if (buf == NULL) {
        fclose(fp);
        return SW_ERR_NOMEM;
    }

    if (fread(buf, 1U, (size_t)len, fp) == (size_t)len) {
        buf[len] = '\0';
        s_root = cJSON_Parse(buf);
        if (s_root != NULL) {
            ret = SW_OK;
        }
    }

    free(buf);
    fclose(fp);
    return ret;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t svc_param_init(void)
{
    pthread_mutex_lock(&s_mutex);

    if (s_root != NULL) {
        cJSON_Delete(s_root);
    }
    s_root = cJSON_CreateObject();

    sw_err_t ret = load_from_file();

    pthread_mutex_unlock(&s_mutex);

    if (ret == SW_OK) {
        LOG_INFO("svc_param: loaded from %s", SVC_PARAM_FILE_PATH);
    }
    return ret;
}

int svc_param_get_int(const char *key, int default_val)
{
    int val = default_val;

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL) {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if ((item != NULL) && cJSON_IsNumber(item)) {
            val = (int)item->valuedouble;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return val;
}

sw_err_t svc_param_get_str(const char *key, char *buf, int buf_size,
                            const char *default_val)
{
    if ((buf == NULL) || (buf_size <= 0)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    bool found = false;
    if (s_root != NULL) {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if ((item != NULL) && cJSON_IsString(item) && (item->valuestring != NULL)) {
            strncpy(buf, item->valuestring, (size_t)(buf_size - 1));
            buf[buf_size - 1] = '\0';
            found = true;
        }
    }
    pthread_mutex_unlock(&s_mutex);

    if (!found) {
        strncpy(buf, (default_val != NULL) ? default_val : "",
                (size_t)(buf_size - 1));
        buf[buf_size - 1] = '\0';
    }
    return SW_OK;
}

sw_err_t svc_param_set_int(const char *key, int val)
{
    if (key == NULL) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    cJSON *item = cJSON_GetObjectItem(s_root, key);
    if (item != NULL) {
        cJSON_SetNumberValue(item, (double)val);
    } else {
        cJSON_AddNumberToObject(s_root, key, (double)val);
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t svc_param_set_str(const char *key, const char *val)
{
    if ((key == NULL) || (val == NULL)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    cJSON *item = cJSON_GetObjectItem(s_root, key);
    if (item != NULL) {
        /* cJSON 内部会 strdup，安全替换 */
        free(item->valuestring);
        item->valuestring = strdup(val);
    } else {
        cJSON_AddStringToObject(s_root, key, val);
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t svc_param_save(void)
{
    FILE *fp;
    char *str;

    pthread_mutex_lock(&s_mutex);
    str = cJSON_PrintUnformatted(s_root);
    pthread_mutex_unlock(&s_mutex);

    if (str == NULL) {
        return SW_ERR_STORAGE;
    }

    fp = fopen(SVC_PARAM_FILE_PATH, "w");
    if (fp == NULL) {
        free(str);
        LOG_ERROR("svc_param_save: cannot open %s", SVC_PARAM_FILE_PATH);
        return SW_ERR_STORAGE;
    }

    fputs(str, fp);
    fclose(fp);
    free(str);

    LOG_INFO("svc_param saved to %s", SVC_PARAM_FILE_PATH);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * CLI 调试接口（param 命令域）
 * ------------------------------------------------------------------------- */
int param_debug_ctl(char *cmd, char *p1, char *p2)
{
    if (cmd == NULL) { return 0; }

    if (strcmp(cmd, "get") == 0) {
        if (p1 == NULL) { return 0; }
        char buf[64] = {0};
        int  ival = svc_param_get_int(p1, INT32_MIN);
        if (ival != INT32_MIN) {
            LOG_INFO("param get: %s = %d", p1, ival);
        } else {
            (void)svc_param_get_str(p1, buf, sizeof(buf), "(not found)");
            LOG_INFO("param get: %s = %s", p1, buf);
        }
        return 1;
    }

    if (strcmp(cmd, "set") == 0) {
        if (p1 == NULL || p2 == NULL) { return 0; }
        (void)svc_param_set_int(p1, atoi(p2));
        LOG_INFO("param set: %s = %s", p1, p2);
        return 1;
    }

    if (strcmp(cmd, "save") == 0) {
        sw_err_t ret = svc_param_save();
        LOG_INFO("param save: %s", (ret == SW_OK) ? "ok" : "failed");
        return 1;
    }

    return 0;
}
