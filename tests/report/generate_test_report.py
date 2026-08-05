#!/usr/bin/env python3
"""从 CTest JUnit 和门禁记录生成 Framework 单次测试报告。"""

from __future__ import annotations

import argparse
import html
import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Iterable
from xml.etree import ElementTree


UNITY_LINE = re.compile(
    r"^(?P<file>.+):(?P<line>\d+):(?P<name>[A-Za-z_][A-Za-z0-9_]*):"
    r"(?P<result>PASS|FAIL|IGNORE)(?::(?P<detail>.*))?$"
)
SPEC_LINE = re.compile(
    r"^WDFSPEC\t(?P<behaviour>[^\t]*)\t(?P<summary>[^\t]*)\t"
    r"(?P<name>[^\t]+)\t(?P<result>passed|failed|skipped)$"
)


@dataclass
class CaseResult:
    name: str
    status: str
    module: str
    file: str = ""
    line: str = ""
    detail: str = ""
    behaviour: str = ""
    summary: str = ""


@dataclass
class TargetResult:
    name: str
    status: str
    duration: float
    output: str
    cases: list[CaseResult] = field(default_factory=list)


def status_of(testcase: ElementTree.Element) -> str:
    if testcase.find("failure") is not None or testcase.find("error") is not None:
        return "failed"
    if testcase.find("skipped") is not None:
        return "skipped"
    return "passed"


def module_from_file(file_name: str) -> str:
    normalized = file_name.replace("\\", "/")
    marker = "/tests/"
    if marker in normalized:
        relative = normalized.split(marker, 1)[1]
        return relative.split("/", 1)[0]
    return "未分类"


def parse_output(output: str, target_status: str) -> list[CaseResult]:
    cases: list[CaseResult] = []
    specs: dict[str, tuple[str, str, str]] = {}
    for line in output.splitlines():
        match = UNITY_LINE.match(line.strip())
        if match:
            result = {"PASS": "passed", "FAIL": "failed", "IGNORE": "skipped"}[
                match.group("result")
            ]
            cases.append(
                CaseResult(
                    name=match.group("name"),
                    status=result,
                    module=module_from_file(match.group("file")),
                    file=match.group("file"),
                    line=match.group("line"),
                    detail=(match.group("detail") or "").strip(),
                )
            )
            continue
        match = SPEC_LINE.match(line.strip())
        if match:
            specs[match.group("name")] = (
                match.group("behaviour"),
                match.group("summary"),
                match.group("result"),
            )

    for case in cases:
        if case.name in specs:
            case.behaviour, case.summary, spec_status = specs[case.name]
            if case.status == "passed" and spec_status != "passed":
                case.status = spec_status

    if target_status == "failed" and not any(case.status == "failed" for case in cases):
        cases.append(
            CaseResult(
                name="测试目标执行",
                status="failed",
                module="测试基础设施",
                summary="测试目标异常退出，未输出对应的 Unity 失败用例",
            )
        )
    elif not cases:
        cases.append(
            CaseResult(
                name="测试目标执行",
                status=target_status,
                module="测试基础设施",
                summary="该目标未输出可识别的 Unity 用例结果",
            )
        )
    return cases


def parse_junit(path: Path) -> tuple[list[TargetResult], str]:
    if not path.is_file():
        return [], f"未生成 JUnit 文件：{path}"
    try:
        root = ElementTree.parse(path).getroot()
    except (ElementTree.ParseError, OSError) as error:
        return [], f"JUnit 文件无法解析：{error}"

    targets: list[TargetResult] = []
    for testcase in root.iter("testcase"):
        output = testcase.findtext("system-out", default="")
        failure = testcase.find("failure")
        if failure is None:
            failure = testcase.find("error")
        if failure is not None and failure.text:
            output = f"{output}\n{failure.text}".strip()
        target_status = status_of(testcase)
        target = TargetResult(
            name=testcase.get("name", "未命名测试目标"),
            status=target_status,
            duration=float(testcase.get("time", "0") or 0),
            output=output,
        )
        target.cases = parse_output(output, target_status)
        targets.append(target)
    if not targets:
        return [], "JUnit 中没有测试目标"
    return targets, ""


