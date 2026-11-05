#!/usr/bin/env python3
"""NPPM_* usage survey of plugin sources (phase 7, P7-01).

Usage: nppm_usage.py <plugins-src-dir> [--markdown]

<plugins-src-dir> holds one checkout per plugin (sub folder name = plugin name).
Counts, per message, how many plugins use it (definition lines of the bundled
Notepad_plus_msgs.h / .cs copies are ignored). Sorted by plugin count, then uses.
"""
import os
import re
import sys
from collections import defaultdict

EXTS = {".c", ".cpp", ".cxx", ".cc", ".h", ".hpp", ".cs", ".py", ".inl"}
NAME = re.compile(r"\bNPPM_[A-Z0-9_]+\b")
DEFINITION = re.compile(r"^\s*(#\s*define|[A-Z0-9_]+\s*=|NPPM_[A-Z0-9_]+\s*=|public\s+const|enum\b)")
# messages that are only bases / ranges, not requests
IGNORE = {"NPPMSG", "NPPM_"}


def main():
    root = sys.argv[1]
    markdown = "--markdown" in sys.argv
    plugins = defaultdict(set)   # message -> plugins
    uses = defaultdict(int)      # message -> occurrences
    names = sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d)))
    for plugin in names:
        for dirpath, _, files in os.walk(os.path.join(root, plugin)):
            if ".git" in dirpath.split(os.sep):
                continue
            for f in files:
                if os.path.splitext(f)[1].lower() not in EXTS:
                    continue
                # bundled copies of the upstream message header
                if f.lower().startswith("notepad_plus_msgs") or f.lower() in {"npp_messages.py"}:
                    continue
                try:
                    text = open(os.path.join(dirpath, f), encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                for line in text.splitlines():
                    if DEFINITION.match(line):
                        continue
                    for m in NAME.findall(line):
                        if m in IGNORE:
                            continue
                        plugins[m].add(plugin)
                        uses[m] += 1
    rows = sorted(plugins, key=lambda m: (-len(plugins[m]), -uses[m], m))
    if markdown:
        print("| # | message | plugins | uses |")
        print("|---|---|---|---|")
        for i, m in enumerate(rows, 1):
            print(f"| {i} | `{m}` | {len(plugins[m])} | {uses[m]} |")
    else:
        print(f"plugins: {', '.join(names)}")
        for m in rows:
            print(f"{len(plugins[m]):3} {uses[m]:5}  {m}  ({', '.join(sorted(plugins[m]))})")


if __name__ == "__main__":
    main()
