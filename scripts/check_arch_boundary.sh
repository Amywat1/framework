#!/usr/bin/env bash
# check_arch_boundary.sh — 框架架构依赖边界检查
#
# 用法:
#   ./scripts/check_arch_boundary.sh [framework_root]
#
# 退出码:
#   0  无违规
#   1  发现违规
#   2  参数错误
#
# 规则概览:
#   R1  common/        不依赖上层目录（domain/ports/application/adapters/runtime/services/cloud/）
#   R2  domain/        不依赖 adapters/ 或 application/
#   R3  ports/         不依赖 adapters/、application/ 或 runtime/
#   R4  runtime/event_bus/   不依赖业务层
#   R5  runtime/scheduler/   不依赖业务层
#   R6  各框架层        不引用项目专属头文件路径（m8/ 前缀）
#   R7  框架核心目录    不含项目机型前缀文件名（m8_*.c / m8_*.h）

set -euo pipefail

FW_ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"

if [ ! -d "${FW_ROOT}/domain" ] || [ ! -d "${FW_ROOT}/common" ]; then
    echo "错误: '${FW_ROOT}' 不是有效的 framework 根目录" >&2
    exit 2
fi

TOTAL_VIOLATIONS=0
TOTAL_RULES=0

# -----------------------------------------------------------------------------
# check_includes <规则描述> <相对目录> <禁止前缀1> [<禁止前缀2> ...]
#
# 在指定目录下检查 .c/.h 文件中是否出现禁止的 #include "prefix 形式。
# 若发现违规则输出文件:行号:匹配行，并将全局 TOTAL_VIOLATIONS 加 1。
# -----------------------------------------------------------------------------
check_includes() {
    local rule="$1"
    local dir="$2"
    shift 2
    local target="${FW_ROOT}/${dir}"

    TOTAL_RULES=$((TOTAL_RULES + 1))

    if [ ! -d "${target}" ]; then
        return
    fi

    local all_hits=""
    for prefix in "$@"; do
        local hits
        hits=$(grep -rn --include='*.c' --include='*.h' \
            "#include \"${prefix}" "${target}" 2>/dev/null || true)
        if [ -n "$hits" ]; then
            [ -n "$all_hits" ] && all_hits+=$'\n'
            all_hits+="${hits}"
        fi
    done

    if [ -z "$all_hits" ]; then
        echo "[PASS] ${rule}"
    else
        echo ""
        echo "[FAIL] ${rule}"
        printf '%s\n' "$all_hits" | sed "s|^${FW_ROOT}/|  |"
        TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
    fi
}

# -----------------------------------------------------------------------------
echo "架构依赖边界检查: ${FW_ROOT}"
echo "======================================================="

# R1: common/ 不依赖上层目录
# common/ 是最底层模块，只允许引用自身和 third_party/
check_includes \
    "R1: common/ 不依赖上层模块" \
    "common" \
    "domain/" "ports/" "application/" "adapters/" "runtime/" "services/" "cloud/"

# R2: domain/ 不依赖 adapters/ 或 application/
# domain 可引用 common/、ports/、runtime/，但不得依赖具体实现层或用例层
check_includes \
    "R2: domain/ 不依赖 adapters/ 或 application/" \
    "domain" \
    "adapters/" "application/"

# R3: ports/ 不依赖 adapters/、application/ 或 runtime/
# ports 是纯抽象契约，不依赖任何实现层或运行时基础设施
check_includes \
    "R3: ports/ 不依赖 adapters/、application/ 或 runtime/" \
    "ports" \
    "adapters/" "application/" "runtime/"

# R4: runtime/event_bus/ 不依赖业务层
# event_bus 是核心基础设施，必须对业务层保持无知
# （runtime/bootstrap/ 是装配器，不在此规则范围内）
check_includes \
    "R4: runtime/event_bus/ 不依赖业务层" \
    "runtime/event_bus" \
    "domain/" "ports/" "application/" "adapters/" "services/" "cloud/"

# R5: runtime/scheduler/ 不依赖业务层
check_includes \
    "R5: runtime/scheduler/ 不依赖业务层" \
    "runtime/scheduler" \
    "domain/" "ports/" "application/" "adapters/" "services/" "cloud/"

# R6: 框架各层不引用项目专属头文件路径（m8/ 前缀）
# 防止通用框架代码引用项目版本头、机型配置头等污染框架可复用性
for layer in domain common ports application runtime services cloud adapters; do
    check_includes \
        "R6-${layer}: ${layer}/ 不引用项目专属头（m8/ 前缀）" \
        "${layer}" \
        "m8/"
done

# R7: 框架核心目录中文件名不含项目机型前缀 m8_
# 防止项目专属文件混入通用框架目录（tests/ 和 demo/ 除外）
TOTAL_RULES=$((TOTAL_RULES + 1))
m8_files=$(find "${FW_ROOT}" \
    -not -path "${FW_ROOT}/tests/*" \
    -not -path "${FW_ROOT}/demo/*" \
    -not -path "${FW_ROOT}/scripts/*" \
    \( -name 'm8_*.c' -o -name 'm8_*.h' \) \
    -print 2>/dev/null | sort || true)

if [ -z "$m8_files" ]; then
    echo "[PASS] R7: 框架核心目录中无项目机型前缀文件（m8_*.c / m8_*.h）"
else
    echo ""
    echo "[FAIL] R7: 框架核心目录中存在项目机型前缀文件"
    printf '%s\n' "$m8_files" | sed "s|^${FW_ROOT}/|  |"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
echo ""
echo "======================================================="
if [ "${TOTAL_VIOLATIONS}" -eq 0 ]; then
    echo "结果: 通过 — 全部 ${TOTAL_RULES} 条规则无违规"
    exit 0
else
    echo "结果: 发现 ${TOTAL_VIOLATIONS} 条规则违规（共检查 ${TOTAL_RULES} 条）"
    exit 1
fi
