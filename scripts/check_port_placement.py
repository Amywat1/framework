#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""端口放置规则：假 port、outbound .c 用途、子目录白名单。

由 check_arch_boundary.sh 调用。也可单独运行：

    python3 scripts/check_port_placement.py --root .
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
from pathlib import Path

# ---------------------------------------------------------------------------
# R25: domain/ports/outbound 下每个 .c 必须登记用途
#
# register_stub: 单例 ops 注册 / 转发，实现可在本文件或 runtime/ports
# helper:        与邻近 port 同目录的便利层或句柄分派，不是领域内部 API
# ---------------------------------------------------------------------------
OUTBOUND_C_REGISTERED = {
    "domain/ports/outbound/storage/engine_program_loader_port.c": (
        "register_stub",
        "单例 loader ops 注册与转发",
    ),
    "domain/ports/outbound/program_engine/engine_environment_port.c": (
        "helper",
        "引擎环境不透明句柄分派，与 port 头同目录",
    ),
    "domain/ports/outbound/storage/param_kv.c": (
        "helper",
        "参数 KV 便利层，与 param_store 同目录",
    ),
}
ALLOWED_C_PURPOSES = ("register_stub", "helper")

# ---------------------------------------------------------------------------
# R26: 端口子目录白名单（能力域，不是「今天谁 include」）
# ---------------------------------------------------------------------------
DOMAIN_OUTBOUND_SUBDIRS = (
    "device",
    "hal",
    "motor",
    "program_engine",
    "safety",
    "storage",
)

# 相对 application/ports/ 的允许前缀；其后可再嵌套。
APPLICATION_PORTS_PREFIXES = (
    "inbound/command",
    "inbound/safety",
    "outbound/cloud",
)

CONTROL_KEYWORDS = frozenset(
    {
        "if",
        "for",
        "while",
        "switch",
        "sizeof",
        "return",
        "_Static_assert",
        "static_assert",
        "defined",
        "elif",
        "alignas",
        "_Alignas",
        "__attribute__",
        "__attribute",
        "typeof",
        "__typeof__",
        "_Generic",
        "_Pragma",
    }
)


def strip_c_comments(src: str) -> str:
    """去掉 // 与 /* */ 注释，字符串内容清空以免干扰扫描。"""
    out: list[str] = []
    i = 0
    n = len(src)
    while i < n:
        if src[i : i + 2] == "//":
            i += 2
            while i < n and src[i] != "\n":
                i += 1
            continue
        if src[i : i + 2] == "/*":
            i += 2
            while i + 1 < n and src[i : i + 2] != "*/":
                if src[i] == "\n":
                    out.append("\n")
                i += 1
            i = min(i + 2, n)
            continue
        if src[i] in "\"'":
            quote = src[i]
            out.append(" ")
            i += 1
            while i < n:
                if src[i] == "\\":
                    i += 2
                    continue
                if src[i] == quote:
                    i += 1
                    break
                if src[i] == "\n":
                    out.append("\n")
                i += 1
            continue
        out.append(src[i])
        i += 1
    return "".join(out)


def drop_preprocessor(src: str) -> str:
    """删除预处理行，保留换行以免后续扫描错位。"""
    lines = []
    for line in src.splitlines(keepends=True):
        stripped = line.lstrip()
        if stripped.startswith("#"):
            lines.append("\n" if line.endswith("\n") else "")
        else:
            lines.append(line)
    return "".join(lines)


def skip_ws(text: str, i: int) -> int:
    n = len(text)
    while i < n and text[i] in " \t\n\r":
        i += 1
    return i


def skip_ws_and_attrs(text: str, i: int) -> int:
    n = len(text)
    while True:
        i = skip_ws(text, i)
        if text.startswith("__attribute__", i) or text.startswith("__attribute", i):
            j = text.find("(", i)
            if j < 0:
                return i
            i = match_paren(text, j) + 1
            continue
        return i


