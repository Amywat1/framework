/**
 * @file    json_param_store.c
 * @brief   基于 JSON 的参数存储适配器实现
 * @author  HUWANGWEI
 * @date    2026-09-30
 *
 * @note    通过 param_store 端口访问本适配器，使上层不感知底层存储细节。
 *          落盘为 A/B 双槽：信封（magic/版本/世代/长度/CRC32）+ JSON payload。
 *          只写未激活槽，槽内采用临时文件 + fsync + rename + 目录 fsync。
 */

#include "adapters/outbound/storage/json/json_param_store.h"

#ifndef PARAM_STORE_JSON_FILE_PATH
#define PARAM_STORE_JSON_FILE_PATH ""
#endif
#include "common/log.h"
#include "common/util_crc.h"
#include "domain/ports/outbound/storage/param_store.h"
#include "third_party/cJSON/cJSON.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define JSON_PARAM_STORE_PATH_MAX      256U
#define JSON_PARAM_STORE_SLOT_PATH_MAX (JSON_PARAM_STORE_PATH_MAX + 8U)
#define JSON_PARAM_SLOT_COUNT          2U
#define JSON_PARAM_HEADER_SIZE         20U
#define JSON_PARAM_OFF_VERSION         4U
#define JSON_PARAM_OFF_SEQ             8U
#define JSON_PARAM_OFF_PAYLOAD_LEN     12U
#define JSON_PARAM_OFF_CRC             16U
#define JSON_PARAM_FMT_VERSION         1U
#define JSON_PARAM_PAYLOAD_MAX         65536U
#define JSON_PARAM_FILE_MODE           ((mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))

static const uint8_t k_json_param_magic[4] = {'M', '8', 'P', 'R'};

static cJSON          *s_root                                 = NULL;
static pthread_mutex_t s_mutex                                = PTHREAD_MUTEX_INITIALIZER;
static char            s_file_path[JSON_PARAM_STORE_PATH_MAX] = PARAM_STORE_JSON_FILE_PATH;
static uint32_t        s_seq                                  = 0U;
static int             s_active_slot                          = -1;
static bool            s_trusted                              = false;

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool file_is_regular(const char *path)
{
    struct stat st;

    if ((path == NULL) || (stat(path, &st) != 0)) {
        return false;
    }
    return S_ISREG(st.st_mode) != 0;
}

static sw_err_t make_slot_path(char *buf, size_t buf_size, const char *stem, unsigned slot)
{
    int n;

    if ((buf == NULL) || (stem == NULL) || (slot >= JSON_PARAM_SLOT_COUNT)) {
        return SW_ERR_PARAM;
    }
    n = snprintf(buf, buf_size, "%s.%c", stem, (slot == 0U) ? 'a' : 'b');
    if ((n < 0) || ((size_t)n >= buf_size)) {
        return SW_ERR_PARAM;
    }
    return SW_OK;
}

static sw_err_t get_file_path(char *buf, size_t buf_size)
{
    if ((buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_file_path[0] == '\0') {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }
    strncpy(buf, s_file_path, buf_size - 1U);
    buf[buf_size - 1U] = '\0';
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

static void reset_root_locked(void)
{
    if (s_root != NULL) {
        cJSON_Delete(s_root);
        s_root = NULL;
    }
    s_root        = cJSON_CreateObject();
    s_trusted     = false;
    s_seq         = 0U;
    s_active_slot = -1;
}

static bool parse_json_object(const char *buf, cJSON **out)
{
    cJSON *parsed;

    if ((buf == NULL) || (out == NULL)) {
        return false;
    }
    parsed = cJSON_Parse(buf);
    if ((parsed == NULL) || !cJSON_IsObject(parsed)) {
        if (parsed != NULL) {
            cJSON_Delete(parsed);
        }
        return false;
    }
    *out = parsed;
    return true;
}

static sw_err_t fsync_parent_dir(const char *file_path)
{
    char        dir[JSON_PARAM_STORE_SLOT_PATH_MAX];
    const char *slash;
    int         fd;

    if (file_path == NULL) {
        return SW_ERR_PARAM;
    }

    slash = strrchr(file_path, '/');
    if (slash == NULL) {
        (void)strncpy(dir, ".", sizeof(dir) - 1U);
        dir[sizeof(dir) - 1U] = '\0';
    } else if (slash == file_path) {
        dir[0] = '/';
        dir[1] = '\0';
    } else {
        size_t n = (size_t)(slash - file_path);

        if (n >= sizeof(dir)) {
            return SW_ERR_PARAM;
        }
        memcpy(dir, file_path, n);
        dir[n] = '\0';
    }

    fd = open(dir, O_RDONLY);
    if (fd < 0) {
        return SW_ERR_STORAGE;
    }
    if (fsync(fd) != 0) {
        (void)close(fd);
        return SW_ERR_STORAGE;
    }
    if (close(fd) != 0) {
        return SW_ERR_STORAGE;
    }
    return SW_OK;
}

static sw_err_t write_all_fd(int fd, const uint8_t *data, size_t len)
{
    size_t off = 0U;

    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return SW_ERR_STORAGE;
        }
        if (n == 0) {
            return SW_ERR_STORAGE;
        }
        off += (size_t)n;
    }
    return SW_OK;
}

