#!/usr/bin/env bash
# 把项目发布包（FHS 根文件系统 tar.gz）打成云端压缩包 OTA 所需的 upgrade.tar.gz。
#
# 用法:
#   pack_ota.sh --env <项目 ota.env> --out DIR [--payload <发布包.tar.gz>]
#
# 省略 --payload 时，在 --out 里选唯一一份 <OTA_PRODUCT>-*.tar.gz（排除 .debug）。
# 包内 RELEASE 取自 payload 的 <OTA_RELEASE_REL>，须含 variant 与 product_version（vX.Y.Z 或 X.Y.Z）。
# 控制台固件文件必须叫 upgrade.tar.gz；版本号填 RELEASE 里的 vX.Y.Z（无 hash）。

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ENV_FILE=""
PAYLOAD=""
OUT_DIR=""

usage()
{
    cat <<'EOF'
用法: pack_ota.sh --env <ota.env> --out DIR [--payload <发布包.tar.gz>]

省略 --payload 时，在 --out 里自动选唯一的 <OTA_PRODUCT>-*.tar.gz（不含 .debug）。
EOF
}

die()
{
    echo "错误: $*" >&2
    exit 1
}

pick_payload_from_dir()
{
    local dir="$1"
    local f base
    local matches=()

    shopt -s nullglob
    for f in "$dir/$OTA_PRODUCT"-*.tar.gz; do
        base="$(basename "$f")"
        if [[ "$base" == *.debug.tar.gz ]]; then
            continue
        fi
        matches+=("$f")
    done
    shopt -u nullglob
    if [[ ${#matches[@]} -eq 1 ]]; then
        PAYLOAD="${matches[0]}"
        echo "==> 自动选用 payload $PAYLOAD" >&2
        return
    fi
    if [[ ${#matches[@]} -eq 0 ]]; then
        die "$dir 里没有 $OTA_PRODUCT-*.tar.gz，请先打发布包或指定 --payload"
    fi
    echo "错误: $dir 里有多份发布包，请指定 --payload：" >&2
    printf '  %s\n' "${matches[@]}" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --env) ENV_FILE="$2"; shift 2 ;;
        --payload) PAYLOAD="$2"; shift 2 ;;
        --out) OUT_DIR="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "未知参数: $1" >&2; usage >&2; exit 1 ;;
    esac
done

if [[ -z "$ENV_FILE" ]] || [[ -z "$OUT_DIR" ]]; then
    usage >&2
    exit 1
fi
# shellcheck source=ota_lib.sh
source "$HERE/ota_lib.sh"
ota_load_env "$ENV_FILE"
if [[ -z "${OTA_PRODUCT:-}" ]] || [[ -z "${OTA_RELEASE_REL:-}" ]]; then
    die "$ENV_FILE 缺少 OTA_PRODUCT 或 OTA_RELEASE_REL"
fi

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
if [[ -z "$PAYLOAD" ]]; then
    pick_payload_from_dir "$OUT_DIR"
elif [[ ! -f "$PAYLOAD" ]]; then
    die "--payload 不是文件: $PAYLOAD"
fi
PAYLOAD="$(cd "$(dirname "$PAYLOAD")" && pwd)/$(basename "$PAYLOAD")"

if ! command -v sha256sum >/dev/null 2>&1 || ! command -v md5sum >/dev/null 2>&1; then
    die "需要 sha256sum 和 md5sum"
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/ota-pack.XXXXXX")"
cleanup()
{
    rm -rf "$work"
}
trap cleanup EXIT

inner="$work/upgrade"
mkdir -p "$inner"
cp "$PAYLOAD" "$inner/payload.tar.gz"
payload_sha256="$(sha256sum "$inner/payload.tar.gz" | awk '{ print $1 }')"
if [[ ${#payload_sha256} -ne 64 ]]; then
    die "无法计算 payload SHA-256"
fi
release_member="$(ota_release_member "$PAYLOAD")"
if [[ -z "$release_member" ]]; then
    die "payload 里没有 $OTA_RELEASE_REL"
fi
tar -xOf "$PAYLOAD" "$release_member" > "$inner/RELEASE.txt"

variant="$(ota_read_field "$inner/RELEASE.txt" variant)"
raw_ver="$(ota_read_field "$inner/RELEASE.txt" product_version)"
hash="$(ota_read_field "$inner/RELEASE.txt" product_hash)"
if [[ -z "$variant" ]]; then
    die "payload 的 RELEASE 没有 variant"
fi

ota_ver=""
if [[ "$raw_ver" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
    ota_ver="$raw_ver"
elif [[ "$raw_ver" =~ ^([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
    ota_ver="v${BASH_REMATCH[1]}"
fi
if [[ -z "$ota_ver" ]]; then
    echo "错误: product_version 须为 vX.Y.Z 或 X.Y.Z，实际=${raw_ver:-?}" >&2
    echo "这是 Git 短哈希时，说明仓库还没有匹配 v[0-9]* 的 tag：先打 tag 再重新构建发布包。" >&2
    exit 1
fi

{
    echo "version=$ota_ver"
    echo "variant=$variant"
    echo "product_hash=${hash:-unknown}"
    echo "product_version=$raw_ver"
    echo "payload_sha256=$payload_sha256"
} > "$inner/RELEASE.txt"

cp "$HERE/upgrade.sh" "$inner/upgrade.sh"
chmod +x "$inner/upgrade.sh"
cp "$HERE/ota_lib.sh" "$inner/ota_lib.sh"
cp "$ENV_FILE" "$inner/ota.env"

# 控制台上传文件名必须是 upgrade.tar.gz；云通道落盘名由项目 OTA_BUNDLE_NAME 决定，不会改名。
# 不要把整包摘要写进包内再校验整包：sidecar 放在包外。
ota_tar="$OUT_DIR/upgrade-${ota_ver}-${variant}.tar.gz"
tar -C "$work" --owner=0 --group=0 --numeric-owner -czf "$ota_tar" upgrade
cp -f "$ota_tar" "$OUT_DIR/upgrade.tar.gz"
package_sha256="$(sha256sum "$ota_tar" | awk '{ print $1 }')"
package_md5="$(md5sum "$ota_tar" | awk '{ print $1 }')"
printf '%s  %s\n' "$package_sha256" "$(basename "$ota_tar")" > "$ota_tar.sha256"

echo "==> $ota_tar"
echo "==> $OUT_DIR/upgrade.tar.gz （上传云端时用这个文件名）"
echo "    version=$ota_ver variant=$variant"
echo "    payload_sha256=$payload_sha256"
echo "    package_sha256=$package_sha256"
echo "    package_md5=$package_md5 （控制台选同一算法后核对，不要拿 MD5 对 SHA256）"
cat "$inner/RELEASE.txt"