def match_paren(text: str, open_idx: int) -> int:
    """返回与 open_idx 处 '(' 匹配的 ')' 下标；失败返回 -1。"""
    depth = 0
    i = open_idx
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def ident_before_paren(text: str, paren_idx: int) -> tuple[str | None, int | None]:
    j = paren_idx - 1
    while j >= 0 and text[j] in " \t\n\r":
        j -= 1
    if j < 0 or not (text[j].isalnum() or text[j] == "_"):
        return None, None
    end = j + 1
    while j >= 0 and (text[j].isalnum() or text[j] == "_"):
        j -= 1
    start = j + 1
    name = text[start:end]
    if not name or name[0].isdigit():
        return None, None
    return name, start


def is_func_ptr_name(text: str, ident_start: int) -> bool:
    """`(*name)(` 形式的函数指针 typedef / 成员。"""
    j = ident_start - 1
    while j >= 0 and text[j] in " \t\n\r":
        j -= 1
    if j < 0 or text[j] != "*":
        return False
    j -= 1
    while j >= 0 and text[j] in " \t\n\r":
        j -= 1
    return j >= 0 and text[j] == "("


def is_extern_c_brace(text: str, brace_idx: int) -> bool:
    """`extern "C" {` 的花括号不计入结构体深度。注释/字符串已剥掉，引号变空格。"""
    j = brace_idx - 1
    while j >= 0 and text[j] in " \t\n\r":
        j -= 1
    # 原文 `"C"` 被清空为空白，前面仍是 extern
    while j >= 0 and text[j] in " \t\n\r":
        j -= 1
    token_end = j + 1
    while j >= 0 and (text[j].isalnum() or text[j] == "_"):
        j -= 1
    return text[j + 1 : token_end] == "extern"


def file_scope_functions(src: str) -> list[tuple[str, str]]:
    """返回文件作用域函数列表：(名字, 'decl'|'def')。"""
    text = drop_preprocessor(strip_c_comments(src))
    results: list[tuple[str, str]] = []
    depth = 0
    extern_c = 0
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "{":
            if depth == 0 and is_extern_c_brace(text, i):
                extern_c += 1
            else:
                depth += 1
            i += 1
            continue
        if ch == "}":
            if depth > 0:
                depth -= 1
            elif extern_c > 0:
                extern_c -= 1
            i += 1
            continue
        if ch == "(" and depth == 0:
            name, start = ident_before_paren(text, i)
            close = match_paren(text, i)
            if name and start is not None and close > i:
                if (
                    name not in CONTROL_KEYWORDS
                    and not is_func_ptr_name(text, start)
                ):
                    k = skip_ws_and_attrs(text, close + 1)
                    if k < n and text[k] == ";":
                        results.append((name, "decl"))
                    elif k < n and text[k] == "{":
                        results.append((name, "def"))
            i = close + 1 if close > i else i + 1
            continue
        i += 1
    return results


