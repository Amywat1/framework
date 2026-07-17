#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
构建期工具：校验方案 JSON 语义（act / intent 模型）。

用法:
    python3 tools/validate_program.py <program.json> \\
        [--io-catalog adapters/machine/m8_engine_io.c] \\
        [--actuator-catalog adapters/machine/m8_engine_actuator.c]

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


def extract_io_names(catalog_path: str) -> tuple[set[str], set[str]]:
    """从 m8_engine_io.c 提取 signal / axis 名。"""
    with open(catalog_path, "r", encoding="utf-8") as fp:
        text = fp.read()

    signals: set[str] = set()
    axes: set[str] = set()
    in_signal = False
    in_axis = False

    for line in text.splitlines():
        if "s_signal_table[]" in line:
            in_signal = True
            in_axis = False
            continue
        if "s_axis_table[]" in line:
            in_axis = True
            in_signal = False
            continue
        if line.strip().startswith("};"):
            in_signal = in_axis = False
            continue

        m = re.search(r'\{\s*"([A-Za-z0-9_]+)"', line)
        if m is None:
            continue
        name = m.group(1)
        if in_signal:
            signals.add(name)
        elif in_axis:
            axes.add(name)

    return signals, axes


def extract_actuator_names(catalog_path: str) -> tuple[set[str], set[str]]:
    """从 m8_engine_actuator.c 提取 resource / water_path 名。"""
    with open(catalog_path, "r", encoding="utf-8") as fp:
        text = fp.read()

    resources: set[str] = set()
    water_paths: set[str] = set()

    m_res = re.search(
        r"s_resources\s*\[\s*\]\s*=\s*\{(.*?)\};",
        text,
        re.S,
    )
    if m_res:
        resources.update(re.findall(r'"([a-z0-9_]+)"', m_res.group(1)))

    m_wp = re.search(
        r"s_water_paths\s*\[\s*\]\s*=\s*\{(.*?)\};",
        text,
        re.S,
    )
    if m_wp:
        water_paths.update(re.findall(r'"([a-z0-9_]+)"', m_wp.group(1)))

    return resources, water_paths


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
            if k in (
                "condition",
                "entry_guard",
                "exit_guard",
                "guard",
                "expr",
                "reset_condition",
                "active_while",
            ) and isinstance(v, str):
                out.update(collect_expr_vars(v))
            else:
                walk_exprs(v, out)
    elif isinstance(node, list):
        for item in node:
            walk_exprs(item, out)


def phase_step_ids(phase: dict[str, Any]) -> set[str]:
    ids: set[str] = set()
    for lane in phase.get("lanes", []) or []:
        for step in lane.get("steps", []) or []:
            sid = step.get("id")
            if sid:
                ids.add(sid)
    return ids


def validate_intent(
    intent: Any,
    ctx: str,
    resources: set[str] | None,
    water_paths: set[str] | None,
    errors: list[str],
) -> None:
    if not isinstance(intent, dict):
        errors.append(f"{ctx} intent/act 应为对象")
        return

    resource = intent.get("resource")
    cmd = intent.get("cmd")
    if not resource:
        errors.append(f"{ctx} 意图缺少 resource")
    elif resources is not None and resource not in resources:
        errors.append(f"{ctx} 未知执行机构资源: {resource}")

    if not cmd:
        errors.append(f"{ctx} 意图缺少 cmd")

    paths = intent.get("paths")
    if paths is None:
        return
    if not isinstance(paths, list):
        errors.append(f"{ctx} paths 应为数组")
        return
    for p in paths:
        if not isinstance(p, str) or not p:
            errors.append(f"{ctx} paths 项须为非空字符串")
            continue
        if water_paths is not None and p not in water_paths:
            errors.append(f"{ctx} 未知水路路径: {p}")


def validate_actions(
    actions: Any,
    ctx: str,
    resources: set[str] | None,
    water_paths: set[str] | None,
    errors: list[str],
) -> None:
    if actions is None:
        return
    if not isinstance(actions, list):
        errors.append(f"{ctx} 动作列表应为数组")
        return
    for i, item in enumerate(actions):
        if not isinstance(item, dict):
            errors.append(f"{ctx}[{i}] 动作项应为对象")
            continue
        if "act" in item:
            validate_intent(item["act"], f"{ctx}[{i}]/act", resources, water_paths, errors)
        elif "wait_time" in item:
            ms = (item.get("wait_time") or {}).get("ms")
            if ms is None or int(ms) < 0:
                errors.append(f"{ctx}[{i}] wait_time.ms 非法")
        elif "io_set" in item:
            errors.append(f"{ctx}[{i}] 已废弃 io_set，请改用 act")
        else:
            errors.append(f"{ctx}[{i}] 不支持的动作原语（仅 act/wait_time）")


