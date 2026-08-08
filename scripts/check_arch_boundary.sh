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
#   R1  common/        不依赖上层目录（domain/application/adapters/runtime/services/）
#   R2  domain/        不依赖 adapters/ 或 application/（含 application/ports）
#   R3  domain|application 下契约头  不依赖 adapters/
#   R4  runtime/event_bus/   不依赖业务层
#   R5  runtime/scheduler/   不依赖业务层
#   R6  各框架层        引号 include 均可在框架树内解析（vendor 头限 providers/）
#   R7  框架层 .c       均已在框架构建清单中登记
#   R8  全框架          不使用 "../" 相对 include（兜底，防止绕过 R1~R5）
#   R9  domain/         不使用文件 IO / JSON / 线程注册 / 动态内存
#   R10 adapters/       不承载跨领域编排（bridge / projection / coordinator）
#   R11 application/    不依赖 adapters/ 或 services/
#   R13 services/       不依赖 adapters/ 或 application/
#   R14 observability/  只依赖 common/
#   R15 全框架头文件    保护宏等于其路径的全大写下划线形式

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
    "domain/" "application/" "adapters/" "runtime/" "services/"

# R2: domain/ 不依赖 adapters/ 或 application/
# domain 可引用 common/、自身 domain/ports/outbound、runtime/event_bus
check_includes \
    "R2: domain/ 不依赖 adapters/ 或 application/" \
    "domain" \
    "adapters/" "application/"

# R3: 契约目录不依赖 adapters（runtime/ports 是装配器，允许依赖 domain/application）
check_includes \
    "R3a: domain/ports/ 不依赖 adapters/" \
    "domain/ports" \
    "adapters/"
check_includes \
    "R3b: application/ports/ 不依赖 adapters/" \
    "application/ports" \
    "adapters/"

# R4: runtime/event_bus/ 不依赖业务层
# event_bus 是核心基础设施，必须对业务层保持无知
# （runtime/bootstrap/ 与 runtime/ports/ 是装配器，不在此规则范围内）
check_includes \
    "R4: runtime/event_bus/ 不依赖业务层" \
    "runtime/event_bus" \
    "domain/" "application/" "adapters/" "services/"

# R5: runtime/scheduler/ 不依赖业务层
check_includes \
    "R5: runtime/scheduler/ 不依赖业务层" \
    "runtime/scheduler" \
    "domain/" "application/" "adapters/" "services/"

# -----------------------------------------------------------------------------
# R6: 框架层不引用框架树之外的头文件
#
# 判据不是"名字像不像项目专属"，而是"能不能在框架树内解析"。
#
# 前一版判据是 grep '#include "m8/'，把"项目专属"绑定到一个写死的项目名。
# 框架无从预知下一个项目叫什么，换个名字规则就完全失效：把
# #include "acme_robot/io_table.h" 注入 domain/ 后全部规则仍报通过。
#
# 反过来立判据即可摆脱对项目命名的依赖：框架不需要知道项目有什么，只需要
# 知道自己有什么。凡引号 include 既不能以仓库根为基准解析、也不在同目录，
# 即为框架外引用——该集合天然覆盖全部项目专属头，与命名无关。
#
# 例外只有 vendor SDK 头：其搜索路径由根 CMakeLists 的 provider 开关在编译期
# 注入（WDF_IO_EXP_ROOT 等），确实不在框架树内。白名单逐项登记，并附加位置
# 约束——必须位于 adapters/**/providers/ 下，使 vendor 依赖无法渗入其它层。
# 新增 vendor provider 须在此显式登记，这一步正是要让评审看见新的外部依赖。
#
# 本规则不替代 R8：指向真实框架文件的 "../" include 是可解析的，会通过本
# 规则却绕过 R1~R5 的前缀匹配，故 R8 仍作为前缀类规则的兜底独立存在。
# -----------------------------------------------------------------------------
VENDOR_INCLUDE_ALLOW='^(io_exp|modbus)/'

TOTAL_RULES=$((TOTAL_RULES + 1))
foreign_includes=""

