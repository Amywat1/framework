/**
 * @file    ota_gate.h
 * @brief   远程升级门闩：何时向云端报版本、何时允许安装、OTA 期间是否挡开洗
 *
 * @note    可选。项目 ota_gate_bind 登记版本串、云通道与安装器，再调 ota_gate_init。
 *          允许态只认 STANDBY，或无急停的 STOPPED。进入允许态写安装闸门，离开即撤。
 *          闸门只跟允许态，与是否绑云通道、云是否在线无关。在线且允许时报一次版本，
 *          离开允许态后清掉已报标记，下次进入再报。订阅无法退订，已在飞的下载靠闸门挡安装。
 *          本模块不做文件 IO，不解包，不决定版本串格式。
 */

#ifndef APPLICATION_OTA_GATE_H
#define APPLICATION_OTA_GATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "application/ports/outbound/cloud/ota/ota_channel_port.h"
#include "common/sw_error.h"
#include "domain/op_mode/op_mode_types.h"

#include <stdbool.h>

/**
 * @brief  本机安装器
 */
typedef struct {
    /**
     * @brief  写入或撤销安装闸门
     * @param  allowed true 写入；false 撤销
     * @note   必填。失败由实现自行记日志，安装脚本见不到闸门即拒装。
     */
    void (*set_allowed)(bool allowed);

    /**
     * @brief  是否正在交接安装或安装锁仍在
     * @note   必填。为真时拒绝开洗。
     */
    bool (*is_installing)(void);
} ota_installer_ops_t;

/**
 * @brief  门闩绑定
 */
typedef struct {
    const char                *version;   /**< 上报版本串，须与云端登记的固件版本一致；有通道时必填 */
    const ota_channel_ops_t   *channel;   /**< 可为 NULL：不上报 */
    const ota_installer_ops_t *installer; /**< 可为 NULL：不写闸门（如仿真） */
} ota_gate_cfg_t;

/**
 * @brief  登记版本串、云通道与安装器（只存指针，须静态或与模块同寿）
 * @param  cfg 绑定；NULL 解除绑定
 * @retval SW_OK        已登记或已解除
 * @retval SW_ERR_PARAM 有通道但缺 report 或版本串为空；有安装器但缺回调
 * @note   重新绑定会清掉已报标记。须在 ota_gate_init 之前调用。
 */
sw_err_t ota_gate_bind(const ota_gate_cfg_t *cfg);

/**
 * @brief  订阅云链路与运营态事件，并按当前状态同步一次
 * @retval SW_OK 已订阅（重复调用只同步，不重复订阅）
 * @retval 其它  事件总线订阅失败
 */
sw_err_t ota_gate_init(void);

/**
 * @brief  给定运营态是否允许报版本并安装
 * @param  mode          当前整机态
 * @param  estop_active  急停是否有效
 * @return 仅 STANDBY，或无急停的 STOPPED 为 true
 */
bool ota_gate_is_allowed(operational_mode_t mode, bool estop_active);

/**
 * @brief  OTA 进行中是否应拒绝开洗
 * @return 通道正在下载，或安装器正在交接/安装锁仍在时为 true
 */
bool ota_gate_blocks_start(void);

/**
 * @brief  按当前整机态、急停与云在线维护闸门并尝试上报
 * @note   事件回调使用；单测可直接调用。
 */
void ota_gate_sync(void);

/**
 * @brief  清空绑定、订阅标记与已报标记，仅供测试隔离
 */
void ota_gate_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_OTA_GATE_H */
