#ifndef DOMAIN_SERVICES_PROGRAM_SNAPSHOT_SERVICE_H
#define DOMAIN_SERVICES_PROGRAM_SNAPSHOT_SERVICE_H

#include "domain/model/program_snapshot.h"
#include "domain/model/wash_program.h"
#include "domain/ports/program_repository_port.h"
#include "shared/result_types.h"

/**
 * @file program_snapshot_service.h
 * @brief 瀹氫箟鍚姩鍓嶇▼搴忓揩鐓ф牎楠屾湇鍔°€?
 */

/**
 * @brief 鎻忚堪绋嬪簭蹇収鏍￠獙鏈嶅姟鎵€闇€鐨勬渶灏忚緭鍏ラ泦鍚堛€?
 */
typedef struct program_snapshot_service_args_t {
    program_snapshot_t *program_snapshot;
    wash_program_t *wash_program;
    program_repository_port_t *program_repository_port;
    unsigned long current_time_ms;
} program_snapshot_service_args_t;

/**
 * @brief 鎶撳彇骞舵牎楠屽惎鍔ㄦ墍闇€鐨勭▼搴忓揩鐓с€?
 *
 * @param program_snapshot_service_args 绋嬪簭蹇収鏈嶅姟杈撳叆锛屼笉鑳戒负绌恒€?
 * @param program_id 鐩爣绋嬪簭鏍囪瘑锛屼笉鑳戒负绌恒€?
 * @return 鎴愬姛杩斿洖 `operation_result_ok()`锛涘弬鏁伴潪娉曘€佺▼搴忎笉鍙敤鎴栧揩鐓т笉鍚堟硶鏃惰繑鍥炲け璐ョ粨鏋溿€?
 */
operation_result_t program_snapshot_service_capture(program_snapshot_service_args_t *program_snapshot_service_args,
    const char *program_id);

#endif
