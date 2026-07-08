#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
构建期工具：生成洗车方案 manifest（SHA256 + size）。

用法：
    python3 tools/gen_program_manifest.py <program.json> <manifest.json>
"""

from __future__ import annotations

import hashlib
import json
import sys
from typing import Any


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write(
            "用法: gen_program_manifest.py <program.json> <manifest.json>\n"
        )
        return 2

    json_path, manifest_path = sys.argv[1], sys.argv[2]

    try:
        with open(json_path, "rb") as fp:
            data = fp.read()
        with open(json_path, "r", encoding="utf-8") as fp:
            doc = json.load(fp)
    except OSError as exc:
        sys.stderr.write(f"读取方案失败: {exc}\n")
        return 1
    except json.JSONDecodeError as exc:
        sys.stderr.write(f"JSON 解析失败: {exc}\n")
        return 1

    prog = doc.get("program") or {}
    digest = hashlib.sha256(data).hexdigest()

    manifest: dict[str, Any] = {
        "program_id": prog.get("id", ""),
        "schema_version": prog.get("schema_version", ""),
        "sha256": digest,
        "size": len(data),
    }

    try:
        with open(manifest_path, "w", encoding="utf-8") as fp:
            json.dump(manifest, fp, ensure_ascii=False, indent=2)
            fp.write("\n")
    except OSError as exc:
        sys.stderr.write(f"写出 manifest 失败: {exc}\n")
        return 1

    sys.stdout.write(f"已生成 manifest: {manifest_path}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