for layer in common domain application adapters runtime services observability; do
    [ -d "${FW_ROOT}/${layer}" ] || continue

    while IFS= read -r src; do
        src_dir=$(dirname "${src}")
        rel_src="${src#"${FW_ROOT}"/}"

        while IFS= read -r inc; do
            [ -n "${inc}" ] || continue

            # 以仓库根为基准可解析（框架内标准形式）
            if [ -f "${FW_ROOT}/${inc}" ]; then
                continue
            fi
            # 同目录内可解析（provider 内部私有头）
            if [ -f "${src_dir}/${inc}" ]; then
                continue
            fi

            if printf '%s' "${inc}" | grep -qE "${VENDOR_INCLUDE_ALLOW}"; then
                case "${rel_src}" in
                    adapters/*/providers/*)
                        continue
                        ;;
                esac
                foreign_includes+="  ${rel_src}: vendor 头 \"${inc}\" 出现在 adapters/**/providers/ 之外"$'\n'
                continue
            fi

            foreign_includes+="  ${rel_src}: \"${inc}\" 无法在框架树内解析"$'\n'
        done < <(grep -oE '#include[[:space:]]*"[^"]+"' "${src}" 2>/dev/null \
            | sed 's/.*"\(.*\)"/\1/')
    done < <(find "${FW_ROOT}/${layer}" \( -name '*.c' -o -name '*.h' \) -print | sort)
done

if [ -z "$foreign_includes" ]; then
    echo "[PASS] R6: 框架层引号 include 均可在框架树内解析（vendor 头限 providers/）"
else
    echo ""
    echo "[FAIL] R6: 框架层引用了框架树之外的头文件"
    printf '%s' "$foreign_includes"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# R7: 框架层每个 .c 都必须被框架自己的构建清单登记
#
# 前一版判据是 find -name 'm8_*.c'，与 R6 同病：绑定写死的项目前缀。
#
# 改为登记完整性：框架层的每个 .c 都应出现在 cmake/wdf_targets.cmake（导出给
# 项目的分层目标）或根 CMakeLists.txt（有外部 SDK 依赖的 vendor provider）中。
# 混进框架目录的项目文件不会被任何框架目标构建，因此必然在此暴露；顺带也能
# 抓出框架自身的死代码。
#
# 例外：仅由 demo/ 与 tests/ 按路径直接装配、刻意不导出为公共目标的源文件。
# 逐项登记而非按目录豁免——每一项都是"为什么不导出"的显式决定。
#
# 为何这三个不导出（评估过导出为公共目标，结论是代价与收益不对称）：
#
#   safety_sim.c / hw_estop_sim.c
#     仿真安全实现。cutout 与 deferred_stop 只记日志——仿真环境没有真实动力输出
#     可切断。一旦导出为公共目标，真机项目 link 错了，急停切断就变成"写一行日志"，
#     而启动日志一切正常、PORT_REQ_SAFETY 校验也通过（端口确实注册了）。这正是
#     把安全端口从弱符号改为注册表要消除的那类静默失效。保持按路径装配反而是一道
#     屏障：真机项目要用它必须手写路径，那一行会在评审里被看见。
#
#   estop_poll_thread.c
#     可选适配器。项目若已有自己的 DI detector 采集通路，接入它会让同一物理输入
#     产生两条并发事件源。是否接入应当是项目的显式选择，不宜做成 link 即生效。
#
# 三者合计只有 demo 与两个测试目标在用，为此建导出目标属于"为单次使用准备的抽象"。
# 新增豁免项须在此写明不导出的理由，而不只是加一行路径。
# -----------------------------------------------------------------------------
UNEXPORTED_SOURCES="
adapters/inbound/safety/estop_poll_thread.c
adapters/outbound/safety/sim/hw_estop_sim.c
adapters/outbound/safety/sim/safety_sim.c
"

TOTAL_RULES=$((TOTAL_RULES + 1))
unregistered=""

for layer in common domain application adapters runtime services observability; do
    [ -d "${FW_ROOT}/${layer}" ] || continue

    while IFS= read -r src; do
        rel_src="${src#"${FW_ROOT}"/}"

        if printf '%s' "${UNEXPORTED_SOURCES}" | grep -qxF "${rel_src}"; then
            continue
        fi
        if grep -qF -- "${rel_src}" "${FW_ROOT}/cmake/wdf_targets.cmake" 2>/dev/null; then
            continue
        fi
        if grep -qF -- "${rel_src}" "${FW_ROOT}/CMakeLists.txt" 2>/dev/null; then
            continue
        fi

        unregistered+="  ${rel_src}"$'\n'
    done < <(find "${FW_ROOT}/${layer}" -name '*.c' -print | sort)
