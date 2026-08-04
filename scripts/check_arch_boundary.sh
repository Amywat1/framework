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
#   R8  全框架          不使用 "../" 相对 include（兜底，防止绕过 R1~R6）
#   R9  domain/         不使用文件 IO / JSON / 线程注册 / 动态内存
#   R10 adapters/       不承载跨领域编排（bridge / projection / coordinator）
#   R11 application/    不依赖 adapters/ 或 services/
#   R12 cloud/          不依赖 adapters/、application/ 或 services/
#   R13 services/       不依赖 adapters/、application/ 或 cloud/
#   R14 observability/  只依赖 common/

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

# R8: 禁止相对路径向上跳的 include
#
# R1~R6 依赖 "#include \"<层>/" 的前缀形式匹配，若改用 "../" 形式引用上层，
# 依赖违规会绕过全部前缀规则而不被发现。此规则作为兜底：
# 框架内一律使用以仓库根为基准的层级路径 include，不得出现 "../"。
# （同目录内的 "#include \"xxx.h\"" 不受影响，其目标必在同层。）
TOTAL_RULES=$((TOTAL_RULES + 1))
relative_includes=$(grep -rn --include='*.c' --include='*.h' \
    '#include[[:space:]]*"\.\.' \
    "${FW_ROOT}" 2>/dev/null \
    | grep -v "^${FW_ROOT}/third_party/" \
    | grep -v "^${FW_ROOT}/build" \
    || true)

if [ -z "$relative_includes" ]; then
    echo "[PASS] R8: 无相对路径向上跳的 include（\"../\" 形式）"
else
    echo ""
    echo "[FAIL] R8: 存在相对路径向上跳的 include，会绕过 R1~R6 前缀检查"
    printf '%s\n' "$relative_includes" | sed "s|^${FW_ROOT}/|  |"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# R9: domain/ 不使用违背领域层定位的设施
