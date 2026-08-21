#!/usr/bin/env python3
"""Prints a one-line PASS/FAIL summary per test, using each test's ctest
result joined with a human-readable description pulled from the
"// CHECKS: <description>" comment directly above its DROGON_TEST(...) in
the test source. Keeping the description next to the test it describes
(instead of in a separate lookup table) means it can't drift out of sync.

Usage: print_test_summary.py <junit.xml> <test-source-dir>
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

CHECKS_RE = re.compile(r"^\s*//\s*CHECKS:\s*(.+?)\s*$")
TEST_RE = re.compile(r"^\s*DROGON_TEST\s*\(\s*(\w+)\s*\)")


def load_descriptions(test_source_dir: Path) -> dict:
    descriptions = {}
    pending = None
    for source_file in sorted(test_source_dir.glob("*.cc")):
        for line in source_file.read_text().splitlines():
            checks_match = CHECKS_RE.match(line)
            if checks_match:
                pending = checks_match.group(1)
                continue
            test_match = TEST_RE.match(line)
            if test_match:
                descriptions[test_match.group(1)] = pending or "(no description found)"
                pending = None
                continue
            # A non-blank, non-comment line resets a dangling "CHECKS:" that
            # wasn't immediately followed by a DROGON_TEST.
            if line.strip():
                pending = None
    return descriptions


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <junit.xml> <test-source-dir>", file=sys.stderr)
        return 2

    junit_path = Path(sys.argv[1])
    test_source_dir = Path(sys.argv[2])

    descriptions = load_descriptions(test_source_dir)
    root = ET.parse(junit_path).getroot()

    failed = 0
    print("Test summary:")
    for testcase in root.findall("testcase"):
        name = testcase.get("name")
        failure = testcase.find("failure") is not None or testcase.get("status") == "fail"
        status = "FAIL" if failure else "PASS"
        if failure:
            failed += 1
        description = descriptions.get(name, "(no description found)")
        print(f"  [{status}] {name}: {description}")

    total = len(root.findall("testcase"))
    print(f"\n{total - failed}/{total} tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