done

if [ -z "$unregistered" ]; then
    echo "[PASS] R7: 框架层 .c 均已在框架构建清单中登记"
else
    echo ""
    echo "[FAIL] R7: 以下框架层 .c 未被任何框架构建目标登记"
    printf '%s' "$unregistered"
    echo "  （项目专属文件应移出框架目录；框架自有文件须登记到 cmake/wdf_targets.cmake"
    echo "    或根 CMakeLists.txt；刻意不导出的须登记到本脚本 UNEXPORTED_SOURCES）"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# R8: 禁止相对路径向上跳的 include
#
# R1~R5 依赖 "#include \"<层>/" 的前缀形式匹配，若改用 "../" 形式引用上层，
# 依赖违规会绕过全部前缀规则而不被发现。R6 也拦不住这类：".." 形式指向的是
# 真实存在的框架文件，在框架树内可解析。此规则作为兜底：
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
# cloud_model.h（当时在顶层 cloud/）曾 include application/orchestrators/report_scheduler.h，同时
# report_scheduler.c include domain/cloud/cloud_point_watcher.h，两个目录互相依赖、
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
    "R13: services/ 不依赖 adapters/ 或 application/" \
    "services" \
    "adapters/" "application/"

# observability 是旁路设施：任何层都可向它发布记录，它不回调任何层，因此不构成
# 环。这条性质只有在它除 common 之外什么都不依赖时才成立，故这里逐一排除其余层。
check_includes \
    "R14: observability/ 只依赖 common/" \
    "observability" \
    "domain/" "application/" "adapters/" "runtime/" "services/"

# -----------------------------------------------------------------------------
# R15: 头文件保护宏必须等于其路径的全大写下划线形式
#
# 加这条规则的直接原因：本轮核对发现 67 个头文件的保护宏与路径不符，其中包括
# 三类已经造成实际风险的情形：
#
#   撞名风险   DRV_VFD_H / TIME_UTIL_H / SW_ERROR_H 等无任何路径前缀，vendor SDK
#              出现同名宏时，后包含的头会被静默跳过，报"类型未定义"而非重复定义，
#              排查成本远高于改名成本。
#   路径失同步 CORE_BOOTSTRAP_BOOTSTRAP_H —— core/ 是早已改名的旧目录；
#              CONFIG_THREADING_THREAD_CONFIG_H —— 目录从未叫 threading。
#   层级缺失   ADAPTERS_HAL_SIM_HW_HAL_IO_SIM_H 缺 OUTBOUND、多余 HW_。
#
# 这些都是"目录搬过、宏没跟"的遗留。只改一次而不加规则，下次搬目录同样不会跟，
# 等于把同一笔债重新记一遍——所以规则本身才是这项的主要产出。
#
# 排除条件定义：#ifndef TRUE、#ifndef SW_LOG_COMPONENT 这类不是保护宏，
# 判据是只取文件中第一个 #ifndef，且要求其后紧跟同名 #define。
# -----------------------------------------------------------------------------
TOTAL_RULES=$((TOTAL_RULES + 1))
guard_violations=""
while IFS= read -r hdr; do
    rel="${hdr#${FW_ROOT}/}"
    # 首个 #ifndef 及其后一行 #define；\r 不计入宏名（部分头文件是 CRLF）
    guard=$(grep -m1 -E '^#ifndef[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*$' "$hdr" 2>/dev/null \
            | tr -d '\r' | awk '{print $2}')
    [ -z "$guard" ] && continue
    define=$(grep -m1 -E '^#define[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' "$hdr" 2>/dev/null \
             | tr -d '\r' | awk '{print $2}')
    # 首个 #ifndef 未被同名 #define 紧跟，说明它是条件定义而非保护宏，跳过
    [ "$guard" != "$define" ] && continue
    expected=$(printf '%s' "$rel" | tr 'a-z' 'A-Z' | tr -c 'A-Z0-9\n' '_')
    if [ "$guard" != "$expected" ]; then
        guard_violations="${guard_violations}  ${rel}: ${guard}（应为 ${expected}）