/**
 * @brief  将完整映像原子替换到 path（同目录临时文件 + fsync + rename）。
 */
static sw_err_t write_file_atomic(const char *path, const uint8_t *data, size_t len)
{
    char tmp[JSON_PARAM_STORE_SLOT_PATH_MAX];
    int  fd;
    int  n;

    if ((path == NULL) || (data == NULL) || (len == 0U)) {
        return SW_ERR_PARAM;
    }

    n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if ((n < 0) || ((size_t)n >= sizeof(tmp))) {
        return SW_ERR_PARAM;
    }

    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, JSON_PARAM_FILE_MODE);
    if (fd < 0) {
        return SW_ERR_STORAGE;
    }

    if (write_all_fd(fd, data, len) != SW_OK) {
        (void)close(fd);
        (void)unlink(tmp);
        return SW_ERR_STORAGE;
    }
    if (fsync(fd) != 0) {
        (void)close(fd);
        (void)unlink(tmp);
        return SW_ERR_STORAGE;
    }
    if (close(fd) != 0) {
        (void)unlink(tmp);
        return SW_ERR_STORAGE;
    }
    if (rename(tmp, path) != 0) {
        (void)unlink(tmp);
        return SW_ERR_STORAGE;
    }
    if (fsync_parent_dir(path) != SW_OK) {
        return SW_ERR_STORAGE;
    }
    return SW_OK;
}

static bool try_load_slot(const char *path, uint32_t *seq_out, cJSON **root_out)
{
    struct stat st;
    uint8_t    *buf        = NULL;
    size_t      size       = 0U;
    uint32_t    version    = 0U;
    uint32_t    seq        = 0U;
    uint32_t    payload_len = 0U;
    uint32_t    crc        = 0U;
    uint32_t    got_crc    = 0U;
    FILE       *fp         = NULL;
    bool        ok         = false;
    cJSON      *parsed     = NULL;

    if ((path == NULL) || (seq_out == NULL) || (root_out == NULL)) {
        return false;
    }
    if ((stat(path, &st) != 0) || (S_ISREG(st.st_mode) == 0)) {
        return false;
    }
    if ((st.st_size < (off_t)JSON_PARAM_HEADER_SIZE)
        || (st.st_size > (off_t)(JSON_PARAM_HEADER_SIZE + JSON_PARAM_PAYLOAD_MAX))) {
        return false;
    }

    size = (size_t)st.st_size;
    buf  = (uint8_t *)malloc(size + 1U);
    if (buf == NULL) {
        return false;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        free(buf);
        return false;
    }
    if (fread(buf, 1U, size, fp) != size) {
        fclose(fp);
        free(buf);
        return false;
    }
    fclose(fp);

    if (memcmp(buf, k_json_param_magic, sizeof(k_json_param_magic)) != 0) {
        free(buf);
        return false;
    }

    version     = get_le32(buf + JSON_PARAM_OFF_VERSION);
    seq         = get_le32(buf + JSON_PARAM_OFF_SEQ);
    payload_len = get_le32(buf + JSON_PARAM_OFF_PAYLOAD_LEN);
    crc         = get_le32(buf + JSON_PARAM_OFF_CRC);

    if ((version != JSON_PARAM_FMT_VERSION) || (seq == 0U) || (payload_len == 0U)
        || (payload_len > JSON_PARAM_PAYLOAD_MAX)
        || ((size_t)JSON_PARAM_HEADER_SIZE + (size_t)payload_len != size)) {
        free(buf);
        return false;
    }

    got_crc = util_crc32(buf + JSON_PARAM_HEADER_SIZE, payload_len);
    if (got_crc != crc) {
        free(buf);
        return false;
    }

    buf[size] = '\0';
    ok        = parse_json_object((const char *)(buf + JSON_PARAM_HEADER_SIZE), &parsed);
    free(buf);
    if (!ok) {
        return false;
    }

    *seq_out  = seq;
    *root_out = parsed;
    return true;
}

