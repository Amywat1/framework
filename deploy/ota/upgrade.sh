#!/bin/bash
# 外层包 upgrade/ 内的安装预检。本机安装器（ota_bundle）解包后执行。
# 闸门、payload SHA-256、变体与版本通过后暂存 payload，再以不阻塞方式拉起安装单元。
# 整包 sidecar 由安装器对账，不在本脚本。部署参数取同目录 ota.env（打包时由项目提供）。
# 暂存目录不要放 /tmp 或 /var/tmp：控制进程的 PrivateTmp 会让安装单元看不见。

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=ota_lib.sh
source "$HERE/ota_lib.sh"
ota_board_env "$HERE/ota.env" OTA_INSTALL_UNIT

BUSY="$OTA_STAGING/busy"

ota_on_die()
{
    if [[ "$HERE" == "$OTA_WORK_DIR/upgrade" ]]; then
        rm -rf "$HERE"
    fi
}

# --no-block：安装单元里会停控制进程。若在控制进程里阻塞等待，会和 stop 互相等。
# sudoers 按完整参数匹配，须放行这两条原文。
start_install_unit()
{
    if command -v sudo >/dev/null 2>&1; then
        sudo -n /bin/systemctl start --no-block "$OTA_INSTALL_UNIT" \
            || sudo -n /usr/bin/systemctl start --no-block "$OTA_INSTALL_UNIT"
    else
        /bin/systemctl start --no-block "$OTA_INSTALL_UNIT"
    fi
}

ota_require_not_symlink "$OTA_STAGING"
mkdir -p "$OTA_STAGING"

if [[ ! -f "$HERE/payload.tar.gz" ]] || [[ ! -f "$HERE/RELEASE.txt" ]]; then
    ota_die "包内缺少 payload.tar.gz 或 RELEASE.txt"
fi
if [[ ! -f "$OTA_INSTALLED_RELEASE" ]]; then
    ota_die "板上没有 $OTA_INSTALLED_RELEASE"
fi
ota_require_allow
ota_check_payload_sha "$HERE/payload.tar.gz" "$HERE/RELEASE.txt"
ota_check_variant "$HERE/RELEASE.txt" "$OTA_INSTALLED_RELEASE"
ota_require_newer_version "$HERE/RELEASE.txt" "$OTA_INSTALLED_RELEASE"
ota_check_disk "$HERE/payload.tar.gz" "$OTA_STAGING"

cp "$HERE/payload.tar.gz" "$OTA_STAGING/payload.tar.gz"
cp "$HERE/RELEASE.txt" "$OTA_STAGING/RELEASE.txt"

if [[ "${OTA_DRY_RUN:-}" == "1" ]]; then
    echo "OTA: dry-run staged $OTA_STAGING"
    exit 0
fi

ota_require_allow drop_staged
printf '1\n' > "$BUSY"

# 跑的是板上活动槽里已装的 apply.sh（上一版），不是本包里的那份。
if ! start_install_unit; then
    ota_die "无法启动 $OTA_INSTALL_UNIT"
fi