"
    fi
done <<EOF
$(find "${FW_ROOT}" -name '*.h' \
    -not -path "${FW_ROOT}/third_party/*" \
    -not -path "${FW_ROOT}/build*/*" \
    -not -path "${FW_ROOT}/tests/stubs/*" \
    -not -path "${FW_ROOT}/demo/*" \
    -print 2>/dev/null | sort)
EOF

if [ -z "$guard_violations" ]; then
    echo "[PASS] R15: 头文件保护宏与路径一致"
else
    echo ""
    echo "[FAIL] R15: 以下头文件的保护宏与路径不一致"
    printf '%s' "$guard_violations"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# R16: 实时可达的互斥量必须启用优先级继承
#
# 对应行为契约 ARCH-16。急停采集线程以 SCHED_FIFO 高优先级运行，其切断链路会
# 取得多把互斥量，而同样的锁被 SCHED_OTHER 周期任务竞争。普通优先级线程持锁
# 期间被抢占时，急停线程的阻塞时长不受任何优先级保护，构成无界优先级反转。
#
# 为何做成分类规则而不是调用图分析：切断链路全程经端口跳转
# （safety_cutout_execute → 项目注册的 cutout → HAL 端口 → 项目适配器），
# 可达集合取决于项目注册了什么，框架静态分析不出来。故改为对每个持锁文件
# 显式分类，RT 可达的必须用 sw_mutex_init_prio_inherit()。
#
# 分类取保守侧：对非 RT 锁启用优先级继承的代价不可测量，对 RT 锁漏启用的代价
# 是无界优先级反转。凡"项目的合理实现可能使其进入急停链路"的锁一律计入 RT。
#
# 本规则同时防两种漏：
#   1) RT 可达文件里出现裸 PTHREAD_MUTEX_INITIALIZER 或 pthread_mutex_init(,NULL)
#   2) 新增持锁文件未登记分类（未登记即报错，强制作者表态）
#
# 加这条规则的直接起因：上一轮转换六把锁时只 grep 了静态宏
# PTHREAD_MUTEX_INITIALIZER，运行期 pthread_mutex_init(&m, NULL) 建的锁全部漏掉，
# 其中 drv_vfd.c 的 io_mutex 确实在切断链路上。单次人工审计会漏，规则不会。
# -----------------------------------------------------------------------------

# RT 可达：必须启用优先级继承
RT_REACHABLE_FILES=(
    "common/log.c"                                                  # 急停边沿每次都写日志
    "runtime/event_bus/event_bus.c"                                 # 急停事件发布
    "adapters/outbound/hal/sim/hal_io_sim.c"                        # 仿真 DO 写
    "adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.c"  # 真机 DO 写
    "adapters/outbound/hal/components/vfd_manager/hal_vfd_manager.c" # 电机停机
    "adapters/outbound/hal/providers/snack/modbus/drv_vfd.c"        # stop_outputs
    "adapters/outbound/hal/providers/snack/modbus/drv_modbus_link.c" # 项目 cutout 可能直写寄存器
)

# 非 RT 可达：允许默认互斥量，须逐个说明依据
NON_RT_FILES=(
    "application/command_gateway.c"                       # 命令提交，非急停路径
    "domain/safety/alarm_registry/alarm_registry.c"        # 由报警采集线程驱动
    "domain/telemetry/device_snapshot.c"                   # 投影读写
    "domain/device_control/patterns/fluid_path.c"          # emergency_off 是无锁原子写
    "adapters/outbound/storage/json/json_deploy_store.c"   # 启动期加载
    "adapters/outbound/storage/json/json_param_store.c"    # 参数存取
    "adapters/outbound/hal/components/adc_gate/hal_adc_gate.c"       # 模拟量采样
    "adapters/outbound/hal/components/sensor_filter/hal_sensor_filter.c" # 传感器滤波
    "adapters/outbound/hal/providers/snack/modbus/drv_voice.c"       # 语音播报
    "application/engine_session/engine_session.c"          # 会话启动
    "observability/core/observation.c"                     # 旁路记录
    "observability/recorder/blackbox_recorder.c"           # 旁路记录
)

