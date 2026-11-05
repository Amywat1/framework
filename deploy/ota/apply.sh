#!/bin/bash
# root 执行：闸门、payload SHA-256、变体与版本通过后，把发布树解到非活动槽再切 current。
# 由项目安装单元调用，不要在控制进程里跑；整包 sidecar 由安装器对账，不在本脚本。
# 本文件是板上活动槽里的上一版脚本。部署参数取同目录 ota.env；
# 宿主文件（单元、sudoers、用户等）由项目钩子 OTA_HOST_INSTALL 按槽安装。
#
# 测试旋钮（环境变量）：OTA_DRY_RUN=1 只预检；OTA_SKIP_SYSTEMCTL=1 不碰服务与宿主文件；
# OTA_START_FAIL=1 注入新槽启动失败；OTA_SKIP_DISK=1 跳过磁盘检查。

set -euo pipefail

# 切槽会改 current 指向；先解析成真实目录，钩子与公共函数始终取本版
HERE="$(cd "$(dirname "$0")" && pwd -P)"
# shellcheck source=ota_lib.sh
source "$HERE/ota_lib.sh"
ota_board_env "$HERE/ota.env" OTA_BUNDLE_NAME OTA_MAIN_UNIT OTA_REQUIRED_FILE OTA_PAYLOAD_PREFIXES

CURRENT="$OTA_SLOT_ROOT/current"
PAYLOAD="$OTA_STAGING/payload.tar.gz"
PKG_RELEASE="$OTA_STAGING/RELEASE.txt"
BUSY="$OTA_STAGING/busy"
SKIP_SYSTEMCTL="${OTA_SKIP_SYSTEMCTL:-0}"
START_FAIL="${OTA_START_FAIL:-0}"
EXTRACT_TIMEOUT_SEC=120

s_stopped=0
s_flipped=0
s_host_installed=0
s_old_slot=""
s_new_slot=""

# current 须是指向 a 或 b 的链接；定出旧槽与要写入的非活动槽
pick_slots()
{
    if [[ ! -L "$CURRENT" ]]; then
        ota_die "$CURRENT 不是符号链接，拒绝安装"
    fi
    s_old_slot="$(basename "$(readlink "$CURRENT")")"
    case "$s_old_slot" in
        a) s_new_slot=b ;;
        b) s_new_slot=a ;;
        *) ota_die "$CURRENT 未指向槽 a 或 b" ;;
    esac
}

set_current_slot()
{
    ln -sfn "$1" "$CURRENT"
}

host_install()
{
    if [[ -z "${OTA_HOST_INSTALL:-}" ]]; then
        return 0
    fi
    OTA_SLOT_ROOT="$OTA_SLOT_ROOT" bash "$HERE/$OTA_HOST_INSTALL" "$1"
}

recover_service()
{
    local u
    if [[ "$s_flipped" -eq 1 ]] && [[ -n "$s_old_slot" ]]; then
        echo "OTA: 回滚 current -> $s_old_slot" >&2
        set_current_slot "$s_old_slot" || true
    fi
    if [[ "$SKIP_SYSTEMCTL" == "1" ]]; then
        return
    fi
    if [[ "$s_host_installed" -eq 1 ]] && [[ -n "$s_old_slot" ]]; then
        echo "OTA: 回滚宿主文件 -> $s_old_slot" >&2
        host_install "$OTA_SLOT_ROOT/$s_old_slot" || true
        systemctl daemon-reload || true
    fi
    if [[ "$s_stopped" -eq 1 ]]; then
        echo "OTA: 安装失败，按旧槽拉起服务" >&2
        # restart：伴随服务若已被新槽拉起，start 是空操作，内存里仍是新进程。
        for u in ${OTA_COMPANION_UNITS:-}; do
            systemctl restart "$u" || true
        done
        systemctl start "$OTA_MAIN_UNIT" || true
    fi
}

on_exit()
{
    rm -f "$BUSY"
    recover_service
}

arm_recover()
{
    trap on_exit EXIT
    trap 'exit 143' TERM
}

disarm_recover()
{
    trap - EXIT TERM
}

# 安装单元可能在控制进程删解包残留之前就把它停掉
clean_unpack_residues()
{
    local bundle="$OTA_WORK_DIR/$OTA_BUNDLE_NAME"
    if [[ "$SKIP_SYSTEMCTL" == "1" ]]; then
        return
    fi
    rm -rf "$OTA_WORK_DIR/upgrade"
    rm -f "$bundle" "$bundle.applying" "$bundle.sha256" "$bundle.md5"
}