def posix_rel(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def iter_sources(root: Path, rel_dir: str, suffixes: tuple[str, ...]) -> list[Path]:
    base = root / rel_dir
    if not base.is_dir():
        return []
    out: list[Path] = []
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = [d for d in dirnames if d != "."]
        for name in filenames:
            if name.endswith(suffixes):
                out.append(Path(dirpath) / name)
    out.sort()
    return out


def find_fake_ports(root: Path) -> list[str]:
    """R24：outbound 头里的普通函数不得在 domain 锥、端口树外定义。

    定义落在 domain/ports/outbound 或完全不在 domain（例如 runtime/ports）
    都合法。函数指针成员、static inline、typedef 回调类型不计入。
    """
    outbound = root / "domain/ports/outbound"
    if not outbound.is_dir():
        return [f"  domain/ports/outbound: 目录不存在"]

    decls: dict[str, list[str]] = {}
    for hdr in iter_sources(root, "domain/ports/outbound", (".h",)):
        rel = posix_rel(root, hdr)
        for name, kind in file_scope_functions(hdr.read_text(encoding="utf-8", errors="replace")):
            if kind == "decl":
                decls.setdefault(name, []).append(rel)

    if not decls:
        return []

    defs_outside: dict[str, list[str]] = {}
    for src in iter_sources(root, "domain", (".c", ".h")):
        rel = posix_rel(root, src)
        if rel.startswith("domain/ports/outbound/"):
            continue
        text = src.read_text(encoding="utf-8", errors="replace")
        for name, kind in file_scope_functions(text):
            if kind == "def" and name in decls:
                defs_outside.setdefault(name, []).append(rel)

    violations: list[str] = []
    for name in sorted(defs_outside):
        hdrs = ", ".join(decls[name])
        for def_path in defs_outside[name]:
            violations.append(
                f"  {def_path}: `{name}` 在 {hdrs} 声明，定义却在 "
                f"domain/ports/outbound 之外；领域内部 API 不得挂在出站端口树"
            )
    return violations


def check_outbound_c(root: Path) -> list[str]:
    """R25：outbound 下每个 .c 必须在用途表中，表项必须真实存在。"""
    violations: list[str] = []
    files = iter_sources(root, "domain/ports/outbound", (".c",))
    seen = {posix_rel(root, p) for p in files}

    for rel, (purpose, _reason) in OUTBOUND_C_REGISTERED.items():
        if purpose not in ALLOWED_C_PURPOSES:
            violations.append(
                f"  {rel}: 用途 `{purpose}` 非法，只允许 "
                + " / ".join(ALLOWED_C_PURPOSES)
            )
        if rel not in seen:
            violations.append(f"  {rel}: 已登记但文件不存在")

    for rel in sorted(seen):
        if rel not in OUTBOUND_C_REGISTERED:
            violations.append(
                f"  {rel}: 未登记用途；在 check_port_placement.py 的 "
                f"OUTBOUND_C_REGISTERED 中登记为 register_stub 或 helper，并写明理由"
            )
    return violations


def check_port_subdirs(root: Path) -> list[str]:
    """R26：端口源文件必须落在已登记的能力域子目录。"""
    violations: list[str] = []

    for name in DOMAIN_OUTBOUND_SUBDIRS:
        path = root / "domain/ports/outbound" / name
        if not path.is_dir():
            violations.append(
                f"  domain/ports/outbound/{name}/: 白名单子目录不存在，"
                f"请同步 OUTBOUND 子目录表"
            )

    for src in iter_sources(root, "domain/ports/outbound", (".c", ".h")):
        rel = posix_rel(root, src)
        rest = rel[len("domain/ports/outbound/") :]
        parts = rest.split("/")
        if len(parts) < 2:
            violations.append(
                f"  {rel}: 出站契约必须放在已登记的能力域子目录下，"
                f"不得直接落在 domain/ports/outbound/"
            )
            continue
        if parts[0] not in DOMAIN_OUTBOUND_SUBDIRS:
            violations.append(
                f"  {rel}: 子目录 `{parts[0]}` 不在允许清单 "
                f"({' / '.join(DOMAIN_OUTBOUND_SUBDIRS)})；新能力域须先登记"
            )

    for prefix in APPLICATION_PORTS_PREFIXES:
        path = root / "application/ports" / prefix
        if not path.is_dir():
            violations.append(
                f"  application/ports/{prefix}/: 白名单前缀目录不存在，"
                f"请同步 APPLICATION_PORTS_PREFIXES"
            )

    app_ports = root / "application/ports"
    if app_ports.is_dir():
        for src in iter_sources(root, "application/ports", (".c", ".h")):
            rel = posix_rel(root, src)
            rest = rel[len("application/ports/") :]
            if not any(
                rest == p or rest.startswith(p + "/")
                for p in APPLICATION_PORTS_PREFIXES
            ):
                violations.append(
                    f"  {rel}: 不在允许前缀 "
                    f"({' / '.join(APPLICATION_PORTS_PREFIXES)})；"
                    f"入站用例走 inbound/<用例>，云传输走 outbound/cloud"
                )
    return violations


def _write_tree(base: Path, files: dict[str, str]) -> None:
    for rel, content in files.items():
        path = base / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")


def self_test() -> list[str]:
    """注入违规，确认规则会失败；再注入合法布局，确认规则会通过。"""
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="wdf_port_place_") as tmp:
        root = Path(tmp)

        # --- R24 必须抓住：出站头声明 + domain 内部定义 ---
        _write_tree(
            root,
            {
                "domain/ports/outbound/motor/exec_port.h": (
                    "typedef int motor_cmd_result_t;\n"
                    "motor_cmd_result_t motor_exec_run(void);\n"
                ),
                "domain/mechanism/motor/exec.c": (
                    "motor_cmd_result_t motor_exec_run(void) { return 0; }\n"
                ),
            },
        )
        hits = find_fake_ports(root)
        if not any("motor_exec_run" in h for h in hits):
            failures.append(
                "自检失败: 注入 `motor_exec_run` 声明于 outbound、"
                "定义于 domain/mechanism 未被 R24 捕获"
            )

        # --- R24 不得误报：注册函数定义在 runtime/ports ---
        root2 = Path(tmp) / "ok_runtime"
        _write_tree(
            root2,
            {
                "domain/ports/outbound/safety/safety_port.h": (
                    "typedef int sw_err_t;\n"
                    "sw_err_t safety_cutout_execute(void);\n"
                ),
                "runtime/ports/port_registry_safety.c": (
                    "sw_err_t safety_cutout_execute(void) { return 0; }\n"
                ),
            },
        )
        if find_fake_ports(root2):
            failures.append(
                "自检失败: `safety_cutout_execute` 定义在 runtime/ports，"
                "R24 不应报假 port"
            )

        # --- R24 不得误报：helper 定义就在 outbound ---
        root3 = Path(tmp) / "ok_helper"
        _write_tree(
            root3,
            {
                "domain/ports/outbound/storage/param_kv.h": (
                    "typedef int sw_err_t;\n"
                    "sw_err_t param_kv_init(void);\n"
                ),
                "domain/ports/outbound/storage/param_kv.c": (
                    "sw_err_t param_kv_init(void) { return 0; }\n"
                ),
            },
        )
        if find_fake_ports(root3):
            failures.append(
                "自检失败: `param_kv_init` 定义在 outbound 同目录，R24 不应报"
            )

        # --- R24 不得把函数指针成员当成普通函数 ---
        root4 = Path(tmp) / "ok_ops"
        _write_tree(
            root4,
            {
                "domain/ports/outbound/hal/hal_io_port.h": (
                    "typedef struct {\n"
                    "    int (*write)(int pin);\n"
                    "} hal_io_ops_t;\n"
                    "typedef int sw_err_t;\n"
                    "sw_err_t hal_io_register(const hal_io_ops_t *ops);\n"
                ),
                "domain/mechanism/motor/unrelated.c": (
                    "int write(int pin) { return pin; }\n"
                ),
            },
        )
        hits = find_fake_ports(root4)
        if any("`write`" in h for h in hits):
            failures.append(
                "自检失败: ops 表里的函数指针 `write` 被当成了出站普通函数"
            )

        # --- R24 不得把 static inline 当成假 port ---
        root5 = Path(tmp) / "ok_inline"
        _write_tree(
            root5,
            {
                "domain/ports/outbound/motor/motor_types.h": (
                    "static inline int motor_dir_is_motion(int dir) { return dir != 0; }\n"
                ),
                "domain/mechanism/motor/x.c": "int dummy(void) { return 0; }\n",
            },
        )
        if find_fake_ports(root5):
            failures.append("自检失败: outbound 头里的 static inline 被 R24 误报")

        # --- R25 必须抓住未登记 .c ---
        root6 = Path(tmp) / "bad_c"
        _write_tree(
            root6,
            {
                "domain/ports/outbound/storage/sneaky.c": "void x(void) {}\n",
            },
        )
        hits = check_outbound_c(root6)
        if not any("sneaky.c" in h for h in hits):
            failures.append("自检失败: 未登记的 outbound .c 未被 R25 捕获")

        # --- R26 必须抓住未登记子目录 ---
        root7 = Path(tmp) / "bad_subdir"
        for name in DOMAIN_OUTBOUND_SUBDIRS:
            (root7 / "domain/ports/outbound" / name).mkdir(parents=True, exist_ok=True)
        for prefix in APPLICATION_PORTS_PREFIXES:
            (root7 / "application/ports" / prefix).mkdir(parents=True, exist_ok=True)
        _write_tree(
            root7,
            {
                "domain/ports/outbound/telemetry/foo.h": "void foo(void);\n",
            },
        )
        hits = check_port_subdirs(root7)
        if not any("telemetry" in h for h in hits):
            failures.append("自检失败: 未登记的 outbound 子目录未被 R26 捕获")

    return failures