rt_violations=""
unclassified=""

while IFS= read -r f; do
    rel="${f#"${FW_ROOT}"/}"
    is_rt=0
    is_known=0
    for k in "${RT_REACHABLE_FILES[@]}"; do
        if [ "$rel" = "$k" ]; then is_rt=1; is_known=1; break; fi
    done
    if [ "$is_known" -eq 0 ]; then
        for k in "${NON_RT_FILES[@]}"; do
            if [ "$rel" = "$k" ]; then is_known=1; break; fi
        done
    fi

    if [ "$is_known" -eq 0 ]; then
        unclassified="${unclassified}  ${rel}"$'\n'
        continue
    fi

    if [ "$is_rt" -eq 1 ]; then
        # RT 可达文件中不得出现裸初始化
        if grep -qE 'PTHREAD_MUTEX_INITIALIZER' "$f" 2>/dev/null; then
            rt_violations="${rt_violations}  ${rel}: 使用了 PTHREAD_MUTEX_INITIALIZER"$'\n'
        fi
        if grep -qE 'pthread_mutex_init[[:space:]]*\([^,]+,[[:space:]]*NULL' "$f" 2>/dev/null; then
            rt_violations="${rt_violations}  ${rel}: 使用了 pthread_mutex_init(, NULL)"$'\n'
        fi
    fi
done <<EOF
$(grep -rlE 'pthread_mutex_t' --include='*.c' "${FW_ROOT}" 2>/dev/null \
    | grep -v "${FW_ROOT}/third_party/" \
    | grep -v "${FW_ROOT}/tests/" \
    | grep -v "${FW_ROOT}/build" \
    | grep -v "${FW_ROOT}/common/sw_mutex.c" \
    | grep -v "${FW_ROOT}/demo/" \
    | sort)
EOF

TOTAL_RULES=$((TOTAL_RULES + 1))
if [ -z "$rt_violations" ] && [ -z "$unclassified" ]; then
    echo "[PASS] R16: 实时可达互斥量均启用优先级继承（${#RT_REACHABLE_FILES[@]} 个 RT 文件已登记）"
else
    echo ""
    if [ -n "$rt_violations" ]; then
        echo "[FAIL] R16: 以下 RT 可达文件使用了默认互斥量"
        printf '%s' "$rt_violations"
        echo "  修正: 改用 common/sw_mutex.h 的 sw_mutex_init_prio_inherit()"
    fi
    if [ -n "$unclassified" ]; then
        echo "[FAIL] R16: 以下持锁文件未登记 RT 可达性分类"
        printf '%s' "$unclassified"
        echo "  修正: 在 check_arch_boundary.sh 的 RT_REACHABLE_FILES 或 NON_RT_FILES 中登记"
        echo "        判据: 该锁是否可能被急停切断链路取得（含项目 cutout 的合理实现）"
    fi
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# R18: 阻塞等待必须分类，等应答类必须有界，且截止时间不得在重试循环内重算
#
# 对应行为契约 ERRM-05、CMD-04。
#
# 为何做成分类规则：ERRM-05 原文写"所有等待操作有超时上限"，逐点核查后该表述
# 不成立——事件总线 dispatch 与引擎会话工作线程都在无限等待"下一件工作到来"，
# 给它们加超时只会得到一个空转唤醒的循环，没有任何收益。真正的不变量是：
# 等"对方的应答/完成"必须有界（否则一次丢失的 post 就永久挂死一条链路），
# 等"新工作到来"不需要有界。这个区分静态判不出来，故逐点登记，未登记即报错。
#
# CMD-04 部分是结构性质：sem_timedwait 是绝对截止时间语义，若 EINTR 重试时
# 重新计算截止时间，每次信号都会把总等待时长再延长一个 wait_ms，超时上限失效。
# 故断言 time_util_fill_deadline 必须出现在重试循环之前，且循环体内没有它。
#
# 已知局限：本规则只扫描下列 WAIT_PRIMITIVES 中显式列出的原语。若引入
# pthread_barrier_wait、mq_receive、epoll_wait 等未列入的阻塞调用，规则不会报错。
# 建表时已核查过 nanosleep/poll/select/epoll_wait/pthread_join/recv/read/sigwait/
# mq_receive/pthread_barrier_wait，框架生产代码中均无真实调用（poll 与 read 的命中
# 是 ops->poll()、ops->read() 这类函数指针调用，属项目注册的回调，不是 POSIX 原语）。
# 新增阻塞原语时须同步扩充 WAIT_PRIMITIVES，否则这条规则对它是空过。
# -----------------------------------------------------------------------------

