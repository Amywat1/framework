#!/usr/bin/env bash
# 使用项目根目录 .clang-format 批量格式化 C/H 源码
# 用法:
#   ./scripts/format.sh           # 格式化全部
#   ./scripts/format.sh --check   # 仅检查，不修改文件

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "错误: 未找到 clang-format，请先安装：" >&2
    echo "  Ubuntu/Debian: sudo apt install clang-format" >&2
    echo "  macOS:         brew install clang-format" >&2
    exit 1
fi

CHECK=0
if [[ "${1:-}" == "--check" ]]; then
    CHECK=1
fi

# 排除 build* 而非仅 build/：check_all.sh 默认构建到 build-check/，若只排除
# build/，门禁就会扫描自己的构建产物（CMake 生成的 CompilerIdC.c、sw_version.h
# 等），导致默认配置下格式检查永远失败。
mapfile -t FILES < <(
    find . \
        -type f \( -name '*.c' -o -name '*.h' \) \
        ! -path './build*' \
        ! -path './third_party/*' \
        ! -path './.cache/*' \
        ! -path './.git/*' \
        2>/dev/null | sort
)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "未找到需要格式化的 .c/.h 文件"
    exit 0
fi

echo "clang-format: $(clang-format --version | head -1)"
echo "配置文件:   $ROOT/.clang-format"
echo "文件数量:   ${#FILES[@]}"
echo

if [[ $CHECK -eq 1 ]]; then
    FAIL=0
    for f in "${FILES[@]}"; do
        if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
            echo "格式不符: $f"
            FAIL=1
        fi
    done
    if [[ $FAIL -eq 0 ]]; then
        echo "全部文件格式正确"
        exit 0
    fi
    echo "存在未格式化的文件，请运行: ./scripts/format.sh" >&2
    exit 1
fi

for f in "${FILES[@]}"; do
    echo "format: $f"
    clang-format -i "$f"
done

echo
echo "完成，共格式化 ${#FILES[@]} 个文件"
