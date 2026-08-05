#!/usr/bin/env bash
# check_doc_refs.sh — 文档引用与代码一致性检查
#
# 用法:
#   ./scripts/check_doc_refs.sh [framework_root]
#
# 退出码:
#   0  无违规
#   1  发现违规
#   2  参数错误
#
# 校验项:
#   D1  文档引用的框架路径必须存在
#   D2  文档引用的函数符号必须能在代码中找到
#   D3  文档之间的相对链接必须可解析
#   D4  doc/ai/ 下自动生成的索引与代码一致
#   D5  「第 N 节」形式的章节引用必须指向真实存在的章节
#   D6  文档引用的测试名必须存在对应测试源文件
#
# 为何需要本脚本：
#
# 文档失真不是靠人工评审能拦住的。本轮核查在 24 篇文档里查出 6 处不存在的路径与
# 3 处不存在的符号，其中 `CloudModel模块设计.md` 的文件清单把 JSON 编解码写成
# `domain/cloud/cloud_point_dispatch.c` 的职责——而那次改动的全部目的正是把编解码
# 移出 domain（见 architecture/08 第 23 节）。文档在描述一个被 R9b 明令禁止的状态，
# 而 R9b 本身是通过的：**边界规则管代码，没有任何规则管文档**。
#
# 这类失真有共同形状：都是"机器能算、人在手抄"的信息。手抄件必然滞后于被抄件，
# 这与 tests/reports/ 曾声称 event_bus 8 例而实际 13 例是同一个问题
# （见 doc/contract/行为契约.md 第 5 节）。因此判据不是"文档写得对不对"——那不可自动判定——
# 而是"文档指到的东西还在不在"，这一条可判定，且恰好覆盖绝大多数实际失真。
#
# 扫描全仓 Markdown 而非只扫 doc/：把范围限在 doc/ 时，根 CLAUDE.md 与
# tests/reports/*.md 都在盲区，而扩大范围后当即在后者查出两处失效路径、一处
# 断链和三处失真的用例数。判据与文件位置无关，限定目录只是漏检。
#
# 白名单的存在是必要的而非妥协：变更记录必须能引用已删除的符号与旧路径，否则
# "为什么删掉它"就无处可写。故白名单逐项登记并附理由，新增一项即是一次显式决定。

set -uo pipefail

FW_ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"

if [ ! -d "${FW_ROOT}/doc" ] || [ ! -d "${FW_ROOT}/domain" ]; then
    echo "错误: '${FW_ROOT}' 不是有效的 framework 根目录" >&2
    exit 2
fi

cd "${FW_ROOT}"

TOTAL_VIOLATIONS=0
TOTAL_RULES=0