# 被扫描的阻塞原语，新增阻塞调用方式时须同步扩充
WAIT_PRIMITIVES=(
    sem_wait sem_timedwait
    pthread_cond_wait pthread_cond_timedwait
    clock_nanosleep usleep
)

# 有界等待：等应答或等完成，必须带绝对截止时间
BOUNDED_WAIT_SITES=(
    "application/command_gateway.c:sem_timedwait"          # 等 handler 回执
    "application/engine_session/engine_session.c:sem_timedwait" # 等启动完成
    "runtime/scheduler/periodic_task.c:clock_nanosleep"    # 绝对下一拍唤醒
)

# 无界等待：等新工作到来，不设超时须逐个说明依据
UNBOUNDED_WAIT_SITES=(
    "runtime/event_bus/event_bus.c:sem_wait"               # dispatch 线程等下一个事件
    "application/engine_session/engine_session.c:sem_wait" # 工作线程等下一次启动请求
)

# 固定时长轮询：睡眠时长是编译期常量或配置值，本身即上限
FIXED_SLEEP_SITES=(
    "adapters/inbound/safety/estop_poll_thread.c:usleep"
    "adapters/outbound/hal/providers/snack/io_exp/io_exp_driver.c:usleep"
    "application/engine_session/engine_session.c:usleep"
)

wait_unclassified=""
deadline_violations=""

while IFS= read -r line; do
    [ -z "$line" ] && continue
    f="${line%%:*}"
    prim="${line##*:}"
    rel="${f#"${FW_ROOT}"/}"
    key="${rel}:${prim}"
    is_known=0
    for k in "${BOUNDED_WAIT_SITES[@]}" "${UNBOUNDED_WAIT_SITES[@]}" "${FIXED_SLEEP_SITES[@]}"; do
        if [ "$key" = "$k" ]; then is_known=1; break; fi
    done
    if [ "$is_known" -eq 0 ]; then
        wait_unclassified="${wait_unclassified}  ${key}"$'\n'
    fi
done <<EOF
$(for prim in "${WAIT_PRIMITIVES[@]}"; do
    grep -rlE "(^|[^_[:alnum:]])${prim}[[:space:]]*\(" --include='*.c' "${FW_ROOT}" 2>/dev/null \
        | grep -v "${FW_ROOT}/third_party/" \
        | grep -v "${FW_ROOT}/tests/" \
        | grep -v "${FW_ROOT}/build" \
        | grep -v "${FW_ROOT}/demo/" \
        | while IFS= read -r ff; do echo "${ff}:${prim}"; done
  done | sort -u)
EOF

# CMD-04: 截止时间必须在重试循环之外计算
for site in "${BOUNDED_WAIT_SITES[@]}"; do
    sf="${FW_ROOT}/${site%%:*}"
    sp="${site##*:}"
    [ "$sp" = "sem_timedwait" ] || continue
    [ -f "$sf" ] || continue
    if ! grep -q 'time_util_fill_deadline' "$sf" 2>/dev/null; then
        deadline_violations="${deadline_violations}  ${site%%:*}: 用了 sem_timedwait 但未见 time_util_fill_deadline"$'\n'
        continue
    fi
    # 取 fill_deadline 与 sem_timedwait 的行号，前者必须严格早于后者
    fill_ln="$(grep -n 'time_util_fill_deadline' "$sf" | head -1 | cut -d: -f1)"
    wait_ln="$(grep -n 'sem_timedwait' "$sf" | tail -1 | cut -d: -f1)"
    if [ -n "$fill_ln" ] && [ -n "$wait_ln" ] && [ "$fill_ln" -ge "$wait_ln" ]; then
        deadline_violations="${deadline_violations}  ${site%%:*}: 截止时间计算未早于等待调用"$'\n'
    fi
    # 重试循环体内不得重算截止时间：do{...}while(EINTR) 区间内出现即违规
    if awk '/do[[:space:]]*\{/{ind=1} ind&&/time_util_fill_deadline/{found=1} ind&&/while.*EINTR/{ind=0} END{exit !found}' "$sf"; then
        deadline_violations="${deadline_violations}  ${site%%:*}: EINTR 重试循环内重算了截止时间"$'\n'
    fi