def load_gates(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return {"gates": [], "error": f"门禁记录无法解析：{error}"}
    data.setdefault("gates", [])
    return data


def git_metadata(root: Path) -> tuple[str, bool]:
    def run(*args: str) -> str:
        result = subprocess.run(
            ["git", "-C", str(root), *args],
            check=False,
            capture_output=True,
            text=True,
        )
        return result.stdout.strip() if result.returncode == 0 else "未知"

    commit = run("rev-parse", "--short", "HEAD")
    dirty = bool(run("status", "--porcelain"))
    return commit, dirty


def write_raw_logs(targets: Iterable[TargetResult], raw_dir: Path) -> None:
    if raw_dir.exists():
        shutil.rmtree(raw_dir)
    raw_dir.mkdir(parents=True, exist_ok=True)
    for target in targets:
        safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", target.name)
        (raw_dir / f"{safe_name}.log").write_text(target.output, encoding="utf-8")


def missing_purposes(targets: Iterable[TargetResult]) -> list[str]:
    return [
        f"{target.name}/{case.name}"
        for target in targets
        for case in target.cases
        if not case.summary
    ]


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def status_label(status: str) -> str:
    return {"passed": "通过", "failed": "失败", "skipped": "未测试"}.get(
        status, "未知"
    )


def render_gate(gate: dict) -> str:
    status = gate.get("status", "unknown")
    duration = float(gate.get("duration_seconds", 0))
    log_path = gate.get("log", "")
    log_link = f'<a href="{esc(log_path)}">检查输出</a>' if log_path else ""
    return f"""
    <article class="gate {esc(status)}">
      <div><span class="status-dot"></span><strong>{esc(gate.get('name', '未命名门禁'))}</strong></div>
      <span class="status-text">{status_label(status)}</span>
      <small>{duration:.2f} 秒 {log_link}</small>
    </article>"""


def render_case(case: CaseResult, raw_file: str) -> str:
    purpose = case.summary or "尚未补充中文测试目的"
    contract = (
        f'<span class="contract">{esc(case.behaviour)}</span>' if case.behaviour else ""
    )
    location = f"{case.file}:{case.line}" if case.file else "无用例位置"
    detail = (
        f'<div class="failure"><strong>失败信息：</strong>{esc(case.detail)}</div>'
        if case.detail
        else ""
    )
    contract_value = esc(case.behaviour) if case.behaviour else ""
    return f"""
    <article class="case" data-status="{esc(case.status)}" data-contract="{contract_value}">
      <div class="case-main">
        <span class="badge {esc(case.status)}">{status_label(case.status)}</span>
        <div>
          <h4>{esc(purpose)} {contract}</h4>
          <code>{esc(case.name)}</code>
        </div>
      </div>
      <div class="case-meta"><span>{esc(location)}</span><a href="raw/{esc(raw_file)}">原始输出</a></div>
      {detail}
    </article>"""


def render_targets(targets: list[TargetResult]) -> str:
    modules: dict[str, dict[str, list[CaseResult]]] = {}
    durations: dict[str, float] = {}
    statuses: dict[str, str] = {}
    for target in targets:
        durations[target.name] = target.duration
        statuses[target.name] = target.status
        for case in target.cases:
            modules.setdefault(case.module, {}).setdefault(target.name, []).append(case)

    sections = []
    for module in sorted(modules):
        target_html = []
        for target_name, cases in sorted(modules[module].items()):
            raw_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", target_name) + ".log"
            case_html = "".join(render_case(case, raw_name) for case in cases)
            target = next(item for item in targets if item.name == target_name)
            raw_output = esc(target.output) or "该测试目标没有标准输出"
            target_html.append(
                f"""
                <section class="target">
                  <header><h3>{esc(target_name)}</h3><span>{status_label(statuses[target_name])} · {durations[target_name]:.3f} 秒 · {len(cases)} 个用例</span></header>
                  {case_html}
                  <details class="raw-output"><summary>Unity 原始输出</summary><pre>{raw_output}</pre></details>
                </section>"""
            )
        sections.append(
            f'<section class="module"><h2>{esc(module)}</h2>{"".join(target_html)}</section>'
        )
    return "".join(sections)


def build_html(root: Path, gates: dict, targets: list[TargetResult], junit_error: str) -> str:
    cases = [case for target in targets for case in target.cases]
    counts = {
        status: sum(case.status == status for case in cases)
        for status in ("passed", "failed", "skipped")
    }
    commit, dirty = git_metadata(root)
    gate_html = "".join(render_gate(gate) for gate in gates.get("gates", []))
    error_html = f'<div class="notice failed">{esc(junit_error)}</div>' if junit_error else ""
    started = gates.get("started_at", datetime.now().astimezone().isoformat(timespec="seconds"))
    duration = float(gates.get("duration_seconds", sum(t.duration for t in targets)))
    dirty_text = "有未提交修改" if dirty else "工作区干净"
    body = render_targets(targets) or '<div class="notice">本次没有可展示的测试结果</div>'
    return f"""<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Framework 测试报告</title>
<style>
:root {{ color-scheme: light; --ink:#18212a; --muted:#66717d; --line:#d9dee3; --pass:#16784b; --fail:#b42318; --skip:#8a5a00; --surface:#f5f7f8; }}
* {{ box-sizing:border-box; }}
body {{ margin:0; color:var(--ink); background:#fff; font:14px/1.55 system-ui,-apple-system,"Segoe UI",sans-serif; }}
main {{ width:min(1180px,calc(100% - 32px)); margin:0 auto 64px; }}
.top {{ padding:32px 0 22px; border-bottom:1px solid var(--line); }}
h1,h2,h3,h4,p {{ margin-top:0; }} h1 {{ margin-bottom:8px; font-size:28px; }}
.meta {{ display:flex; flex-wrap:wrap; gap:8px 20px; color:var(--muted); }}
.gates {{ display:grid; grid-template-columns:repeat(4,minmax(0,1fr)); gap:10px; margin:20px 0; }}
.gate {{ border:1px solid var(--line); border-top:3px solid var(--muted); border-radius:6px; padding:14px; display:grid; gap:8px; min-width:0; }}
.gate.passed {{ border-top-color:var(--pass); }} .gate.failed {{ border-top-color:var(--fail); }} .gate.skipped {{ border-top-color:var(--skip); }}
.status-dot {{ display:inline-block; width:8px; height:8px; margin-right:8px; border-radius:50%; background:var(--muted); }}
.passed .status-dot {{ background:var(--pass); }} .failed .status-dot {{ background:var(--fail); }} .skipped .status-dot {{ background:var(--skip); }}
.status-text {{ font-size:20px; font-weight:700; }} .gate small {{ color:var(--muted); }} .gate a {{ color:#245b8f; float:right; }}
.summary {{ display:flex; gap:20px; padding:14px 0; font-weight:700; }}
.summary .passed {{ color:var(--pass); }} .summary .failed {{ color:var(--fail); }} .summary .skipped {{ color:var(--skip); }}
.toolbar {{ position:sticky; top:0; z-index:2; display:flex; gap:4px; padding:10px 0; background:#fff; border-bottom:1px solid var(--line); }}
button {{ border:1px solid var(--line); border-radius:5px; padding:7px 12px; background:#fff; color:var(--ink); cursor:pointer; }} button.active {{ color:#fff; background:#26323d; border-color:#26323d; }}
.module {{ padding-top:26px; }} .module>h2 {{ font-size:21px; border-bottom:2px solid var(--ink); padding-bottom:7px; }}
.target {{ margin:0 0 22px; }} .target>header {{ display:flex; justify-content:space-between; gap:16px; align-items:baseline; background:var(--surface); padding:9px 12px; }}
.target h3 {{ margin:0; font-size:16px; }} .target header span,.case-meta {{ color:var(--muted); font-size:12px; }}
.case {{ border-bottom:1px solid var(--line); padding:13px 12px; }} .case-main {{ display:flex; gap:12px; align-items:flex-start; }}
.case h4 {{ margin:0 0 3px; font-size:14px; }} .badge {{ flex:none; min-width:48px; text-align:center; border-radius:4px; padding:2px 6px; color:#fff; font-size:12px; }}
.badge.passed {{ background:var(--pass); }} .badge.failed {{ background:var(--fail); }} .badge.skipped {{ background:var(--skip); }}
.contract {{ margin-left:6px; color:#305d8c; font-size:12px; }} .case-meta {{ display:flex; justify-content:space-between; gap:12px; margin:7px 0 0 60px; overflow-wrap:anywhere; }}
.case-meta a {{ color:#245b8f; }} .failure {{ margin:8px 0 0 60px; color:var(--fail); white-space:pre-wrap; }}
.raw-output {{ margin:8px 12px; }} .raw-output summary {{ color:#245b8f; cursor:pointer; }} .raw-output pre {{ max-height:360px; overflow:auto; padding:12px; background:#161b22; color:#e6edf3; white-space:pre-wrap; overflow-wrap:anywhere; font-size:12px; }}
.notice {{ margin:18px 0; padding:12px; border-left:4px solid var(--skip); background:#fff8e8; }} .notice.failed {{ border-color:var(--fail); background:#fff1f0; }}
[hidden] {{ display:none!important; }}
@media (max-width:760px) {{ .gates {{ grid-template-columns:repeat(2,minmax(0,1fr)); }} .target>header {{ align-items:flex-start; flex-direction:column; gap:2px; }} .case-meta,.failure {{ margin-left:0; }} }}
</style>
</head>
<body><main>
  <header class="top">
    <h1>Framework 测试报告</h1>
    <div class="meta"><span>提交 <code>{esc(commit)}</code></span><span>{dirty_text}</span><span>开始于 {esc(started)}</span><span>总耗时 {duration:.2f} 秒</span></div>
  </header>
  <section class="gates">{gate_html}</section>
  {error_html}
  <div class="summary"><span>目标 {len(targets)}</span><span>用例 {len(cases)}</span><span class="passed">通过 {counts['passed']}</span><span class="failed">失败 {counts['failed']}</span><span class="skipped">未测试 {counts['skipped']}</span></div>
  <nav class="toolbar" aria-label="结果筛选"><button class="active" data-filter="all">全部</button><button data-filter="failed">仅失败</button><button data-filter="contract">行为契约</button></nav>
  <div id="results">{body}</div>
</main>
<script>
const buttons=[...document.querySelectorAll('[data-filter]')];
function applyFilter(filter){{
  document.querySelectorAll('.case').forEach(item=>{{ item.hidden=filter==='failed'?item.dataset.status!=='failed':filter==='contract'?!item.dataset.contract:false; }});
  document.querySelectorAll('.target').forEach(item=>{{ item.hidden=![...item.querySelectorAll('.case')].some(child=>!child.hidden); }});
  document.querySelectorAll('.module').forEach(item=>{{ item.hidden=![...item.querySelectorAll('.target')].some(child=>!child.hidden); }});
}}
buttons.forEach(button=>button.addEventListener('click',()=>{{ buttons.forEach(item=>item.classList.remove('active')); button.classList.add('active'); applyFilter(button.dataset.filter); }}));
</script></body></html>"""


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--junit", required=True, type=Path)
    parser.add_argument("--gates", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--raw-dir", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    args = parser.parse_args(argv)

    gates = load_gates(args.gates)
    targets, junit_error = parse_junit(args.junit)
    missing = missing_purposes(targets)
    write_raw_logs(targets, args.raw_dir)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        build_html(args.root, gates, targets, junit_error), encoding="utf-8"
    )
    if gates.get("error"):
        print(gates["error"], file=sys.stderr)
        return 1
    if missing:
        print("以下用例未填写测试目的：", file=sys.stderr)
        for name in missing:
            print(f"  - {name}", file=sys.stderr)
        return 1
    print(f"测试报告：{args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
