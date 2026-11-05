#!/usr/bin/env bash
# 核 deploy/ota 脚本：打包结构、闸门、payload SHA-256、变体/版本拒装、内包前缀、切槽与回滚。
# 夹具用中性产品名 app；不碰 systemd（OTA_SKIP_SYSTEMCTL=1）。
# 用法: test_ota_scripts.sh
set -euo pipefail

FW_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OTA_DIR="$FW_ROOT/deploy/ota"
work="$(mktemp -d "${TMPDIR:-/tmp}/wdf-ota-scripts.XXXXXX")"
cleanup()
{
    rm -rf "$work"
}
trap cleanup EXIT

env_file="$work/ota.env"
share="$work/share/ota"
stage="$work/stage"
allow="$work/allow"
installed="$work/installed.txt"
slot_root="$work/slots"
out="$work/out"
extract="$work/extract"

fail()
{
    echo "test_ota_scripts: $*" >&2
    exit 1
}

spec()
{
    printf 'WDFSPEC\t%s\t%s\t%s\tpassed\n' "$1" "$2" "$3" >&2
}

write_installed()
{
    printf 'product_version=%s\nvariant=%s\n' "$1" "$2" > "$installed"
}

make_payload()
{
    local dir="$1"
    local tarball="$2"
    mkdir -p "$dir/usr/share/app" "$dir/etc/app" "$dir/usr/bin"
    echo "new-conf" > "$dir/etc/app/app.env"
    echo "new-bin" > "$dir/usr/bin/app"
    cat > "$dir/usr/share/app/RELEASE.txt" <<'EOF'
product_version=v1.2.3
product_hash=abc1234
variant=alpha
EOF
    tar -C "$dir" -czf "$tarball" .
}

prepare()
{
    cat > "$env_file" <<'EOF'
# 测试夹具：中性产品
OTA_PRODUCT=app
OTA_RELEASE_REL=usr/share/app/RELEASE.txt
OTA_STAGING=/nonexistent/app/ota
OTA_SLOT_ROOT=/nonexistent/app/slots
OTA_WORK_DIR=/nonexistent/app
OTA_BUNDLE_NAME=update.tar.gz
OTA_INSTALL_UNIT=app-ota.service
OTA_MAIN_UNIT=app.service
OTA_COMPANION_UNITS=app-helper.service
OTA_REQUIRED_FILE=usr/bin/app
OTA_PAYLOAD_PREFIXES=usr lib etc/systemd etc/app
OTA_PAYLOAD_EXCLUDE=etc/app
OTA_HOST_INSTALL=host_install.sh
EOF
    mkdir -p "$share" "$stage" "$out" "$extract"
    cp "$OTA_DIR/apply.sh" "$OTA_DIR/ota_lib.sh" "$share/"
    cp "$env_file" "$share/ota.env"

    make_payload "$work/payload" "$out/app-v1.2.3-alpha.tar.gz"
    "$OTA_DIR/pack_ota.sh" --env "$env_file" --out "$out" >/dev/null
    tar -C "$extract" -xzf "$out/upgrade-v1.2.3-alpha.tar.gz"
}

setup_slots()
{
    rm -rf "$slot_root"
    mkdir -p "$slot_root/a/usr/bin" "$slot_root/a/usr/share/app" "$slot_root/a/etc/app"
    echo "old-bin" > "$slot_root/a/usr/bin/app"
    echo "old-conf" > "$slot_root/a/etc/app/app.env"
    cp "$installed" "$slot_root/a/usr/share/app/RELEASE.txt"
    ln -sfn a "$slot_root/current"
}

current_slot()
{
    basename "$(readlink "$slot_root/current")"
}

run_env()
{
    OTA_STAGING="$stage" OTA_ALLOW="$allow" OTA_INSTALLED_RELEASE="$installed" \
        OTA_SLOT_ROOT="$slot_root" OTA_WORK_DIR="$work" OTA_SKIP_DISK=1 "$@"
}

run_upgrade()
{
    run_env env OTA_DRY_RUN=1 bash "$extract/upgrade/upgrade.sh"
}

run_apply_dry()
{
    run_env env OTA_DRY_RUN=1 bash "$share/apply.sh"
}

run_apply_slots()
{
    run_env env OTA_SKIP_SYSTEMCTL=1 OTA_START_FAIL="${1:-0}" bash "$share/apply.sh"
}

expect_fail()
{
    local what="$1"
    shift
    if "$@" 2>/dev/null; then
        fail "$what 应失败"
    fi
}

case_package_layout()
{
    local f
    for f in upgrade.sh ota_lib.sh ota.env RELEASE.txt payload.tar.gz; do
        [[ -e "$extract/upgrade/$f" ]] || fail "包内缺少 upgrade/$f"
    done
    [[ -f "$out/upgrade.tar.gz" ]] || fail "缺少 upgrade.tar.gz"
    [[ "$(ota_field version)" == "v1.2.3" ]] || fail "version 应为 v1.2.3"
    [[ "$(ota_field variant)" == "alpha" ]] || fail "variant 应为 alpha"
    [[ "$(awk '{ print $1 }' "$out/upgrade-v1.2.3-alpha.tar.gz.sha256")" == \
        "$(sha256sum "$out/upgrade-v1.2.3-alpha.tar.gz" | awk '{ print $1 }')" ]] || fail "整包 sidecar 不对"
    spec "" "打包含脚本、部署参数、RELEASE 与 payload，并写整包 sidecar" case_package_layout
}

