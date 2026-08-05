#!/usr/bin/env python3
"""gen_doc_index.py — 生成 doc/ai/ 下的检索层文档

用法:
    ./scripts/gen_doc_index.py            # 生成
    ./scripts/gen_doc_index.py --check    # 只校验已生成文件是否为最新，不写盘

退出码:
    0  生成成功 / --check 下内容已是最新
    1  --check 下发现内容过期
    2  参数或环境错误

为何生成而不手写
----------------
doc/ 已有两类手写文档：architecture/ 面向评审，module-design/ 面向维护。它们的
共同问题是"机器能算、人在手抄"——本轮核查在 24 篇文档里查出 6 处不存在的路径与
3 处不存在的符号，其中 CloudModel 的文件清单把 JSON 编解码写成 domain 侧职责，
而那次改动的全部目的正是把编解码移出 domain。

因此第三类（AI 检索层）如果也手写，只会立刻变成第四份失同步副本。同一个道理
doc/contract/行为契约.md 第 5 节已论证过，tests/reports/ 也已因此出过事故（手写覆盖表声称
event_bus 8 例、实际 13 例）。

所以本脚本的输出全部从代码抽取：事件表来自 common/event_types.h 的宏定义与全仓
的 event_publish / 订阅表调用点，端口表来自 ports/**/*.h 的 doxygen @brief 与
注册表，目录表来自 cmake/wdf_targets.cmake。框架 79 个头文件 @brief 覆盖率 100%，
这些信息本来就在代码里，缺的只是抽取。

--check 模式进门禁：代码改了而索引没重生成即失败，这样索引不可能落后于代码。
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AI_DIR = ROOT / "doc" / "ai"

# 抽取范围：框架各层。tests/ 与 demo/ 不入索引——它们是消费方而非框架契约。
LAYERS = [
    "common",
    "domain",
    "ports",
    "application",
    "adapters",
    "runtime",
    "services",
    "observability",
]

BANNER = """<!-- 本文件由 scripts/gen_doc_index.py 自动生成，请勿手工编辑。 -->
<!-- 重新生成：./scripts/gen_doc_index.py -->
"""


def sources(exts=(".c", ".h")) -> list[Path]:
    out = []
    for layer in LAYERS:
        base = ROOT / layer
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            if p.suffix in exts and p.is_file():
                out.append(p)
    return out


def rel(p: Path) -> str:
    return p.relative_to(ROOT).as_posix()


# ---------------------------------------------------------------------------
# 符号索引：事件 / 端口 / 错误码 / 容量常量
# ---------------------------------------------------------------------------

EVT_DEF_RE = re.compile(
    r"^#define\s+(EVT_[A-Z0-9_]+)\s+EVT_MAKE\(\s*(EVT_CAT_[A-Z0-9_]+)\s*,\s*(EVT_[A-Z0-9_]+)\s*\)",
    re.M,
)
# 注释前后一律用 [ \t]* 而非 \s*：\s* 会跨过行尾。无尾注释的枚举项（EVT_CAT_NONE）
# 一旦让 \s* 吃掉换行与下一行缩进，下一项的 ^ 锚点就不再成立，该项被静默跳过。
# 首次实现正是这样漏掉了 EVT_CAT_HW——而它装着急停事件。
EVT_CAT_RE = re.compile(
    r"^[ \t]*(EVT_CAT_[A-Z0-9_]+)[ \t]*=[ \t]*(\d+),?[ \t]*(?:/\*\*<[ \t]*(.*?)[ \t]*\*/)?[ \t]*$",
    re.M,
)
EVT_ID_RE = re.compile(r"^#define\s+(EVT_[A-Z0-9_]*_ID_[A-Z0-9_]+)\s+(\d+)U", re.M)


def collect_events() -> str:
    hdr = (ROOT / "common" / "event_types.h").read_text(encoding="utf-8")

    cats = {}
    for name, num, desc in EVT_CAT_RE.findall(hdr):
        if name.endswith(("_NONE", "_MAX")):
            continue
        cats[name] = (int(num), desc or "")

    ids = {name: int(num) for name, num in EVT_ID_RE.findall(hdr)}

    # 一致性自检：每个事件宏引用的类别都必须已被解析到。
    # 抽取脚本静默漏掉一整个类别不会有任何征兆——输出看起来完全正常，只是少了一节。
    # 首版就漏了 EVT_CAT_HW（含急停事件）。宁可在这里硬失败。
    referenced = {cat for _macro, cat, _id in EVT_DEF_RE.findall(hdr)}
    missing = referenced - set(cats)
    if missing:
        raise RuntimeError(
            f"事件类别解析不全，缺 {sorted(missing)}；"
            "请检查 EVT_CAT_RE 是否匹配 common/event_types.h 的当前写法"
        )

    # 发布点：event_publish(EVT_XXX, ...)
    publishers: dict[str, set[str]] = {}
    # 订阅点：event_subscribe(EVT_XXX, ...) 与订阅表 { EVT_XXX, handler }
    subscribers: dict[str, set[str]] = {}

    pub_re = re.compile(r"event_publish\s*\(\s*(EVT_[A-Z0-9_]+)")
    sub_re = re.compile(r"event_subscribe\s*\(\s*(EVT_[A-Z0-9_]+)")
    tbl_re = re.compile(r"\{\s*(EVT_[A-Z0-9_]+)\s*,\s*[a-z_]")

    for p in sources((".c",)):
        text = p.read_text(encoding="utf-8", errors="replace")
        where = rel(p)
        for m in pub_re.finditer(text):
            publishers.setdefault(m.group(1), set()).add(where)
        for m in sub_re.finditer(text):
            subscribers.setdefault(m.group(1), set()).add(where)
        for m in tbl_re.finditer(text):
            subscribers.setdefault(m.group(1), set()).add(where)

    lines = [
        "## 事件全表",
        "",
        f"共 {len(EVT_DEF_RE.findall(hdr))} 个事件，定义于 `common/event_types.h`。",
        "",
        "事件类型为 `uint16_t` 复合编码：`type = (category << 8) | local_id`。",
        "「发布」与「订阅」列由代码调用点抽取；空表示框架内无调用点，"
        "该事件供项目侧发布或订阅。",
        "",
    ]

    by_cat: dict[str, list[tuple[str, str, int]]] = {}
    for macro, cat, id_macro in EVT_DEF_RE.findall(hdr):
        by_cat.setdefault(cat, []).append((macro, id_macro, ids.get(id_macro, -1)))

    for cat in sorted(cats, key=lambda c: cats[c][0]):
        num, desc = cats[cat]
        items = sorted(by_cat.get(cat, []), key=lambda t: t[2])
        lines.append(f"### `{cat}` = {num}" + (f" — {desc}" if desc else ""))
        lines.append("")
        lines.append("| 事件 | local_id | 发布 | 订阅 |")
        lines.append("|------|----------|------|------|")
        for macro, _id_macro, local_id in items:
            pub = "<br>".join(f"`{x}`" for x in sorted(publishers.get(macro, ()))) or "—"
            sub = "<br>".join(f"`{x}`" for x in sorted(subscribers.get(macro, ()))) or "—"
            lines.append(f"| `{macro}` | {local_id} | {pub} | {sub} |")
        lines.append("")

    return "\n".join(lines)


BRIEF_RE = re.compile(r"@brief\s+(.+)")


def file_brief(p: Path) -> str:
    """取文件头 doxygen 的 @brief。框架 79 个头文件覆盖率 100%。"""
    text = p.read_text(encoding="utf-8", errors="replace")
    m = BRIEF_RE.search(text)
    return m.group(1).strip().rstrip("*/").strip() if m else ""


FUNC_RE = re.compile(r"^(?:sw_err_t|void|bool|int|size_t|uint\d+_t|const\s+\w+\s*\*)\s+(\w+)\s*\(", re.M)


def collect_ports() -> str:
    port_headers = sorted((ROOT / "ports").rglob("*.h"))

    lines = [
        "## 端口全表",
        "",
        f"共 {len(port_headers)} 个端口头文件。端口是框架与项目之间唯一的契约边界；",
        "「注册入口」列取自头文件中的 `*_register` 声明，未列出即该头不提供注册。",
        "",
        "| 契约头 | 职责 | 注册入口 |",
        "|--------|------|----------|",
    ]

    for p in port_headers:
        text = p.read_text(encoding="utf-8", errors="replace")
        regs = sorted({f for f in FUNC_RE.findall(text) if f.endswith("_register")})
        reg = "<br>".join(f"`{r}()`" for r in regs) or "—"
        lines.append(f"| `{rel(p)}` | {file_brief(p) or '—'} | {reg} |")

    lines.append("")
    return "\n".join(lines)


# 接受 /* */ 与 /**< */ 两种尾注释：sw_error.h 用前者，事件类别枚举用后者。
# 同样用 [ \t]* 不用 \s*，理由见 EVT_CAT_RE。
ERR_RE = re.compile(
    r"^[ \t]*(SW_[A-Z0-9_]+)[ \t]*=[ \t]*(-?\d+),?[ \t]*(?:/\*+<?[ \t]*(.*?)[ \t]*\*/)?[ \t]*$",
    re.M,
)


def collect_errors() -> str:
    hdr = (ROOT / "common" / "sw_error.h").read_text(encoding="utf-8")
    rows = ERR_RE.findall(hdr)

    lines = [
        "## 错误码",
        "",
        "定义于 `common/sw_error.h`。**数值不得变动**：错误码经 protobuf 跨进程传给",
        "观测进程，删除或改动中间值会让观测侧解读出的原因全部错位。",
        "",
        "分类判定用 `sw_err_is_transient()` / `sw_err_is_caller_fault()` /",
        "`sw_err_is_missing_binding()`，不要在调用点重新枚举错误码。",
        "",
        "| 错误码 | 数值 | 含义 |",
        "|--------|------|------|",
    ]
    for name, val, desc in rows:
        lines.append(f"| `{name}` | {val} | {desc or '—'} |")
    lines.append("")
    return "\n".join(lines)


CAP_FILES = [
    "runtime/event_bus/event_bus_config.h",
    "runtime/scheduler/thread_registry.h",
    "domain/safety/model/alarm_types.h",
    "runtime/config/thread_config.h",
]
# 名字中"含"容量语义词即可，不要求以其结尾：EVENT_BUS_MAX_SUBS_PER_EVT 的 MAX 在中间。
CAP_RE = re.compile(
    r"^#define\s+([A-Z][A-Z0-9_]*(?:MAX|SIZE|COUNT|LIMIT|_MS|_PRIO)[A-Z0-9_]*)\s+(\S+)", re.M
)


def collect_capacities() -> str:
    lines = [
        "## 容量与门限常量",
        "",
        "这些常量都附有实测依据与余量说明，见各头文件的头部注释。调整容量须同步",
        "更新该注释与对应测试的断言门限。",
        "",
        "| 常量 | 值 | 定义位置 |",
        "|------|----|----------|",
    ]
    for f in CAP_FILES:
        p = ROOT / f
        if not p.is_file():
            continue
        for name, val in CAP_RE.findall(p.read_text(encoding="utf-8", errors="replace")):
            lines.append(f"| `{name}` | `{val}` | `{f}` |")
    lines.append("")
    return "\n".join(lines)


def build_symbol_index() -> str:
    return "\n".join(
        [
            BANNER,
            "# 符号索引",
            "",
            "本文件供快速定位框架符号：事件、端口、错误码、容量常量。",
            "内容全部由 `scripts/gen_doc_index.py` 从代码抽取，不手工维护。",
            "",
            "设计意图与取舍见 `doc/architecture/`，代码级契约见 `doc/module-design/`。",
            "",
            collect_events(),
            collect_ports(),
            collect_errors(),
            collect_capacities(),
        ]
    )


# ---------------------------------------------------------------------------
# 代码地图：目录 → CMake 目标 → 设计文档
# ---------------------------------------------------------------------------

TARGET_RE = re.compile(r"_wdf_add_interface_lib\(\s*(\w+)(.*?)^\)", re.M | re.S)


def collect_targets() -> dict[str, list[str]]:
    cm = (ROOT / "cmake" / "wdf_targets.cmake").read_text(encoding="utf-8")
    out = {}
    for name, body in TARGET_RE.findall(cm):
        srcs = re.findall(r"([A-Za-z0-9_/.-]+\.c)\b", body)
        out[name] = sorted(set(srcs))
    return out


def build_code_map() -> str:
    targets = collect_targets()

    # 目录 → 覆盖它的 CMake 目标
    dir_targets: dict[str, set[str]] = {}
    for tgt, srcs in targets.items():
        for s in srcs:
            dir_targets.setdefault(str(Path(s).parent), set()).add(tgt)

    # 目录 → 提及它的设计文档
    doc_files = sorted((ROOT / "doc").rglob("*.md"))
    doc_texts = {}
    for d in doc_files:
        if d.parent == AI_DIR:
            continue
        doc_texts[rel(d)] = d.read_text(encoding="utf-8", errors="replace")

    root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")

    # 刻意不导出为公共目标、由 demo/ 与 tests/ 按路径直接装配的源文件。
    # 名单取自 check_arch_boundary.sh 的 UNEXPORTED_SOURCES（R7 的例外），
    # 从那里读而不在此重抄——两处各写一份必然失同步。
    boundary_sh = (ROOT / "scripts" / "check_arch_boundary.sh").read_text(encoding="utf-8")
    m = re.search(r'UNEXPORTED_SOURCES="\n(.*?)\n"', boundary_sh, re.S)
    unexported = set(m.group(1).split()) if m else set()

    lines = [
        BANNER,
        "# 代码地图",
        "",
        "目录 → CMake 目标 → 设计文档 的对应关系。由 `scripts/gen_doc_index.py`",
        "从 `cmake/wdf_targets.cmake` 与 `doc/` 抽取，不手工维护。",
        "",
        "「设计文档」列只取**深度提及**该目录的文档（提及次数 ≥3），并按次数排序；",
        "只顺带提一句的文档不列出，否则 `common/` 这类目录会列出十几篇而失去指向性。",
        "标 **无** 说明该目录没有任何文档深度覆盖——这本身是可行动的信息，",
        "已知缺口与原因记在 `doc/module-design/README.md` 的「尚未收录的代码」一节。",
        "",
        "「CMake 目标」列的 `vendor（根 CMakeLists）` 指由 `WDF_ENABLE_*` 选项以 STATIC",
        "库形式提供的可选 provider，它们有外部 SDK 依赖，不作为无条件导出的 INTERFACE 源。",
        "",
        "| 目录 | 源文件 | CMake 目标 | 设计文档 |",
        "|------|--------|-----------|----------|",
    ]

    all_dirs = set()
    for p in sources((".c", ".h")):
        all_dirs.add(str(p.parent.relative_to(ROOT)))

    for d in sorted(all_dirs):
        c_files = sorted((ROOT / d).glob("*.c"))
        n = len(c_files) + len(list((ROOT / d).glob("*.h")))

        tgt_set = sorted(dir_targets.get(d, ()))
        if tgt_set:
            tgts = "<br>".join(f"`{t}`" for t in tgt_set)
        elif not c_files:
            tgts = "—（仅头文件）"
        elif any(rel(f) in root_cmake for f in c_files):
            tgts = "vendor（根 CMakeLists）"
        elif all(rel(f) in unexported for f in c_files):
            tgts = "刻意不导出（R7 例外）"
        else:
            # 到这里说明有 .c 既不属任何 INTERFACE 目标、也未登记豁免 —— R7 会失败
            tgts = "**未登记**"

        # 深度提及：出现 3 次以上才算该文档在讲这个目录
        scored = [(v.count(d), k) for k, v in doc_texts.items() if v.count(d) >= 3]
        scored.sort(key=lambda t: (-t[0], t[1]))
        doc_col = (
            "<br>".join(f"`{Path(k).relative_to('doc') if k.startswith('doc/') else k}` ({c})"
                        for c, k in scored[:3])
            or "**无**"
        )
        if len(scored) > 3:
            doc_col += f"<br>等 {len(scored)} 篇"

        lines.append(f"| `{d}/` | {n} | {tgts} | {doc_col} |")

    lines.append("")
    lines.append("## CMake 目标与源清单")
    lines.append("")
    lines.append("新增框架 `.c` 必须登记到 `cmake/wdf_targets.cmake` 或根 `CMakeLists.txt`，")
    lines.append("否则架构边界检查 R7 失败。")
    lines.append("")
    lines.append("| 目标 | 源文件数 |")
    lines.append("|------|----------|")
    for t in sorted(targets):
        lines.append(f"| `{t}` | {len(targets[t])} |")
    lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
def outputs() -> dict[Path, str]:
    return {
        AI_DIR / "符号索引.md": build_symbol_index(),
        AI_DIR / "代码地图.md": build_code_map(),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只校验是否最新，不写盘")
    args = ap.parse_args()

    try:
        results = outputs()
    except FileNotFoundError as e:
        print(f"错误: 抽取源缺失 — {e}", file=sys.stderr)
        return 2

    if args.check:
        stale = []
        for path, content in results.items():
            if not path.is_file():
                stale.append(f"  {rel(path)}: 尚未生成")
            elif path.read_text(encoding="utf-8") != content:
                stale.append(f"  {rel(path)}: 内容与代码不一致")
        if stale:
            print("[FAIL] 检索层索引已过期")
            print("\n".join(stale))
            print("  修正: 运行 ./scripts/gen_doc_index.py 重新生成")
            return 1
        print(f"[PASS] 检索层索引与代码一致（{len(results)} 个文件）")
        return 0

    AI_DIR.mkdir(parents=True, exist_ok=True)
    for path, content in results.items():
        path.write_text(content, encoding="utf-8")
        print(f"生成: {rel(path)}  {len(content.splitlines())} 行")
    return 0


if __name__ == "__main__":
    sys.exit(main())