#
# R1~R6 只看 include 路径，管不住"用标准库直接做本不属于领域层的事"。
# 以下四类是领域层最容易悄然引入的越界，一旦出现，领域规则就不再可纯逻辑测试：
#
#   文件 IO      文件格式与路径属于存储适配职责，应经 storage 端口
#   JSON 解析    序列化格式属于适配层，领域只接收已解析的模型
#   线程注册     领域不自建线程，时序推进由调用方登记周期任务
#   动态内存     嵌入式领域层优先静态分配，避免运行期分配失败与碎片
#
# 白名单：方案引擎的模型/表达式/运行时按可变规模方案树分配，改为静态池需要
# 预设方案规模上限，当前按加载期一次性分配 + 显式 free 管理（engine_destroy /
# engine_program_free），故豁免动态内存一项。豁免仅限这三个文件，新增文件
# 若同样需要动态内存，须在此显式登记并说明理由。
# -----------------------------------------------------------------------------
check_domain_antipattern() {
    local rule="$1"
    local pattern="$2"
    shift 2
    local exempt_args=()
    local f

    for f in "$@"; do
        exempt_args+=(-e "^${FW_ROOT}/domain/${f}:")
    done

    TOTAL_RULES=$((TOTAL_RULES + 1))

    # 过滤纯注释行：行首（忽略缩进）为 //、/* 或续行 * 的一律跳过。
    # 文档里提到某个设施的名字不构成依赖，只有代码引用才算违规。
    local hits
    hits=$(grep -rnE --include='*.c' --include='*.h' "${pattern}" "${FW_ROOT}/domain" 2>/dev/null \
        | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|/\*|\*)' \
        || true)

    if [ ${#exempt_args[@]} -gt 0 ] && [ -n "$hits" ]; then
        hits=$(printf '%s\n' "$hits" | grep -v "${exempt_args[@]}" || true)
    fi

    if [ -z "$hits" ]; then
        echo "[PASS] ${rule}"
    else
        echo ""
        echo "[FAIL] ${rule}"
        printf '%s\n' "$hits" | sed "s|^${FW_ROOT}/|  |"
        TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
    fi
}

check_domain_antipattern \
    "R9a: domain/ 不做文件 IO（应经 storage 端口）" \
    '\b(fopen|freopen|fread|fwrite|fclose|remove|rename)[[:space:]]*\('

check_domain_antipattern \
    "R9b: domain/ 不解析序列化格式（应由适配层传入已解析模型）" \
    '\bcJSON'

check_domain_antipattern \
    "R9c: domain/ 不自建线程或周期任务（由调用方驱动 tick）" \
    '\b(periodic_task_register|pthread_create|thread_register(_arg)?)[[:space:]]*\('

check_domain_antipattern \
    "R9d: domain/ 不使用动态内存（方案引擎三文件已登记豁免）" \
    '\b(malloc|calloc|realloc|strdup)[[:space:]]*\(' \
    'program_engine/engine/engine.c' \
    'program_engine/engine/engine_expr.c' \
    'program_engine/model/engine_model.c'

# -----------------------------------------------------------------------------
# R10: adapters/ 不承载跨领域编排
#
# adapters 的职责是适配外部系统（SDK、协议、硬件、仿真后端）。事件桥接、投影、
# 协调器这类"只依赖 domain + runtime、不碰任何外部系统"的代码属于 application：
# 混在 adapters 里会让"adapters 是外部适配"这条边界失去可判定性，后续 review
# 无法据此判断新代码该放哪。
#
# 判据：adapters 下的文件名不得出现 application 层的编排后缀。
# -----------------------------------------------------------------------------
TOTAL_RULES=$((TOTAL_RULES + 1))
orchestration_in_adapters=$(find "${FW_ROOT}/adapters" \
    \( -name '*_bridge.c' -o -name '*_bridge.h' \
       -o -name '*_projection.c' -o -name '*_projection.h' \
       -o -name '*_coordinator.c' -o -name '*_coordinator.h' \) \
    -print 2>/dev/null | sort || true)

if [ -z "$orchestration_in_adapters" ]; then
    echo "[PASS] R10: adapters/ 不含 bridge / projection / coordinator（应归 application）"
else
    echo ""
    echo "[FAIL] R10: adapters/ 混入了跨领域编排代码，应迁至 application/"
    printf '%s\n' "$orchestration_in_adapters" | sed "s|^${FW_ROOT}/|  |"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# R11~R14: 上层目录的出向依赖
#
# R1~R10 只约束了 common/domain/ports/runtime 的出向依赖，application、cloud、
# services、observability 的出向依赖此前无任何规则——它们只作为"禁止被引用的
# 上层"出现在别人的规则里。这个缺口让目录级双向依赖可以长期存在而不被发现：
# cloud_model.h 曾 include application/orchestrators/report_scheduler.h，同时
# report_scheduler.c include cloud/cloud_point_watcher.h，两个目录互相依赖、
# 谁都无法单独提取，而全部 10 条规则都通过。
#
# 各层允许的出向依赖按实际需要确定，不做超出现状的收紧：
#   application  common / domain / ports / runtime / cloud
#   cloud        common / domain / ports / runtime
#   services     common / ports / domain
#   observability common（旁路设施，不参与主链路，故约束最严）
# -----------------------------------------------------------------------------
check_includes \
    "R11: application/ 不依赖 adapters/ 或 services/" \
    "application" \
    "adapters/" "services/"

check_includes \
    "R12: cloud/ 不依赖 adapters/、application/ 或 services/" \
    "cloud" \
    "adapters/" "application/" "services/"

check_includes \
    "R13: services/ 不依赖 adapters/、application/ 或 cloud/" \
    "services" \
    "adapters/" "application/" "cloud/"

# observability 是旁路设施：任何层都可向它发布记录，它不回调任何层，因此不构成
# 环。这条性质只有在它除 common 之外什么都不依赖时才成立，故这里逐一排除其余层。
check_includes \
    "R14: observability/ 只依赖 common/" \
    "observability" \
    "domain/" "ports/" "application/" "adapters/" "runtime/" "services/" "cloud/"

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