# -----------------------------------------------------------------------------
# 扫描范围：全仓被 git 跟踪的 Markdown，不限于 doc/
#
# 原先只扫 doc/，于是根 CLAUDE.md 与 tests/reports/*.md 都在盲区里——而后者正是
# 本仓最早那起文档失真的现场（event_bus.md 长期声称 8/8 通过，实际 13 例）。
# 判据本身与文件位置无关，限定目录只是漏检。
# -----------------------------------------------------------------------------
mapfile -t DOC_FILES < <(git -c core.quotepath=false ls-files '*.md' 2>/dev/null | sort)
if [ ${#DOC_FILES[@]} -eq 0 ]; then
    mapfile -t DOC_FILES < <(find . -name '*.md' -not -path './third_party/*' \
        -not -path './build*' | sed 's|^\./||' | sort)
fi

# 框架顶层目录：路径引用须以其中之一开头才纳入检查
LAYER_RE='^(common|domain|ports|application|adapters|runtime|services|observability|demo|tests|cmake|scripts|tools|third_party|doc)/'

# -----------------------------------------------------------------------------
# 允许不存在的路径引用，逐项登记理由
#
# 判据：只有"记录该路径已不存在"这一种情形可豁免。凡是描述当前结构的引用，
# 一律不得进入本表——那正是本规则要抓的东西。
# -----------------------------------------------------------------------------
PATH_ALLOW="
services/dev_ctx|architecture/08 第 18.4 节记录该层已删除并入 device_snapshot
adapters/providers|architecture/08 第 12 节的泛指写法，指 adapters 下各 providers 目录
"

# -----------------------------------------------------------------------------
# 允许在代码中找不到的符号，逐项登记理由
# -----------------------------------------------------------------------------
SYMBOL_ALLOW="
cloud_model_register_scheduler|architecture/08 第 22.2 节记录该函数已删除
cloud_model_request_resync|architecture/08 第 22.2 节记录该函数已删除
framework_reset_for_test|architecture/08 第 6 节记录该方案未被采用
defined|C 预处理器语法，非框架符号
"

allow_lookup() {
    # allow_lookup <表> <键> —— 命中返回 0
    printf '%s' "$1" | grep -qE "^${2}\|"
}

# -----------------------------------------------------------------------------
# D1: 文档引用的框架路径必须存在
#
# 只检查反引号内、以框架顶层目录开头的路径。三类写法不参与存在性判定：
#
#   glob 模式    `ports/**/cloud/`、`cloud_model.{h,c}` 是刻意的模式表达
#   占位符       `adapters/outbound/hal/providers/<vendor>/` 中的尖括号是待填项
#   .md 文件     文档间链接由 D3 按文档基准解析，用文件系统基准判会全部误报
#
# 另接受"模块基名"写法：`application/side_effect_router` 指模块而非文件，
# 同名 .c 或 .h 存在即视为可解析。文档里谈模块时不写扩展名是正常表述，
# 强制补全反而会让"指整个模块"与"指某个文件"失去区分。
# -----------------------------------------------------------------------------
TOTAL_RULES=$((TOTAL_RULES + 1))
missing_paths=""

while IFS= read -r line; do
    [ -n "${line}" ] || continue
    doc_file="${line%%:*}"
    ref="${line#*:}"

    case "${ref}" in
        *'*'* | *'{'* | *'}'* | *'<'* | *'>'*) continue ;;
        *.md) continue ;;
    esac

    ref="${ref%/}"

    [ -e "${ref}" ] && continue
    # 模块基名：同名 .c 或 .h 存在即可
    if [ -e "${ref}.c" ] || [ -e "${ref}.h" ]; then
        continue
    fi

    if allow_lookup "${PATH_ALLOW}" "${ref}"; then
        continue
    fi

    missing_paths+="  ${doc_file}: \`${ref}\` 不存在"$'\n'
done < <(
    for f in "${DOC_FILES[@]}"; do
        grep -oE '`[^`]+`' "${f}" 2>/dev/null \
            | tr -d '`' \
            | grep -E "${LAYER_RE}" \
            | while IFS= read -r r; do printf '%s:%s\n' "${f}" "${r}"; done
    done | sort -u
)

if [ -z "${missing_paths}" ]; then
    echo "[PASS] D1: 文档引用的框架路径均存在"
else
    echo ""
    echo "[FAIL] D1: 以下文档引用了不存在的框架路径"
    printf '%s' "${missing_paths}"
    echo "  修正: 更新文档指向当前路径；若为记录已删除路径，登记到本脚本 PATH_ALLOW"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# D2: 文档引用的函数符号必须能在代码中找到
#
# 只取反引号内 `name()` 形式且长度 ≥5 的小写标识符——短名与大写宏误报率高，
# 而函数名是文档里最常失同步的一类（改名后文档留在旧名，读者按旧名搜不到）。
#
# 搜索范围含 tests/：文档会引用测试辅助函数，它们同样是真实符号。
# -----------------------------------------------------------------------------
TOTAL_RULES=$((TOTAL_RULES + 1))
missing_symbols=""

SEARCH_DIRS="common domain ports application adapters runtime services observability demo tests"

while IFS= read -r line; do
    [ -n "${line}" ] || continue
    doc_file="${line%%:*}"
    sym="${line#*:}"

    if allow_lookup "${SYMBOL_ALLOW}" "${sym}"; then
        continue
    fi

    if grep -rqE --include='*.c' --include='*.h' "\b${sym}\b" ${SEARCH_DIRS} 2>/dev/null; then
        continue
    fi

    missing_symbols+="  ${doc_file}: \`${sym}()\` 在代码中不存在"$'\n'
done < <(
    for f in "${DOC_FILES[@]}"; do
        grep -oE '`[a-z_][a-z0-9_]{4,}\(\)`' "${f}" 2>/dev/null \
            | tr -d '`()' \
            | while IFS= read -r r; do printf '%s:%s\n' "${f}" "${r}"; done
    done | sort -u
)

if [ -z "${missing_symbols}" ]; then
    echo "[PASS] D2: 文档引用的函数符号均可在代码中找到"
else
    echo ""
    echo "[FAIL] D2: 以下文档引用了不存在的函数符号"
    printf '%s' "${missing_symbols}"
    echo "  修正: 改为当前符号名；若为记录已删除符号，登记到本脚本 SYMBOL_ALLOW"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# D3: 文档之间的相对链接必须可解析
