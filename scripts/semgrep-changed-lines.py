#!/usr/bin/env python3
"""Prints the findings of `semgrep scan --json` (read from stdin) and exits 1 if any.

A rule whose metadata sets `changed-lines-only: true` reports only lines changed
since the given ref: the working tree against it, staged, unstaged and untracked
files included. Every other rule reports every match.

    semgrep scan --config .semgrep --json . | scripts/semgrep-changed-lines.py <ref>
"""
import json
import re
import subprocess
import sys


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def changed_lines(base):
    """path -> the set of changed line numbers, or None when the whole file is new."""
    changed = {}
    path = None

    for line in git("diff", "-U0", "--no-color", base, "--").splitlines():
        if line.startswith("+++ "):
            path = line[len("+++ b/"):] if line.startswith("+++ b/") else None
            if path is not None:
                changed.setdefault(path, set())
        elif line.startswith("@@") and path is not None:
            hunk = re.match(r"@@ -\S+ \+(\d+)(?:,(\d+))? @@", line)
            start, count = int(hunk.group(1)), int(hunk.group(2) or 1)
            changed[path].update(range(start, start + count))

    for path in git("ls-files", "--others", "--exclude-standard").splitlines():
        changed[path] = None

    return changed


def source_line(path, line):
    """Semgrep's JSON shows the matched code only when logged in, so it is read here."""
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read().splitlines()[line - 1].strip()
    except (OSError, IndexError):
        return ""


def main():
    base = sys.argv[1]
    report = json.load(sys.stdin)
    changed = changed_lines(base)
    findings = 0

    for error in report.get("errors", []):
        print(f"semgrep error: {error.get('message', error)}")
        findings += 1

    for result in report.get("results", []):
        path, line = result["path"], result["start"]["line"]

        if result["extra"].get("metadata", {}).get("changed-lines-only"):
            lines = changed.get(path, set())
            if lines is not None and line not in lines:
                continue

        rule = result["check_id"].rsplit(".", 1)[-1]
        print(f"{path}:{line}: [{rule}] {result['extra']['message'].strip()}")
        print(f"    {source_line(path, line)}")
        findings += 1

    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
