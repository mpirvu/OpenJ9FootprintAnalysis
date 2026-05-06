#!/usr/bin/env python3

import argparse
import json
import math
import re
from pathlib import Path


SECTION_HEADER = "printSpaceKBTakenByVmComponents"
LINE_RE = re.compile(
    r"^\s*(.+?):\s+Virtual=\s*(\d+)\s+KB;\s+RSS=\s*(\d+)\s+KB\s*$"
)


def find_analysis_file(run_dir: Path) -> Path:
    matches = sorted(run_dir.glob("footprintAnalysis.pid*.txt"))
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise FileNotFoundError(
            f"No footprintAnalysis output matching 'footprintAnalysis.pid*.txt' found in {run_dir}"
        )
    raise FileNotFoundError(
        f"Multiple footprintAnalysis outputs matching 'footprintAnalysis.pid*.txt' found in {run_dir}"
    )


def parse_vm_components(run_dir: Path):
    analysis_file = find_analysis_file(run_dir)
    lines = analysis_file.read_text(encoding="utf-8").splitlines()

    in_section = False
    rows = []

    for line in lines:
        if not in_section:
            if SECTION_HEADER in line:
                in_section = True
            continue

        if not line.strip():
            if rows:
                break
            continue

        match = LINE_RE.match(line)
        if match:
            label = match.group(1).rstrip()
            virt = int(match.group(2))
            rss = int(match.group(3))
            rows.append((label, virt, rss))
            continue

        if rows:
            break

    if not rows:
        raise ValueError(f"Could not parse section below '{SECTION_HEADER}' in {analysis_file}")

    return rows


def format_percent(old: int, new: int) -> str:
    if old == 0:
        if new == 0:
            return "+0%"
        return "n/a"
    change = ((new - old) / old) * 100.0
    rounded = int(round(change))
    return f"{rounded:+d}%"


def main():
    parser = argparse.ArgumentParser(
        description="Compare VM component footprint summaries from two collect_openj9_footprint.py output directories."
    )
    parser.add_argument("run1", help="First footprint collection directory")
    parser.add_argument("run2", help="Second footprint collection directory")
    args = parser.parse_args()

    run1 = Path(args.run1)
    run2 = Path(args.run2)

    rows1 = parse_vm_components(run1)
    rows2 = parse_vm_components(run2)

    map1 = {label: (virt, rss) for label, virt, rss in rows1}
    map2 = {label: (virt, rss) for label, virt, rss in rows2}

    ordered_labels = [label for label, _, _ in rows1]
    for label, _, _ in rows2:
        if label not in map1:
            ordered_labels.append(label)

    label_width = max(len(label) for label in ordered_labels + ["Component"])
    num_width = max(
        7,
        max(len(str(value)) for label in ordered_labels for value in (*map1.get(label, (0, 0)), *map2.get(label, (0, 0))))
    )

    header = (
        f"{'Component':<{label_width}}  "
        f"{'VIRT1':>{num_width}} {'RSS1':>{num_width}} | "
        f"{'VIRT2':>{num_width}} {'RSS2':>{num_width}} | "
        f"{'ΔVIRT':>6} {'ΔRSS':>6}"
    )
    print(header)

    for label in ordered_labels:
        virt1, rss1 = map1.get(label, (0, 0))
        virt2, rss2 = map2.get(label, (0, 0))
        print(
            f"{label:<{label_width}}  "
            f"{virt1:>{num_width}d} {rss1:>{num_width}d} | "
            f"{virt2:>{num_width}d} {rss2:>{num_width}d} | "
            f"{format_percent(virt1, virt2):>6} {format_percent(rss1, rss2):>6}"
        )


if __name__ == "__main__":
    main()

# Made with Bob