#
# 文档互引用两种写法：Markdown 链接 [text](path.md) 与反引号裸路径 `path.md`。
# 两种都按"相对本文件所在目录"与"相对 doc/"两种基准尝试解析，任一成立即通过——
# 仓库里两种写法并存，统一成本高于收益。
# -----------------------------------------------------------------------------
TOTAL_RULES=$((TOTAL_RULES + 1))
broken_links=""

while IFS= read -r line; do
    [ -n "${line}" ] || continue
    doc_file="${line%%:*}"
    target="${line#*:}"
    doc_dir="$(dirname "${doc_file}")"

    case "${target}" in
        http*) continue ;;
    esac

    [ -e "${doc_dir}/${target}" ] && continue
    [ -e "doc/${target}" ] && continue
    [ -e "${target}" ] && continue

    broken_links+="  ${doc_file}: 链接 \`${target}\` 无法解析"$'\n'
done < <(
    for f in "${DOC_FILES[@]}"; do
        {
            grep -oE '\]\([^)]+\.md\)' "${f}" 2>/dev/null | sed 's/^](\(.*\))$/\1/'
            grep -oE '`[^`]+\.md`' "${f}" 2>/dev/null | tr -d '`'
        } | while IFS= read -r r; do printf '%s:%s\n' "${f}" "${r}"; done
    done | sort -u
)

if [ -z "${broken_links}" ]; then
    echo "[PASS] D3: 文档间链接均可解析"
else
    echo ""
    echo "[FAIL] D3: 以下文档链接无法解析"
    printf '%s' "${broken_links}"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# D4: doc/ai/ 下自动生成的索引必须与代码一致
#
# 检索层（事件全表、端口全表、代码地图）全部从代码抽取。放进门禁的意义在于：
# 改了代码而没重新生成索引即失败，索引因此不可能落后于代码——这正是手写文档
# 做不到的那件事（D1/D2 只能查"指到的东西还在不在"，查不出"少了新增的东西"）。
# -----------------------------------------------------------------------------
TOTAL_RULES=$((TOTAL_RULES + 1))
if [ ! -x "${FW_ROOT}/scripts/gen_doc_index.py" ]; then
    echo ""
    echo "[FAIL] D4: 缺少 scripts/gen_doc_index.py"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
elif gen_output="$("${FW_ROOT}/scripts/gen_doc_index.py" --check 2>&1)"; then
    printf '%s\n' "${gen_output}" | sed 's/^\[PASS\] /[PASS] D4: /'
else
    echo ""
    printf '%s\n' "${gen_output}" | sed 's/^\[FAIL\] /[FAIL] D4: /'
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# D5: 「第 N 节」形式的章节引用必须指向真实存在的章节
#
# 章节号是本仓文档最脆的一种引用：文档内互引全靠它，而它没有任何符号性——
# 中间插入一节，后面所有引用静默偏移一位，读者跳到邻近章节且不会察觉。
# architecture/08 单篇就有 44 处「第 N 节」互引，拆分或重排该文档时必然出错。
#
# 判据：引用的章节号须存在于同一文档的 `## N.` 或 `### N.M` 标题中。跨文档
# 引用（「见 xxx.md 第 N 节」）取被引文档判定；同一行出现多个引用时逐个判。
# -----------------------------------------------------------------------------
TOTAL_RULES=$((TOTAL_RULES + 1))

if bad_sections="$(python3 - "${FW_ROOT}" <<'PYEOF'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
docs = sorted(
    p for p in root.rglob("*.md")
    if not any(part in {"third_party", ".git"} or part.startswith("build")
               for part in p.relative_to(root).parts)
)

# 每篇文档实际拥有的章节号集合（含 N 与 N.M 两级）
owned: dict[Path, set[str]] = {}
for d in docs:
    nums = set()
    for line in d.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"^#{2,4}\s+(\d+(?:\.\d+)?)[.、 ]", line)
        if m:
            nums.add(m.group(1))
            nums.add(m.group(1).split(".")[0])
    owned[d] = nums

# 按文件名与去扩展名的词干两种键索引：跨文档引用有 `xxx.md` 第 N 节 与
# `architecture/04` 第 N 节 两种写法，后者不带扩展名。
by_name: dict[str, list[Path]] = {}
for d in docs:
    by_name.setdefault(d.name, []).append(d)
    by_name.setdefault(d.stem, []).append(d)

