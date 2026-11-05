#!/bin/bash
# 压缩包 OTA 脚本公共函数。由 upgrade.sh / apply.sh / pack_ota.sh source，不单独执行。
#
# 部署参数来自项目 ota.env：逐行 KEY=VALUE，只认 OTA_ 开头的键，其余行（含 # 注释）忽略，
# 不经 shell 求值。环境里已有的同名变量优先，供测试覆盖。键：
#   OTA_PRODUCT           发布包文件名前缀（<前缀>-*.tar.gz），仅打包用
#   OTA_RELEASE_REL       发布树内 RELEASE 相对路径（含 variant、product_version、product_hash）
#   OTA_STAGING           闸门 allow、安装锁 busy、last_error 与暂存 payload 所在目录
#   OTA_SLOT_ROOT         槽根目录。槽名固定为 a、b，current 为指向活动槽的相对链接；
#                         这是模块契约，不是可配项
#   OTA_WORK_DIR          云通道落盘与外层包解包目录
#   OTA_BUNDLE_NAME       云通道落盘文件名
#   OTA_INSTALL_UNIT      执行 apply.sh 的安装单元
#   OTA_MAIN_UNIT         主服务；READY 才算装好
#   OTA_COMPANION_UNITS   伴随服务（空格分隔，可空）；主服务停后停、READY 后重拉
#   OTA_REQUIRED_FILE     新槽内必须存在的文件（相对槽根）
#   OTA_PAYLOAD_PREFIXES  内包允许的路径前缀（空格分隔）
#   OTA_PAYLOAD_EXCLUDE   内包里允许存在但不解进槽的前缀（空格分隔，可空）
#   OTA_HOST_INSTALL      与 apply.sh 同目录的宿主文件钩子（可空）；参数为槽目录，
#                         环境变量 OTA_SLOT_ROOT 供钩子拼 current 路径
#
# 板上脚本（upgrade.sh / apply.sh）失败走 ota_die：写 <OTA_STAGING>/last_error、删安装锁，
# 再调用脚本自己定义的 ota_on_die（可选）。

OTA_LIST_TIMEOUT_SEC=15
OTA_DISK_FACTOR=3

ota_load_env()
{
    local file="$1"
    local key value

    if [[ ! -f "$file" ]]; then
        echo "OTA: 缺少部署参数 $file" >&2
        exit 1
    fi
    while IFS='=' read -r key value || [[ -n "$key" ]]; do
        [[ "$key" =~ ^OTA_[A-Z0-9_]+$ ]] || continue
        if [[ -n "${!key+x}" ]]; then
            continue
        fi
        printf -v "$key" '%s' "$value"
    done < "$file"
}

ota_die()
{
    echo "OTA: $*" >&2
    if [[ -n "${OTA_STAGING:-}" ]]; then
        printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" > "${OTA_STAGING}/last_error" 2>/dev/null || true
        rm -f "${OTA_STAGING}/busy"
    fi
    if declare -F ota_on_die >/dev/null; then
        ota_on_die
    fi
    exit 1
}

ota_require_env()
{
    local k
    for k in "$@"; do
        if [[ -z "${!k:-}" ]]; then
            ota_die "部署参数缺少 $k"
        fi
    done
}

# 板上脚本入口：加载 ota.env，检查公共键与脚本额外要求的键，补闸门与活动槽 RELEASE 的默认路径
ota_board_env()
{
    local file="$1"
    shift
    ota_load_env "$file"
    ota_require_env OTA_STAGING OTA_SLOT_ROOT OTA_RELEASE_REL OTA_WORK_DIR "$@"
    OTA_ALLOW="${OTA_ALLOW:-$OTA_STAGING/allow}"
    OTA_INSTALLED_RELEASE="${OTA_INSTALLED_RELEASE:-$OTA_SLOT_ROOT/current/$OTA_RELEASE_REL}"
}

ota_require_not_symlink()
{
    if [[ -L "$1" ]]; then
        ota_die "$1 是符号链接，拒绝安装"
    fi
}

