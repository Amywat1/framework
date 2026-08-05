#!/usr/bin/env bash
# check_all.sh — 框架提交前统一门禁
#
# 串联五项检查，任一失败即整体失败：
#   1. 架构依赖边界检查   scripts/check_arch_boundary.sh
#   2. 行为契约结构检查   scripts/check_behaviour_contract.sh
#   3. 文档引用一致性     scripts/check_doc_refs.sh
#   4. 代码格式检查       scripts/format.sh --check
#   5. 单元测试           cmake 配置 + 构建 + ctest
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
RESULT_DIR="${BUILD_DIR}/test-results"
GATE_LOG_DIR="${RESULT_DIR}/gates"
GATES_JSON="${RESULT_DIR}/gates.json"
JUNIT_FILE="${RESULT_DIR}/junit.xml"
REPORT_FILE="${RESULT_DIR}/report.html"
RAW_DIR="${RESULT_DIR}/raw"
SKIP_TESTS=0
if [[ "${1:-}" == "--skip-tests" ]]; then
    SKIP_TESTS=1
fi

FAILED=()
GATE_NAMES=()
GATE_STATUSES=()
GATE_DURATIONS=()
GATE_LOGS=()
STARTED_AT="$(date --iso-8601=seconds)"
START_MS="$(date +%s%3N)"

mkdir -p "$GATE_LOG_DIR"
rm -f "$GATES_JSON" "$JUNIT_FILE" "$REPORT_FILE"

duration_seconds() {
    local elapsed_ms="$1"
    printf '%d.%03d' "$((elapsed_ms / 1000))" "$((elapsed_ms % 1000))"
}

record_gate() {
    GATE_NAMES+=("$1")
    GATE_STATUSES+=("$2")
    GATE_DURATIONS+=("$3")
    GATE_LOGS+=("$4")
}

run_step() {
    local name="$1"
    local log_name="$2"
    local log_path="${GATE_LOG_DIR}/${log_name}.log"
    local begin_ms
    local end_ms
    local elapsed
    local status
    shift 2

    echo ""
    echo "======================================================="
    echo "[$name]"
    echo "======================================================="
    begin_ms="$(date +%s%3N)"
    if "$@" 2>&1 | tee "$log_path"; then
        echo "--> $name 通过"
        status="passed"
    else
        echo "--> $name 失败" >&2
        FAILED+=("$name")
        status="failed"
    fi
    end_ms="$(date +%s%3N)"
    elapsed="$(duration_seconds "$((end_ms - begin_ms))")"
    record_gate "$name" "$status" "$elapsed" "gates/${log_name}.log"
}

run_tests() {
    if ! command -v cmake >/dev/null 2>&1; then
        echo "错误: 未找到 cmake" >&2
        return 1
    fi

    cmake -S "$ROOT" -B "$BUILD_DIR" -DWDF_BUILD_TESTS=ON -DWDF_BUILD_DEMO=ON >/dev/null || return 1
    cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)" || return 1
    ctest --test-dir "$BUILD_DIR" \
        --output-on-failure \
        --output-junit "$JUNIT_FILE" \
        --test-output-size-passed 10485760 \
        --test-output-size-failed 10485760 \
        -j"$(nproc 2>/dev/null || echo 4)"
}

write_gate_results() {
    local end_ms
    local total
    local index
    local comma=""
    end_ms="$(date +%s%3N)"
    total="$(duration_seconds "$((end_ms - START_MS))")"
    {
        printf '{\n  "started_at": "%s",\n  "duration_seconds": %s,\n  "gates": [\n' \
            "$STARTED_AT" "$total"
        for ((index = 0; index < ${#GATE_NAMES[@]}; index++)); do
            printf '%s    {"name": "%s", "status": "%s", "duration_seconds": %s, "log": "%s"}' \
                "$comma" "${GATE_NAMES[$index]}" "${GATE_STATUSES[$index]}" \
                "${GATE_DURATIONS[$index]}" "${GATE_LOGS[$index]}"
            comma=$',\n'
        done
        printf '\n  ]\n}\n'
    } >"$GATES_JSON"
}

generate_report() {
    if ! command -v python3 >/dev/null 2>&1; then
        echo "错误: 未找到 python3，无法生成测试报告" >&2
        return 1
    fi
    python3 "${ROOT}/tests/report/generate_test_report.py" \
        --junit "$JUNIT_FILE" \
        --gates "$GATES_JSON" \
        --output "$REPORT_FILE" \
        --raw-dir "$RAW_DIR" \
        --root "$ROOT"
}

run_step "架构依赖边界" "architecture" "${ROOT}/scripts/check_arch_boundary.sh"
run_step "行为契约结构" "behaviour-contract" "${ROOT}/scripts/check_behaviour_contract.sh"
run_step "文档引用一致性" "doc-refs" "${ROOT}/scripts/check_doc_refs.sh"
run_step "代码格式" "format" "${ROOT}/scripts/format.sh" --check

if [[ $SKIP_TESTS -eq 0 ]]; then
    run_step "自动化测试" "automated-tests" run_tests
else
    echo ""
    echo "已按 --skip-tests 跳过单元测试"
    printf '按 --skip-tests 跳过自动化测试\n' >"${GATE_LOG_DIR}/automated-tests.log"
    record_gate "自动化测试" "skipped" "0.000" "gates/automated-tests.log"
fi

write_gate_results
if ! generate_report; then
    FAILED+=("测试报告生成")
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