# 引用与「第」之间常隔着反引号（`xxx.md` 第 N 节），必须允许，否则文件名组
# 匹配不上、被当成同文档引用 —— 这正是 D5 首次运行时三处误报的原因。
# 两种引用语法都要覆盖：「第 N 节」与「§N」。仓库里两种并存，且 §N 形式同样会在
# 重排章节时静默偏移——本轮给 Runtime 插入一节文件清单后，两处 §3.x 就指错了，
# 而当时只查「第 N 节」的规则毫无反应。
ref_re = re.compile(
    r"(?:([\w一-鿿./-]+?(?:\.md)?)[`」』\s]*(?:的)?[`\s]*)?"
    r"(?:第\s*(\d+(?:\.\d+)?)\s*节|§\s*(\d+(?:\.\d+)?))"
)
bad = []

for d in docs:
    text = d.read_text(encoding="utf-8", errors="replace")
    for lineno, line in enumerate(text.splitlines(), 1):
        for m in ref_re.finditer(line):
            fname = m.group(1)
            num = m.group(2) or m.group(3)
            target = d
            if fname:
                key = Path(fname).name
                cands = by_name.get(key) or by_name.get(Path(key).stem)
                # 无法唯一定位（不存在或同名多份）时跳过，不误报
                if not cands or len(set(cands)) != 1:
                    if key.endswith(".md") or "/" in fname:
                        continue
                    # 既不是文件名也不是路径，说明「第 N 节」前面只是普通文字
                    fname = None
                else:
                    target = cands[0]
            if num not in owned[target]:
                where = f"（{target.name}）" if target is not d else ""
                rel_d = d.relative_to(root).as_posix()
                form = "第 %s 节" % num if m.group(2) else "§%s" % num
                bad.append(f"  {rel_d}:{lineno}: {form}{where} 不存在")

if bad:
    print("\n".join(bad))
    sys.exit(1)
sys.exit(0)
PYEOF
)"; then
    echo "[PASS] D5: 章节引用均指向真实存在的章节"
else
    echo ""
    echo "[FAIL] D5: 以下章节引用指向不存在的章节"
    printf '%s\n' "${bad_sections}"
    echo "  修正: 更新章节号；拆分或重排文档时须同步全部「第 N 节」引用"
    TOTAL_VIOLATIONS=$((TOTAL_VIOLATIONS + 1))
fi

# -----------------------------------------------------------------------------
# D6: 文档引用的测试名必须存在对应测试源文件
#
# 模块设计文档的「测试覆盖」小节逐条列出测试名。测试改名或删除后，这些表格会留在
# 旧名上，而读者据此找不到测试——本仓 tests/reports/ 手写覆盖表声称 event_bus 8 例
# 而实际 13 例，就是同类失真。
#
# 本规则只查"名字指到的测试还在不在"，不查覆盖描述是否准确（那不可自动判定）；
# 用例数与通过数一律不写进文档，由 check_all.sh 每次生成的报告为准。
# -----------------------------------------------------------------------------
TOTAL_RULES=$((TOTAL_RULES + 1))
missing_tests=""

while IFS= read -r line; do
    [ -n "${line}" ] || continue
    doc_file="${line%%:*}"
    tname="${line#*:}"

    # 两种合法形态：测试目标（同名 .c 文件）与单个 Unity 用例函数（源码内定义）。
    # 只查文件名会把用例函数全判成失实——文档引用具体用例是正常且有价值的表述，
    # 那是"哪一条断言锁定了这个行为"的唯一指路方式。
    if find tests -name "${tname}.c" -print -quit 2>/dev/null | grep -q .; then
        continue
    fi
    if grep -rqE --include='*.c' "(void|static void)[[:space:]]+${tname}[[:space:]]*\(" tests 2>/dev/null; then
        continue
    fi

    missing_tests+="  ${doc_file}: 测试 \`${tname}\` 既无同名源文件也无同名用例函数"$'\n'
done < <(
    for f in "${DOC_FILES[@]}"; do
        grep -oE '`test_[a-z0-9_]+`' "${f}" 2>/dev/null \
            | tr -d '`' \
            | while IFS= read -r r; do printf '%s:%s\n' "${f}" "${r}"; done
    done | sort -u
)

if [ -z "${missing_tests}" ]; then
    echo "[PASS] D6: 文档引用的测试名均存在对应源文件"
else
    echo ""
    echo "[FAIL] D6: 以下文档引用了不存在的测试"
    printf '%s' "${missing_tests}"
    echo "  修正: 改为当前测试名，或删除该行"
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
