#!/usr/bin/env python3
"""Framework 测试报告生成器单元测试。"""

import json
import tempfile
import unittest
from pathlib import Path

from generate_test_report import (
    TargetResult,
    build_html,
    load_gates,
    missing_purposes,
    parse_junit,
    parse_output,
    render_case,
)


class ReportGeneratorTest(unittest.TestCase):
    def test_parse_unity_and_spec(self):
        output = "\n".join(
            [
                "/repo/tests/runtime/test_bus.c:12:test_publish:PASS",
                "WDFSPEC\tEBUS-01\t发布事件后通知订阅者\ttest_publish\tpassed",
            ]
        )
        case = parse_output(output, "passed")[0]
        self.assertEqual("runtime", case.module)
        self.assertEqual("EBUS-01", case.behaviour)
        self.assertEqual("发布事件后通知订阅者", case.summary)
        self.assertEqual("passed", case.status)

    def test_parse_failure_and_escape_html(self):
        output = "/repo/tests/common/test_x.c:9:test_value:FAIL:Expected <x> & y"
        case = parse_output(output, "failed")[0]
        case_html = render_case(case, "test_x.log")
        report = build_html(Path("."), {"gates": []}, [], "<损坏>")
        self.assertEqual("failed", case.status)
        self.assertEqual("Expected <x> & y", case.detail)
        self.assertIn("Expected &lt;x&gt; &amp; y", case_html)
        self.assertIn("&lt;损坏&gt;", report)

    def test_target_without_unity_output_is_explicit(self):
        case = parse_output("Segmentation fault", "failed")[0]
        self.assertEqual("failed", case.status)
        self.assertEqual("测试目标执行", case.name)

    def test_crash_after_passed_case_adds_target_failure(self):
        output = "/repo/tests/common/test_x.c:9:test_value:PASS\nSegmentation fault"
        cases = parse_output(output, "failed")
        self.assertEqual(["passed", "failed"], [case.status for case in cases])

    def test_parse_junit_with_skipped_target(self):
        with tempfile.TemporaryDirectory() as directory:
            junit = Path(directory) / "junit.xml"
            junit.write_text(
                '<testsuite><testcase name="test_a" time="0.1"><skipped/>'
                '<system-out></system-out></testcase></testsuite>',
                encoding="utf-8",
            )
            targets, error = parse_junit(junit)
        self.assertFalse(error)
        self.assertEqual("skipped", targets[0].status)
        self.assertEqual("skipped", targets[0].cases[0].status)

    def test_missing_or_invalid_gate_file_fails_explicitly(self):
        with tempfile.TemporaryDirectory() as directory:
            gates = load_gates(Path(directory) / "missing.json")
        self.assertIn("error", gates)

    def test_gate_failure_is_rendered(self):
        gates = {
            "gates": [
                {"name": "架构依赖边界", "status": "failed", "duration_seconds": 0.2}
            ]
        }
        report = build_html(Path("."), gates, [], "")
        self.assertIn("架构依赖边界", report)
        self.assertIn("失败", report)

    def test_missing_purpose_is_reported(self):
        cases = parse_output("/repo/tests/common/test_x.c:9:test_value:PASS", "passed")
        target = TargetResult("test_x", "passed", 0.1, "", cases)
        self.assertEqual(["test_x/test_value"], missing_purposes([target]))

        cases[0].summary = "验证示例值"
        self.assertEqual([], missing_purposes([target]))


if __name__ == "__main__":
    unittest.main()