static bool try_load_legacy(const char *path, cJSON **root_out)
{
    FILE  *fp  = NULL;
    long   len = 0L;
    char  *buf = NULL;
    bool   ok  = false;
    cJSON *parsed = NULL;

    if ((path == NULL) || (root_out == NULL) || !file_is_regular(path)) {
        return false;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        return false;
    }

    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    len = ftell(fp);
    rewind(fp);
    if ((len <= 0L) || (len > (long)JSON_PARAM_PAYLOAD_MAX)) {
        fclose(fp);
        return false;
    }

    buf = (char *)malloc((size_t)len + 1U);
    if (buf == NULL) {
        fclose(fp);
        return false;
    }
    if (fread(buf, 1U, (size_t)len, fp) != (size_t)len) {
        fclose(fp);
        free(buf);
        return false;
    }
    fclose(fp);
    buf[len] = '\0';

    ok = parse_json_object(buf, &parsed);
    free(buf);
    if (!ok) {
        return false;
    }
    *root_out = parsed;
    return true;
}

static bool durable_files_present(const char *stem)
{
    char slot[JSON_PARAM_STORE_SLOT_PATH_MAX];
    unsigned i;

    if (file_is_regular(stem)) {
        return true;
    }
    for (i = 0U; i < JSON_PARAM_SLOT_COUNT; i++) {
        if (make_slot_path(slot, sizeof(slot), stem, i) != SW_OK) {
            continue;
        }
        if (file_is_regular(slot)) {
            return true;
        }
    }
    return false;
}

/* -------------------------------------------------------------------------
 * ops 实现
 * ------------------------------------------------------------------------- */
static sw_err_t store_load(void)
{
    char     path[JSON_PARAM_STORE_PATH_MAX];
    char     slot_path[JSON_PARAM_SLOT_COUNT][JSON_PARAM_STORE_SLOT_PATH_MAX];
    cJSON   *slot_root[JSON_PARAM_SLOT_COUNT] = {NULL, NULL};
    uint32_t slot_seq[JSON_PARAM_SLOT_COUNT]  = {0U, 0U};
    bool     slot_ok[JSON_PARAM_SLOT_COUNT]   = {false, false};
    unsigned i;
    int      best = -1;
    cJSON   *legacy = NULL;
    sw_err_t ret    = SW_ERR_STORAGE;

    if (get_file_path(path, sizeof(path)) != SW_OK) {
        return SW_ERR_PARAM;
    }

    for (i = 0U; i < JSON_PARAM_SLOT_COUNT; i++) {
        if (make_slot_path(slot_path[i], sizeof(slot_path[i]), path, i) != SW_OK) {
            return SW_ERR_PARAM;
        }
    }

    pthread_mutex_lock(&s_mutex);
    reset_root_locked();

    for (i = 0U; i < JSON_PARAM_SLOT_COUNT; i++) {
        slot_ok[i] = try_load_slot(slot_path[i], &slot_seq[i], &slot_root[i]);
        if (slot_ok[i]) {
            if ((best < 0) || (slot_seq[i] > slot_seq[best])) {
                best = (int)i;
            }
        }
    }

    if (best >= 0) {
        cJSON_Delete(s_root);
        s_root        = slot_root[best];
        slot_root[best] = NULL;
        s_seq         = slot_seq[best];
        s_active_slot = best;
        s_trusted     = true;
        ret           = SW_OK;
        (void)unlink(path);
        LOG_INFO("json_param_store: loaded slot %c seq=%u from %s",
                 (best == 0) ? 'a' : 'b',
                 (unsigned)s_seq,
                 path);
    } else if (try_load_legacy(path, &legacy)) {
        cJSON_Delete(s_root);
        s_root        = legacy;
        s_seq         = 0U;
        s_active_slot = -1;
        s_trusted     = true;
        ret           = SW_OK;
        LOG_INFO("json_param_store: imported legacy %s", path);
    } else if (!durable_files_present(path)) {
        LOG_WARN("json_param_store: file not found (%s), using defaults", path);
    } else {
        LOG_ERROR("json_param_store: no valid image (%s), refusing persist until files are cleared",
                  path);
    }

    for (i = 0U; i < JSON_PARAM_SLOT_COUNT; i++) {
        if (slot_root[i] != NULL) {
            cJSON_Delete(slot_root[i]);
        }
    }

    pthread_mutex_unlock(&s_mutex);
    return ret;
}