ota_field()
{
    awk -F= -v k="$1" '$1 == k { print $2; exit }' "$extract/upgrade/RELEASE.txt"
}

case_refusals()
{
    rm -f "$allow"
    write_installed v1.0.0 alpha
    expect_fail "无闸门 upgrade.sh" run_upgrade
    setup_slots
    expect_fail "无闸门 apply.sh" run_apply_slots
    [[ "$(current_slot)" == "a" ]] || fail "无闸门不应切槽"

    echo 1 > "$allow"
    write_installed v1.0.0 beta
    expect_fail "变体不符 upgrade.sh" run_upgrade
    [[ ! -f "$stage/payload.tar.gz" ]] || fail "变体不符不应暂存"

    write_installed v1.2.3 alpha
    expect_fail "同版本 upgrade.sh" run_upgrade
    write_installed v2.0.0 alpha
    expect_fail "降级 upgrade.sh" run_upgrade

    write_installed v1.0.0 alpha
    run_upgrade >/dev/null
    [[ -f "$stage/payload.tar.gz" ]] || fail "dry-run 未暂存 payload"
    write_installed v1.0.0 beta
    rm -f "$stage/last_error"
    expect_fail "变体不符 apply.sh" run_apply_dry
    [[ -s "$stage/last_error" ]] || fail "拒装应写 last_error"

    write_installed v1.2.3 alpha
    setup_slots
    expect_fail "同版本 apply.sh" run_apply_slots
    [[ "$(current_slot)" == "a" ]] || fail "同版本拒装不应切槽"

    write_installed v1.0.0 alpha
    run_upgrade >/dev/null
    echo junk >> "$stage/payload.tar.gz"
    expect_fail "payload 被改 apply.sh" run_apply_dry
    cp "$extract/upgrade/payload.tar.gz" "$extract/upgrade/payload.ok"
    echo junk >> "$extract/upgrade/payload.tar.gz"
    expect_fail "payload 被改 upgrade.sh" run_upgrade
    mv -f "$extract/upgrade/payload.ok" "$extract/upgrade/payload.tar.gz"
    spec UPG-09 "无闸门、变体不符、同版本或降级、payload 被改均拒装不切槽" case_refusals
}

case_prefixes()
{
    local evil="$work/evil"
    mkdir -p "$evil/etc"
    make_payload "$evil" "$work/evil.tar.gz"
    echo "root:x:0:0" > "$evil/etc/passwd"
    tar -C "$evil" -czf "$work/evil.tar.gz" .
    rm -rf "$work/evil-out" "$work/evil-extract"
    mkdir -p "$work/evil-out" "$work/evil-extract"
    "$OTA_DIR/pack_ota.sh" --env "$env_file" --payload "$work/evil.tar.gz" --out "$work/evil-out" >/dev/null
    tar -C "$work/evil-extract" -xzf "$work/evil-out/upgrade-v1.2.3-alpha.tar.gz"
    echo 1 > "$allow"
    write_installed v1.0.0 alpha
    run_env env OTA_DRY_RUN=1 bash "$work/evil-extract/upgrade/upgrade.sh" >/dev/null
    expect_fail "内包含 etc/passwd apply.sh" run_apply_dry

    run_upgrade >/dev/null
    setup_slots
    run_apply_slots >/dev/null
    [[ ! -e "$slot_root/b/etc/app/app.env" ]] || fail "排除前缀不应解进新槽"
    [[ "$(cat "$slot_root/a/etc/app/app.env")" == "old-conf" ]] || fail "不应改旧槽"
    spec UPG-10 "内包成员不在允许前缀拒装；排除前缀不解进新槽" case_prefixes
}

case_slots()
{
    echo 1 > "$allow"
    write_installed v1.0.0 alpha
    run_upgrade >/dev/null
    setup_slots
    run_apply_slots >/dev/null
    [[ "$(current_slot)" == "b" ]] || fail "应切到 b"
    [[ "$(cat "$slot_root/b/usr/bin/app")" == "new-bin" ]] || fail "新二进制应在 b"
    [[ "$(cat "$slot_root/a/usr/bin/app")" == "old-bin" ]] || fail "不应改旧槽"

    run_upgrade >/dev/null
    setup_slots
    rm -f "$stage/last_error"
    expect_fail "启动失败 apply.sh" run_apply_slots 1
    [[ "$(current_slot)" == "a" ]] || fail "启动失败应拨回 a"
    [[ -s "$stage/last_error" ]] || fail "启动失败应写 last_error"

    run_upgrade >/dev/null
    setup_slots
    rm -rf "$slot_root/current"
    mkdir -p "$slot_root/current"
    expect_fail "current 非链接 apply.sh" run_apply_slots
    [[ ! -L "$slot_root/current" ]] || fail "current 为目录时不应改成链接"
    spec UPG-11 "先解非活动槽再切 current，启动失败拨回，current 非链接拒装" case_slots
}

prepare
case_package_layout
case_refusals
case_prefixes
case_slots
echo "test_ota_scripts: ok"
