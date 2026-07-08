#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
构建期工具：校验洗车方案 JSON 语义。

用法：
    python3 tools/validate_program.py <program.json> [--io-catalog engine_io_m8.c]

退出码：0=通过，非 0=失败。
"""

from __future__ import annotations

import json
import re
import sys
from typing import Any


def load_json(path: str) -> dict[str, Any]:
    with open(path, "r", encoding="utf-8") as fp:
        return json.load(fp)


def extract_io_names(catalog_path: str) -> tuple[set[str], set[str], set[str]]:
    with open(catalog_path, "r", encoding="utf-8") as fp:
        text = fp.read()

    signals: set[str] = set()
    outputs: set[str] = set()
    axes: set[str] = set()

    in_signal = False
    in_axis = False
    in_output = False

    for line in text.splitlines():
        if "s_signal_table[]" in line:
            in_signal = True
            in_axis = False
            in_output = False
            continue
        if "s_axis_table[]" in line:
            in_axis = True
            in_signal = False
            in_output = False
            continue
        if "s_output_table[]" in line:
            in_output = True
            in_signal = False
            in_axis = False
            continue
        if line.strip().startswith("};"):
            in_signal = in_axis = in_output = False
            continue

        m = re.search(r'\{\s*"([A-Z0-9_]+)"', line)
        if m is None:
            m = re.search(r'\{\s*"([a-z0-9_]+)"', line)
        if m is None:
            continue

        name = m.group(1)
        if in_signal:
            signals.add(name)
        elif in_axis:
            axes.add(name)
        elif in_output:
            outputs.add(name)

    return signals, outputs, axes


VAR_RE = re.compile(
    r"(?:"
    r"\$[A-Za-z_][A-Za-z0-9_]*|"
    r"axes\.[A-Za-z_][A-Za-z0-9_]*\.(?:position|valid|speed)|"
    r"markers\.[A-Za-z_][A-Za-z0-9_]*\.(?:position|valid)|"
    r"phase\.(?:elapsed_ms|direction)|"
    r"[A-Z][A-Z0-9_]*"
    r")"
)


KEYWORDS = {"AND", "OR", "NOT", "true", "false"}


def collect_expr_vars(expr: str) -> set[str]:
    if not expr:
        return set()
    return {v for v in VAR_RE.findall(expr) if v not in KEYWORDS}


def walk_exprs(node: Any, out: set[str]) -> None:
    if isinstance(node, dict):
        for k, v in node.items():
            if k in ("condition", "entry_guard", "exit_guard", "guard", "expr",
                     "reset_condition") and isinstance(v, str):
                out.update(collect_expr_vars(v))
            else:
                walk_exprs(v, out)
    elif isinstance(node, list):
        for item in node:
            walk_exprs(item, out)


def phase_step_ids(phase: dict[str, Any]) -> set[str]:
    ids: set[str] = set()
    for lane in phase.get("lanes", []):
        for step in lane.get("steps", []):
            sid = step.get("id")
            if sid:
                ids.add(sid)
    return ids


def validate_program(prog: dict[str, Any], signals: set[str] | None,
                     outputs: set[str] | None, axes_catalog: set[str] | None) -> list[str]:
    errors: list[str] = []
    root = prog.get("program")
    if not isinstance(root, dict):
        return ["缺少顶层 program"]

    phases = root.get("phases")
    if not isinstance(phases, list) or not phases:
        errors.append("缺少 phases")
        return errors

    phase_ids: set[str] = set()
    for ph in phases:
        pid = ph.get("id")
        if not pid:
            errors.append("阶段缺少 id")
            continue
        if pid in phase_ids:
            errors.append(f"阶段 id 重复: {pid}")
        phase_ids.add(pid)

        if not ph.get("exit_guard"):
            errors.append(f"阶段缺少 exit_guard: {pid}")
        timeout = ph.get("timeout_ms")
        if timeout is None or int(timeout) <= 0:
            errors.append(f"阶段 timeout_ms 非法: {pid}")

        step_ids = phase_step_ids(ph)
        for lane in ph.get("lanes", []):
            for step in lane.get("steps", []):
                stype = step.get("type")
                if stype not in ("event", "control"):
                    errors.append(
                        f"仅支持 event/control 步骤: {pid}/{step.get('id')} type={stype}"
                    )
                for dep in step.get("after", []):
                    if dep not in step_ids:
                        errors.append(
                            f"after 引用未知步骤: {pid}/{step.get('id')} -> {dep}"
                        )

    interlocks = root.get("interlocks", [])
    if not any((ilk.get("id") == "estop") for ilk in interlocks):
        errors.append("缺少 estop 联锁")

    prog_axes = set((root.get("axes") or {}).keys())
    for mid, mk in (root.get("markers") or {}).items():
        axis = mk.get("axis")
        if axis and axis not in prog_axes:
            errors.append(f"标记 {mid} 引用未知轴: {axis}")
        sig = (mk.get("on") or {}).get("signal")
        if sig and signals is not None and sig not in signals:
            errors.append(f"标记 {mid} 引用未知信号: {sig}")

    expr_vars: set[str] = set()
    walk_exprs(root, expr_vars)

    params = set((root.get("params") or {}).keys())
    markers = set((root.get("markers") or {}).keys())

    for var in sorted(expr_vars):
        if var.startswith("$"):
            if var[1:] not in params:
                errors.append(f"未知参数: {var}")
            continue
        if var.startswith("axes."):
            parts = var.split(".")
            if len(parts) >= 2 and parts[1] not in prog_axes:
                errors.append(f"未知坐标轴: {var}")
            continue
        if var.startswith("markers."):
            parts = var.split(".")
            if len(parts) >= 2 and parts[1] not in markers:
                errors.append(f"未知标记: {var}")
            continue
        if var.startswith("phase."):
            continue
        if signals is not None and var not in signals:
            errors.append(f"未知 DI 信号: {var}")

    def check_actions(actions: list[Any], ctx: str) -> None:
        for act in actions or []:
            if "io_set" in act:
                ch = act["io_set"].get("channel")
                if ch and outputs is not None and ch not in outputs:
                    errors.append(f"{ctx} 未知 DO: {ch}")

    for ph in phases:
        check_actions(ph.get("on_enter"), f"{ph.get('id')}/on_enter")
        check_actions(ph.get("on_exit"), f"{ph.get('id')}/on_exit")
        for lane in ph.get("lanes", []):
            for step in lane.get("steps", []):
                stype = step.get("type")
                if stype == "control":
                    aw = step.get("active_while")
                    ve = step.get("value_expr")
                    out = step.get("output")
                    if not aw:
                        errors.append(f"control 缺少 active_while: {ph.get('id')}/{step.get('id')}")
                    if not ve:
                        errors.append(f"control 缺少 value_expr: {ph.get('id')}/{step.get('id')}")
                    if not out:
                        errors.append(f"control 缺少 output: {ph.get('id')}/{step.get('id')}")
                    elif outputs is not None and out not in outputs:
                        errors.append(f"{ph.get('id')}/{step.get('id')} 未知 DO: {out}")
                    if isinstance(aw, str):
                        walk_exprs({"expr": aw}, expr_vars)
                    if isinstance(ve, str):
                        walk_exprs({"expr": ve}, expr_vars)
                else:
                    check_actions(step.get("actions"), f"{ph.get('id')}/{step.get('id')}")
                    trig = step.get("trigger") or {}
                    if trig.get("type") == "signal":
                        sig = trig.get("signal")
                        if sig and signals is not None and sig not in signals:
                            errors.append(f"未知触发信号: {sig}")
                    done = step.get("done") or {}
                    if done.get("type") == "signal":
                        sig = done.get("signal")
                        if sig and signals is not None and sig not in signals:
                            errors.append(f"未知 done 信号: {sig}")

    for ilk in interlocks:
        check_actions(ilk.get("actions"), f"interlock/{ilk.get('id')}")

    return errors


def main() -> int:
    if len(sys.argv) < 2:
        sys.stderr.write(
            "用法: validate_program.py <program.json> [--io-catalog engine_io_m8.c]\n"
        )
        return 2

    json_path = sys.argv[1]
    catalog_path = None
    if "--io-catalog" in sys.argv:
        idx = sys.argv.index("--io-catalog")
        if idx + 1 >= len(sys.argv):
            sys.stderr.write("缺少 --io-catalog 参数值\n")
            return 2
        catalog_path = sys.argv[idx + 1]

    try:
        doc = load_json(json_path)
    except (OSError, json.JSONDecodeError) as exc:
        sys.stderr.write(f"读取 JSON 失败: {exc}\n")
        return 1

    signals = outputs = axes = None
    if catalog_path:
        signals, outputs, axes = extract_io_names(catalog_path)

    errors = validate_program(doc, signals, outputs, axes)
    if errors:
        sys.stderr.write(f"方案校验失败 ({json_path}):\n")
        for err in errors:
            sys.stderr.write(f"  - {err}\n")
        return 1

    sys.stdout.write(f"方案校验通过: {json_path}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
