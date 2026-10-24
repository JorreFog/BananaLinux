#!/usr/bin/env python3
"""Banana Theme Studio: the example themes against docs/theme.schema.json.

Validates every theme shipped on the card and in docs/themes/ against the format's JSON Schema, so the schema and
the examples cannot drift apart. Skips (with a note) when the jsonschema package isn't installed.

    python3 tools/theme-studio/tests/schema.test.py
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SCHEMA = ROOT / "docs" / "theme.schema.json"
THEMES = [ROOT / "system" / "rootfs" / "usr" / "share" / "banana" / "themes", ROOT / "docs" / "themes"]


def strip_comments(s):
    """Strip // and /* */ comments and trailing commas, as the engine's JSON reader allows."""
    out, i, n, instr = [], 0, len(s), False
    while i < n:
        c, nx = s[i], s[i + 1] if i + 1 < n else ""
        if instr:
            out.append(c)
            if c == "\\":
                out.append(nx)
                i += 2
                continue
            if c == '"':
                instr = False
            i += 1
            continue
        if c == '"':
            instr = True
            out.append(c)
            i += 1
            continue
        if c == "/" and nx == "/":
            while i < n and s[i] != "\n":
                i += 1
            continue
        if c == "/" and nx == "*":
            i += 2
            while i < n and not (s[i] == "*" and s[i + 1 : i + 2] == "/"):
                i += 1
            i += 2
            continue
        if c == ",":
            j = i + 1
            while j < n and s[j].isspace():
                j += 1
            if j < n and s[j] in "}]":
                i += 1
                continue
        out.append(c)
        i += 1
    return "".join(out)


def main():
    try:
        import jsonschema
    except ImportError:
        print("schema.test: skipped (pip install jsonschema to run it)")
        return 0

    schema = json.loads(SCHEMA.read_text())
    jsonschema.Draft202012Validator.check_schema(schema)
    validator = jsonschema.Draft202012Validator(schema)

    files = []
    for d in THEMES:
        if d.exists():
            files.extend(sorted(d.glob("*.json")))

    fails = 0
    for f in files:
        try:
            theme = json.loads(strip_comments(f.read_text()))
        except Exception as e:  # noqa: BLE001
            print("  FAIL parse %s: %s" % (f, e))
            fails += 1
            continue
        errs = sorted(validator.iter_errors(theme), key=lambda e: list(e.path))
        if errs:
            fails += 1
            for e in errs[:5]:
                print("  FAIL %s: %s at %s" % (f.name, e.message, list(e.path)))
        else:
            print("  ok   %s" % f.name)

    print("schema.test: %s (%d theme(s))" % ("FAILED" if fails else "ok", len(files)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())