# 成员须落在 OTA_PAYLOAD_PREFIXES 之一之下，或是某个前缀的上级目录
payload_member_allowed()
{
    local p="${1#./}"
    local pre
    p="${p%/}"
    if [[ "$p" == "." ]] || [[ -z "$p" ]]; then
        return 0
    fi
    if [[ "$p" == *..* ]]; then
        return 1
    fi
    for pre in $OTA_PAYLOAD_PREFIXES; do
        if [[ "$p" == "$pre" ]] || [[ "$p" == "$pre"/* ]] || [[ "$pre" == "$p"/* ]]; then
            return 0
        fi
    done
    return 1
}

check_payload_members()
{
    local line
    local count=0
    local list="$OTA_STAGING/members.list"

    if ! timeout "$OTA_LIST_TIMEOUT_SEC" tar -tzf "$PAYLOAD" > "$list"; then
        rm -f "$list"
        ota_die "无法列出内包"
    fi
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        count=$((count + 1))
        if ! payload_member_allowed "$line"; then
            rm -f "$list"
            ota_die "内包成员不在允许前缀: $line"
        fi
    done < "$list"
    rm -f "$list"
    if [[ "$count" -eq 0 ]]; then
        ota_die "内包为空或无法列出"
    fi
}

check_payload_hash()
{
    local pkg_hash member inner_hash
    pkg_hash="$(ota_read_field "$PKG_RELEASE" product_hash)"
    member="$(ota_release_member "$PAYLOAD" || true)"
    if [[ -z "$member" ]] || [[ -z "$pkg_hash" ]] || [[ "$pkg_hash" == "unknown" ]]; then
        return
    fi
    inner_hash="$(tar -xOf "$PAYLOAD" "$member" | awk -F= '$1 == "product_hash" { print $2; exit }')"
    if [[ -n "$inner_hash" ]] && [[ "$inner_hash" != "$pkg_hash" ]]; then
        ota_die "payload hash 与 RELEASE 不符 package=$pkg_hash inner=$inner_hash"
    fi
}

extract_to_slot()
{
    local -a excl=()
    local e
    for e in ${OTA_PAYLOAD_EXCLUDE:-}; do
        excl+=("--exclude=$e" "--exclude=./$e")
    done
    timeout "$EXTRACT_TIMEOUT_SEC" tar -C "$1" --no-same-owner ${excl[@]+"${excl[@]}"} -xzf "$PAYLOAD"
}

if [[ ! -f "$PAYLOAD" ]] || [[ ! -f "$PKG_RELEASE" ]]; then
    ota_die "缺少 $PAYLOAD 或 $PKG_RELEASE"
fi
if [[ ! -f "$OTA_INSTALLED_RELEASE" ]]; then
    ota_die "板上没有 $OTA_INSTALLED_RELEASE，拒绝安装"
fi
ota_require_allow drop_staged

ota_require_not_symlink "$OTA_STAGING"
ota_require_not_symlink "$OTA_SLOT_ROOT"
if [[ "$SKIP_SYSTEMCTL" != "1" ]]; then
    ota_require_not_symlink "$OTA_WORK_DIR"
fi

ota_check_payload_sha "$PAYLOAD" "$PKG_RELEASE"
ota_check_variant "$PKG_RELEASE" "$OTA_INSTALLED_RELEASE"
ota_require_newer_version "$PKG_RELEASE" "$OTA_INSTALLED_RELEASE"
check_payload_hash
check_payload_members
mkdir -p "$OTA_SLOT_ROOT/a" "$OTA_SLOT_ROOT/b"
ota_check_disk "$PAYLOAD" "$OTA_SLOT_ROOT" "$OTA_STAGING"

pkg_variant="$(ota_read_field "$PKG_RELEASE" variant)"
if [[ "${OTA_DRY_RUN:-}" == "1" ]]; then
    echo "OTA: dry-run apply variant=$pkg_variant"
    exit 0
fi

pick_slots
NEW_SLOT="$OTA_SLOT_ROOT/$s_new_slot"
ota_require_not_symlink "$NEW_SLOT"
rm -rf "$NEW_SLOT"
mkdir -p "$NEW_SLOT"
echo "OTA: 解到非活动槽 $NEW_SLOT（排除 ${OTA_PAYLOAD_EXCLUDE:-无}）"
if ! extract_to_slot "$NEW_SLOT"; then
    rm -rf "$NEW_SLOT"
    ota_die "解到非活动槽失败"
fi
if [[ ! -f "$NEW_SLOT/$OTA_REQUIRED_FILE" ]]; then
    rm -rf "$NEW_SLOT"
    ota_die "非活动槽缺少 $OTA_REQUIRED_FILE"
fi
sync

arm_recover

# 解槽可能已过数秒，开洗会撤闸门；停服务前必须再看一次。
ota_require_allow drop_staged

if [[ "$SKIP_SYSTEMCTL" != "1" ]]; then
    echo "OTA: 停止 $OTA_MAIN_UNIT"
    systemctl stop "$OTA_MAIN_UNIT"
    if systemctl is-active --quiet "$OTA_MAIN_UNIT"; then
        ota_die "$OTA_MAIN_UNIT 仍在运行，放弃安装"
    fi
    s_stopped=1
    for u in ${OTA_COMPANION_UNITS:-}; do
        systemctl stop "$u" || true
    done
fi

echo "OTA: current $s_old_slot -> $s_new_slot"
set_current_slot "$s_new_slot"
s_flipped=1
sync

if [[ "$SKIP_SYSTEMCTL" == "1" ]]; then
    if [[ "$START_FAIL" == "1" ]]; then
        ota_die "注入启动失败"
    fi
    disarm_recover
    rm -f "$PAYLOAD" "$PKG_RELEASE" "$BUSY"
    echo "OTA: skip-systemctl 完成 variant=$pkg_variant slot=$s_new_slot"
    exit 0
fi

# 先置标记：钩子半途失败时回滚也按旧槽重装
s_host_installed=1
host_install "$NEW_SLOT"
rm -f "$PAYLOAD" "$PKG_RELEASE" "$BUSY"
clean_unpack_residues

systemctl daemon-reload || true
# 伴随服务等主服务 READY 后再拉：失败时它仍停着，回滚才能按旧槽起来。
if [[ "$START_FAIL" == "1" ]] || ! systemctl start "$OTA_MAIN_UNIT"; then
    ota_die "新槽未能 READY"
fi
for u in ${OTA_COMPANION_UNITS:-}; do
    systemctl try-restart "$u" || true
done
disarm_recover
echo "OTA: 完成 variant=$pkg_variant slot=$s_new_slot"