def validate_program(
    prog: dict[str, Any],
    signals: set[str] | None,
    axes_catalog: set[str] | None,
    resources: set[str] | None,
    water_paths: set[str] | None,
) -> list[str]:
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

        for keep in ph.get("keep") or []:
            if resources is not None and keep not in resources:
                errors.append(f"{pid}/keep 未知资源: {keep}")

        validate_actions(ph.get("on_enter"), f"{pid}/on_enter", resources, water_paths, errors)
        validate_actions(ph.get("on_exit"), f"{pid}/on_exit", resources, water_paths, errors)

        step_ids = phase_step_ids(ph)
        for lane in ph.get("lanes", []) or []:
            for step in lane.get("steps", []) or []:
                sid = step.get("id") or "?"
                stype = step.get("type")
                if stype not in ("event", "control"):
                    errors.append(f"仅支持 event/control 步骤: {pid}/{sid} type={stype}")
                    continue

                for dep in step.get("after") or []:
                    if dep not in step_ids:
                        errors.append(f"after 引用未知步骤: {pid}/{sid} -> {dep}")

                if stype == "control":
                    if not step.get("active_while"):
                        errors.append(f"control 缺少 active_while: {pid}/{sid}")
                    if "intent" not in step:
                        errors.append(f"control 缺少 intent: {pid}/{sid}")
                    else:
                        validate_intent(
                            step.get("intent"),
                            f"{pid}/{sid}/intent",
                            resources,
                            water_paths,
                            errors,
                        )
                    if step.get("output") is not None or step.get("value_expr") is not None:
                        errors.append(f"{pid}/{sid} 已废弃 output/value_expr，请改用 intent")
                else:
                    validate_actions(
                        step.get("actions"),
                        f"{pid}/{sid}/actions",
                        resources,
                        water_paths,
                        errors,
                    )
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

    interlocks = root.get("interlocks") or []
    if not any((ilk.get("id") == "estop") for ilk in interlocks):
        errors.append("缺少 estop 联锁")
    for ilk in interlocks:
        validate_actions(
            ilk.get("actions"),
            f"interlock/{ilk.get('id')}",
            resources,
            water_paths,
            errors,
        )

    prog_axes = set((root.get("axes") or {}).keys())
    for mid, mk in (root.get("markers") or {}).items():
        axis = mk.get("axis")
        if axis and axis not in prog_axes:
            errors.append(f"标记 {mid} 引用未知轴: {axis}")
        if axes_catalog is not None and axis and axis not in axes_catalog:
            errors.append(f"标记 {mid} 轴不在 IO catalog: {axis}")
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

    return errors


def parse_args(argv: list[str]) -> tuple[str, str | None, str | None]:
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        sys.stderr.write(
            "用法: validate_program.py <program.json> "
            "[--io-catalog m8_engine_io.c] "
            "[--actuator-catalog m8_engine_actuator.c]\n"
        )
        raise SystemExit(2)

    json_path = argv[1]
    io_catalog = None
    act_catalog = None
    i = 2
    while i < len(argv):
        if argv[i] == "--io-catalog":
            if i + 1 >= len(argv):
                sys.stderr.write("缺少 --io-catalog 参数值\n")
                raise SystemExit(2)
            io_catalog = argv[i + 1]
            i += 2
            continue
        if argv[i] == "--actuator-catalog":
            if i + 1 >= len(argv):
                sys.stderr.write("缺少 --actuator-catalog 参数值\n")
                raise SystemExit(2)
            act_catalog = argv[i + 1]
            i += 2
            continue
        sys.stderr.write(f"未知参数: {argv[i]}\n")
        raise SystemExit(2)
    return json_path, io_catalog, act_catalog


def main() -> int:
    json_path, io_catalog_path, act_catalog_path = parse_args(sys.argv)

    try:
        doc = load_json(json_path)
    except (OSError, json.JSONDecodeError) as exc:
        sys.stderr.write(f"读取 JSON 失败: {exc}\n")
        return 1

    signals = axes = None
    if io_catalog_path:
        signals, axes = extract_io_names(io_catalog_path)

    resources = water_paths = None
    if act_catalog_path:
        resources, water_paths = extract_actuator_names(act_catalog_path)

    errors = validate_program(doc, signals, axes, resources, water_paths)
    if errors:
        sys.stderr.write(f"方案校验失败 ({json_path}):\n")
        for err in errors:
            sys.stderr.write(f"  - {err}\n")
        return 1

    sys.stdout.write(f"方案校验通过: {json_path}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
