/**
 * @file    ota_bundle.h
 * @brief   压缩包 OTA 本机安装器：闸门文件、安装锁、落盘巡检与交接安装脚本
 *
 * @note    Linux 文件系统实现，供 ota_gate 作安装器。云通道把 `<work_dir>/<bundle_name>`
 *          落盘后不自动解包。巡检要求：文件大小连续两拍不变、`tar -tzf` 能列出且成员都在
 *          `upgrade/` 下、闸门 `<state_dir>/allow` 存在、旁路 sidecar（`.sha256` 或 `.md5`）
 *          与整包摘要一致。缺 sidecar 时调 write_sidecar 补写，补不出则等待。
 *          通过后改名为 `.applying`，解到 `<work_dir>/upgrade/` 并执行 `upgrade.sh`。
 *          半截包、非法成员、摘要不符会闩住，等文件大小变化或闸门再现再试；
 *          安装失败把文件改回原名，闸门再现再试同一份包。
 *          安装锁 `<state_dir>/busy` 由安装脚本写入、安装单元结束时删除；
 *          configure 时若安装单元已不在运行，清掉残留锁。
 *          暂存目录不要放 /tmp 或 /var/tmp：服务的 PrivateTmp 会让安装单元看不见。
 */

#ifndef ADAPTERS_OUTBOUND_OTA_OTA_BUNDLE_H
#define ADAPTERS_OUTBOUND_OTA_OTA_BUNDLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "application/ota_gate.h"
#include "common/sw_error.h"

/** @brief 巡检周期（毫秒） */
#define OTA_BUNDLE_POLL_MS 1000U
/** @brief 派生路径缓冲上限（含结尾 NUL） */
#define OTA_BUNDLE_PATH_MAX 256U

/**
 * @brief  按云端任务摘要在压缩包旁写 sidecar
 * @param  bundle_path 落盘压缩包路径
 * @return SW_OK 已写入；其它值表示当前没有可用摘要
 */
typedef sw_err_t (*ota_bundle_sidecar_fn_t)(const char *bundle_path);

/**
 * @brief  安装器配置
 * @note   路径与单元名会拼进 shell 命令：须为绝对路径，不得含单引号、换行。
 */
typedef struct {
    const char             *state_dir;     /**< 闸门 allow 与安装锁 busy 所在目录 */
    const char             *work_dir;      /**< 云通道落盘与解包目录 */
    const char             *bundle_name;   /**< 落盘文件名，不得含 `/` */
    const char             *install_unit;  /**< 安装单元名，用于判断安装锁是否残留 */
    ota_bundle_sidecar_fn_t write_sidecar; /**< 可为 NULL：只认已有 sidecar */
} ota_bundle_cfg_t;

/**
 * @brief  登记配置并清理残留安装锁（只存指针，须静态或与模块同寿）
 * @param  cfg 配置，不可为空
 * @retval SW_OK        已就绪
 * @retval SW_ERR_PARAM 字段为空、非绝对路径、含非法字符或派生路径超长
 * @note   须在 ota_gate_init 与调度启动之前调用。
 */
sw_err_t ota_bundle_configure(const ota_bundle_cfg_t *cfg);

/**
 * @brief  取安装器操作表，供 ota_gate_cfg_t.installer
 * @return 静态操作表；未 configure 时写闸门与查锁均不做事
 */
const ota_installer_ops_t *ota_bundle_installer(void);

/**
 * @brief  巡检一次落盘
 * @note   会阻塞执行 tar、摘要计算与 upgrade.sh，各自带超时。只应在周期任务线程调用。
 *         未 configure 时直接返回。
 */
void ota_bundle_poll(void);

/**
 * @brief  登记巡检周期任务（须在 scheduler_start_all 前）
 * @retval SW_OK 已登记
 * @retval 其它  周期任务表满或参数非法
 */
sw_err_t ota_bundle_register_task(void);

/**
 * @brief  清空配置与巡检状态，仅供测试隔离
 */
void ota_bundle_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_OTA_OTA_BUNDLE_H */