done

TOTAL_RULES=$((TOTAL_RULES + 1))
if [ -z "$wait_unclassified" ] && [ -z "$deadline_violations" ]; then
    echo "[PASS] R18: 阻塞等待均已分类（有界 ${#BOUNDED_WAIT_SITES[@]}、无界 ${#UNBOUNDED_WAIT_SITES[@]}、定时 ${#FIXED_SLEEP_SITES[@]}），截止时间未在重试内重算"
else
    echo ""
    if [ -n "$wait_unclassified" ]; then
        echo "[FAIL] R18: 以下阻塞等待调用点未登记分类"
        printf '%s' "$wait_unclassified"
        echo "  修正: 在 check_arch_boundary.sh 的 BOUNDED_WAIT_SITES / UNBOUNDED_WAIT_SITES / FIXED_SLEEP_SITES 中登记"
        echo '        判据: 等应答或等完成必须有界；仅“等新工作到来”可无界'
    fi
    if [ -n "$deadline_violations" ]; then
        echo "[FAIL] R18: 以下有界等待的截止时间计算位置有问题"
        printf '%s' "$deadline_violations"
        echo "  修正: 截止时间须在 EINTR 重试循环之前计算一次并按指针复用"
    fi
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# R17: 表达式求值器只允许依赖白名单内的框架头
#
# 对应行为契约 ENGN-04（表达式求值不产生副作用）。方案表达式来自部署时下发的
# 资产，若求值能写 IO、触发报警或改状态，方案作者就获得了绕过命令裁决与安全
# 矩阵的旁路——一个条件表达式本该只回答"是否满足"，不该能让设备动作。
#
# 为何用依赖白名单而不是运行期断言：断言只能证明"这次没有副作用"，白名单证明
# "无法有副作用"。求值器不 include IO/报警/事件总线的头，就调不到它们。
# 已核对 engine_expr.o 的未定义符号，框架侧只有 engine_profile_height_at 与
# engine_profile_in_zone 两个，均为读形接口（结果经 out 参数返回、返回值仅表示
# 成功与否），无写入通路。
#
# engine_profile 是允许的例外：轮廓查询是条件判断的合法输入。它自身也只持有一个
# provider 指针，转发给项目实现的读接口。
# -----------------------------------------------------------------------------
EXPR_PURE_FILE="domain/program_engine/engine/engine_expr.c"
EXPR_ALLOWED_INCLUDES='^(domain/program_engine/engine/engine_expr\.h|domain/program_engine/engine/engine_profile\.h|common/)'

expr_violations=""
if [ -f "${FW_ROOT}/${EXPR_PURE_FILE}" ]; then
    while IFS= read -r inc; do
        [ -z "$inc" ] && continue
        if ! printf '%s' "$inc" | grep -qE "$EXPR_ALLOWED_INCLUDES"; then
            expr_violations="${expr_violations}  ${EXPR_PURE_FILE}: #include \"${inc}\""$'\n'
        fi
    done <<EOF
$(grep -oE '#include[[:space:]]*"[^"]+"' "${FW_ROOT}/${EXPR_PURE_FILE}" 2>/dev/null \
    | sed 's/.*"\(.*\)"/\1/')
EOF
else
    expr_violations="  ${EXPR_PURE_FILE}: 文件不存在（R17 的被检查目标已改名或移动，请同步更新规则）"$'\n'
fi

TOTAL_RULES=$((TOTAL_RULES + 1))
if [ -z "$expr_violations" ]; then
    echo "[PASS] R17: 表达式求值器依赖白名单（仅 common/ 与 engine_profile）"
else
    echo ""
    echo "[FAIL] R17: 表达式求值器引入了白名单外的依赖"
    printf '%s' "$expr_violations"
    echo "  依据: 求值能写 IO / 触发报警即等于方案资产可绕过命令裁决与安全矩阵"
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