# 闸门不在则拒装；带 drop_staged 时顺带删掉已暂存的 payload
ota_require_allow()
{
    if [[ ! -f "$OTA_ALLOW" ]]; then
        if [[ "${1:-}" == "drop_staged" ]]; then
            rm -f "$OTA_STAGING/payload.tar.gz" "$OTA_STAGING/RELEASE.txt"
        fi
        ota_die "无允许闸门，拒绝安装"
    fi
}

ota_read_field()
{
    awk -F= -v k="$2" '$1 == k { print $2; exit }' "$1"
}

# 包内 RELEASE 成员的原样路径（可能带 ./ 前缀）；没有则输出空。
# awk 读完全部输入，避免提前退出让 tar 收 SIGPIPE。
ota_release_member()
{
    tar -tzf "$1" | awk -v r="$OTA_RELEASE_REL" \
        '{ p = $0; sub(/^\.\//, "", p); if (!found && p == r) { print $0; found = 1 } }'
}

ota_base_version()
{
    if [[ "$1" =~ ^v?([0-9]+)\.([0-9]+)\.([0-9]+) ]]; then
        printf '%s.%s.%s\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}"
        return 0
    fi
    return 1
}

ota_version_int()
{
    local a b c
    IFS=. read -r a b c <<EOF
$1
EOF
    printf '%d\n' $((a * 1000000 + b * 1000 + c))
}

# 包内版本须严格高于活动槽版本
ota_require_newer_version()
{
    local pkg_release="$1"
    local installed="$2"
    local pkg have

    pkg="$(ota_base_version "$(ota_read_field "$pkg_release" version)")" \
        || pkg="$(ota_base_version "$(ota_read_field "$pkg_release" product_version)")" || true
    have="$(ota_base_version "$(ota_read_field "$installed" version)")" \
        || have="$(ota_base_version "$(ota_read_field "$installed" product_version)")" || true
    if [[ -z "$pkg" ]] || [[ -z "$have" ]]; then
        ota_die "无法比较版本 package=${pkg:-?} installed=${have:-?}"
    fi
    if [[ "$(ota_version_int "$pkg")" -le "$(ota_version_int "$have")" ]]; then
        ota_die "禁止降级或同版本 package=$pkg installed=$have"
    fi
}

# 包内变体须与活动槽一致
ota_check_variant()
{
    local pkg have

    pkg="$(ota_read_field "$1" variant)"
    have="$(ota_read_field "$2" variant)"
    if [[ -z "$pkg" ]] || [[ "$pkg" != "$have" ]]; then
        ota_die "变体不符 package=${pkg:-?} installed=${have:-?}"
    fi
}

# payload 字节须与 RELEASE 里的 payload_sha256 一致
ota_check_payload_sha()
{
    local payload="$1"
    local release="$2"
    local expect have

    if ! command -v sha256sum >/dev/null 2>&1; then
        ota_die "没有 sha256sum，拒绝安装"
    fi
    expect="$(ota_read_field "$release" payload_sha256)"
    if [[ ! "$expect" =~ ^[0-9a-f]{64}$ ]]; then
        ota_die "RELEASE 缺少有效 payload_sha256"
    fi
    have="$(sha256sum "$payload" | awk '{ print $1 }')"
    if [[ "$have" != "$expect" ]]; then
        ota_die "payload SHA-256 不符 expect=$expect have=$have"
    fi
}

ota_disk_avail_bytes()
{
    local k
    k="$(df -Pk "$1" 2>/dev/null | awk 'NR==2 { print $4; exit }')"
    if [[ -z "$k" ]]; then
        return 1
    fi
    printf '%s\n' $((k * 1024))
}

# 可用空间须不小于 payload 的 OTA_DISK_FACTOR 倍；依次尝试给出的目录
ota_check_disk()
{
    local payload="$1"
    local need avail="" dir
    shift
    if [[ "${OTA_SKIP_DISK:-}" == "1" ]]; then
        return
    fi
    need=$(( $(stat -c%s "$payload") * OTA_DISK_FACTOR ))
    for dir in "$@"; do
        avail="$(ota_disk_avail_bytes "$dir")" && break
        avail=""
    done
    if [[ -z "$avail" ]] || [[ "$avail" -lt "$need" ]]; then
        ota_die "磁盘不足 need=$need avail=${avail:-?}"
    fi
}