def format_block(rule_id: str, title: str, violations: list[str]) -> tuple[bool, str]:
    if not violations:
        return True, f"[PASS] {rule_id}: {title}\n"
    body = "".join(v if v.endswith("\n") else v + "\n" for v in violations)
    return False, f"\n[FAIL] {rule_id}: {title}\n{body}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="端口放置规则检查")
    parser.add_argument("--root", default=".", help="框架根目录")
    parser.add_argument(
        "--skip-self-test",
        action="store_true",
        help="跳过注入自检（仅调试用）",
    )
    args = parser.parse_args(argv)

    root = Path(args.root).resolve()
    if not (root / "domain").is_dir() or not (root / "common").is_dir():
        print(f"错误: '{root}' 不是有效的 framework 根目录", file=sys.stderr)
        return 2

    failed = 0
    r24_self_ok = True

    if not args.skip_self_test:
        st = self_test()
        if st:
            print("[FAIL] R24: 端口放置规则自检未通过（规则本身失效）")
            for line in st:
                print(f"  {line}")
            failed += 1
            r24_self_ok = False

    r24 = find_fake_ports(root)
    if r24_self_ok:
        ok, text = format_block(
            "R24",
            "出站头中的普通函数不得在 domain 锥内、端口树外定义",
            r24,
        )
        sys.stdout.write(text)
        if not ok:
            print("  修正: 领域内部 API 迁出 domain/ports/outbound；")
            print("        真 port 的实现放 adapters 或 runtime/ports 注册桩。")
            failed += 1
    elif r24:
        print("  （真实树仍有假 port，附列于自检失败之后）")
        for line in r24:
            print(line if line.endswith("\n") else line)

    r25 = check_outbound_c(root)
    n_c = len(list(iter_sources(root, "domain/ports/outbound", (".c",))))
    title25 = (
        f"domain/ports/outbound 下每个 .c 已登记用途（{n_c} 个）"
        if not r25
        else "domain/ports/outbound 下存在未登记或失效的 .c"
    )
    ok, text = format_block("R25", title25, r25)
    sys.stdout.write(text)
    if not ok:
        print("  修正: 在 scripts/check_port_placement.py 的 OUTBOUND_C_REGISTERED")
        print("        登记，用途仅限 register_stub / helper。")
        failed += 1

    r26 = check_port_subdirs(root)
    ok, text = format_block(
        "R26",
        "端口源文件均落在已登记的能力域子目录",
        r26,
    )
    sys.stdout.write(text)
    if not ok:
        print("  修正: 新能力域加入 check_port_placement.py 的")
        print("        DOMAIN_OUTBOUND_SUBDIRS 或 APPLICATION_PORTS_PREFIXES。")
        failed += 1

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
