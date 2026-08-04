#!/usr/bin/env bash
# check_all.sh — 框架提交前统一门禁
#
# 串联三项现成检查，任一失败即整体失败：
#   1. 架构依赖边界检查   scripts/check_arch_boundary.sh
#   2. 行为契约结构检查   scripts/check_behaviour_contract.sh
#   3. 代码格式检查       scripts/format.sh --check
#   4. 单元测试           cmake 配置 + 构建 + ctest
#
# 用法:
#   ./scripts/check_all.sh                # 全部检查
#   ./scripts/check_all.sh --skip-tests   # 只做静态检查（无 cmake 环境时）
#   BUILD_DIR=/tmp/wdf ./scripts/check_all.sh
#
# 退出码:
#   0  全部通过
#   1  存在失败项

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-${ROOT}/build-check}"
SKIP_TESTS=0
if [[ "${1:-}" == "--skip-tests" ]]; then
    SKIP_TESTS=1
fi

FAILED=()

run_step() {
    local name="$1"
    shift

    echo ""
    echo "======================================================="
    echo "[$name]"
    echo "======================================================="
    if "$@"; then
        echo "--> $name 通过"
    else
        echo "--> $name 失败" >&2
        FAILED+=("$name")
    fi
}

run_tests() {
    if ! command -v cmake >/dev/null 2>&1; then
        echo "错误: 未找到 cmake" >&2
        return 1
    fi

    cmake -S "$ROOT" -B "$BUILD_DIR" -DWDF_BUILD_TESTS=ON -DWDF_BUILD_DEMO=ON >/dev/null || return 1
    cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)" || return 1
    ctest --test-dir "$BUILD_DIR" --output-on-failure -j"$(nproc 2>/dev/null || echo 4)" || return 1
}

run_step "架构依赖边界" "${ROOT}/scripts/check_arch_boundary.sh"
run_step "行为契约结构" "${ROOT}/scripts/check_behaviour_contract.sh"
run_step "代码格式" "${ROOT}/scripts/format.sh" --check

if [[ $SKIP_TESTS -eq 0 ]]; then
    run_step "单元测试" run_tests
else
    echo ""
    echo "已按 --skip-tests 跳过单元测试"
fi

echo ""
echo "======================================================="
if [[ ${#FAILED[@]} -eq 0 ]]; then
    echo "门禁结果: 全部通过"
    exit 0
fi
echo "门禁结果: 以下检查失败" >&2
for item in "${FAILED[@]}"; do
    echo "  - $item" >&2
done
exit 1