static sw_err_t store_save(void)
{
    char     path[JSON_PARAM_STORE_PATH_MAX];
    char     slot_path[JSON_PARAM_STORE_SLOT_PATH_MAX];
    char    *json_str   = NULL;
    uint8_t *image      = NULL;
    size_t   payload_len = 0U;
    uint32_t seq        = 0U;
    unsigned slot       = 0U;
    uint32_t crc        = 0U;
    sw_err_t wr         = SW_ERR_STORAGE;

    if (get_file_path(path, sizeof(path)) != SW_OK) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);

    if (s_root == NULL) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_STORAGE;
    }
    if (!s_trusted && durable_files_present(path)) {
        pthread_mutex_unlock(&s_mutex);
        LOG_ERROR("json_param_store: refuse to persist untrusted tree over existing files");
        return SW_ERR_STATE;
    }

    json_str = cJSON_Print(s_root);
    if (json_str == NULL) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_STORAGE;
    }

    payload_len = strlen(json_str);
    if ((payload_len == 0U) || (payload_len > JSON_PARAM_PAYLOAD_MAX)) {
        free(json_str);
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_STORAGE;
    }

    seq  = s_seq + 1U;
    slot = (s_active_slot < 0) ? 0U : (unsigned)(1 - s_active_slot);
    if (make_slot_path(slot_path, sizeof(slot_path), path, slot) != SW_OK) {
        free(json_str);
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }

    image = (uint8_t *)malloc(JSON_PARAM_HEADER_SIZE + payload_len);
    if (image == NULL) {
        free(json_str);
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_NOMEM;
    }

    memcpy(image, k_json_param_magic, sizeof(k_json_param_magic));
    put_le32(image + JSON_PARAM_OFF_VERSION, JSON_PARAM_FMT_VERSION);
    put_le32(image + JSON_PARAM_OFF_SEQ, seq);
    put_le32(image + JSON_PARAM_OFF_PAYLOAD_LEN, (uint32_t)payload_len);
    crc = util_crc32((const uint8_t *)json_str, (uint32_t)payload_len);
    put_le32(image + JSON_PARAM_OFF_CRC, crc);
    memcpy(image + JSON_PARAM_HEADER_SIZE, json_str, payload_len);
    free(json_str);

    wr = write_file_atomic(slot_path, image, JSON_PARAM_HEADER_SIZE + payload_len);
    free(image);

    if (wr != SW_OK) {
        pthread_mutex_unlock(&s_mutex);
        LOG_ERROR("json_param_store: cannot write %s", slot_path);
        return wr;
    }

    s_seq         = seq;
    s_active_slot = (int)slot;
    s_trusted     = true;
    (void)unlink(path);
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("json_param_store: saved slot %c seq=%u", (slot == 0U) ? 'a' : 'b', (unsigned)seq);
    return SW_OK;
}

static sw_err_t store_get(const char *key, char *buf, size_t buf_size)
{
    sw_err_t ret = SW_ERR_PARAM;

    if ((key == NULL) || (buf == NULL) || (buf_size == 0U)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL) {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if (item != NULL) {
            if (cJSON_IsString(item) && (item->valuestring != NULL)) {
                strncpy(buf, item->valuestring, buf_size - 1U);
                buf[buf_size - 1U] = '\0';
                ret                = SW_OK;
            } else if (cJSON_IsNumber(item)) {
                /* 数值型键：转换为字符串返回 */
                snprintf(buf, buf_size, "%g", item->valuedouble);
                ret = SW_OK;
            }
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

static sw_err_t store_set(const char *key, const char *val)
{
    if ((key == NULL) || (val == NULL)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (s_root != NULL) {
        cJSON *item = cJSON_GetObjectItem(s_root, key);
        if (item != NULL) {
            /* 已存在：根据原始类型更新 */
            if (cJSON_IsNumber(item)) {
                cJSON_SetNumberValue(item, atof(val));
            } else {
                free(item->valuestring);
                item->valuestring = strdup(val);
            }
        } else {
            /* 新键：统一以字符串形式存储 */
            cJSON_AddStringToObject(s_root, key, val);
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * 注册
 * ------------------------------------------------------------------------- */
static const param_store_ops_t s_ops = {
    .load = store_load,
    .save = store_save,
    .get  = store_get,
    .set  = store_set,
};

void json_param_store_register(void)
{
    /* s_ops 静态定义且必填字段齐全，注册不应失败；失败即为编程错误 */
    if (param_store_register(&s_ops) != SW_OK) {
        LOG_ERROR("json_param_store: register rejected (ops incomplete)");
        return;
    }
    LOG_INFO("json_param_store: registered");
}

sw_err_t json_param_store_configure(const char *path)
{
    size_t len;

    if ((path == NULL) || (path[0] == '\0')) {
        return SW_ERR_PARAM;
    }

    len = strlen(path);
    if (len >= sizeof(s_file_path)) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    memcpy(s_file_path, path, len + 1U);
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}
