#!/usr/bin/env python3

import argparse
import re
from pathlib import Path


CALLSITE_RE = re.compile(r"^\s*!j9x\s+0x[0-9A-Fa-f]+,0x([0-9A-Fa-f]+)\s+(.+?):\d+\s*$")


def find_callsites_file(path: Path) -> Path:
    if path.is_file():
        return path

    matches = sorted(path.glob("callsites.pid*.txt"))
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise FileNotFoundError(f"No callsites file matching 'callsites.pid*.txt' found in {path}")
    raise FileNotFoundError(f"Multiple callsites files matching 'callsites.pid*.txt' found in {path}")


def parse_callsites(path: Path):
    callsites_file = find_callsites_file(path)
    stats = {}

    for line in callsites_file.read_text(encoding="utf-8").splitlines():
        match = CALLSITE_RE.match(line)
        if not match:
            continue

        size = int(match.group(1), 16)
        source_file = match.group(2)

        if source_file not in stats:
            stats[source_file] = [0, 0, size, size]

        entry = stats[source_file]
        entry[0] += 1
        entry[1] += size
        entry[2] = min(entry[2], size)
        entry[3] = max(entry[3], size)

    if not stats:
        raise ValueError(f"No callsite allocation entries were parsed from {callsites_file}")

    return callsites_file, stats


def print_summary(stats, sort_by: str):
    rows = []
    for source_file, entry in stats.items():
        count = entry[0]
        total = entry[1]
        min_value = entry[2]
        max_value = entry[3]
        avg = total / count
        rows.append((source_file, count, total, min_value, max_value, avg))

    sort_index = {
        "source": 0,
        "count": 1,
        "total": 2,
        "min": 3,
        "max": 4,
        "avg": 5,
    }[sort_by]

    reverse = sort_by != "source"
    rows.sort(key=lambda row: row[sort_index], reverse=reverse)

    source_width = max(len("Source File"), max(len(row[0]) for row in rows))
    count_width = max(len("Count"), max(len(str(row[1])) for row in rows))
    total_width = max(len("Total"), max(len(str(row[2])) for row in rows))
    min_width = max(len("Min"), max(len(str(row[3])) for row in rows))
    max_width = max(len("Max"), max(len(str(row[4])) for row in rows))
    avg_width = max(len("Avg"), max(len(f"{row[5]:.1f}") for row in rows))

    print(
        f"{'Source File':<{source_width}}  "
        f"{'Count':>{count_width}}  "
        f"{'Total':>{total_width}}  "
        f"{'Min':>{min_width}}  "
        f"{'Max':>{max_width}}  "
        f"{'Avg':>{avg_width}}"
    )

    for source_file, count, total, min_value, max_value, avg in rows:
        print(
            f"{source_file:<{source_width}}  "
            f"{count:>{count_width}d}  "
            f"{total:>{total_width}d}  "
            f"{min_value:>{min_width}d}  "
            f"{max_value:>{max_width}d}  "
            f"{avg:>{avg_width}.1f}"
        )


def main():
    parser = argparse.ArgumentParser(
        description="Aggregate allocation sizes from callsites.pid*.txt per source file."
    )
    parser.add_argument(
        "input_path",
        help="Path to a collect_openj9_footprint.py output directory or a callsites.pid*.txt file",
    )
    parser.add_argument(
        "--sort-by",
        choices=["source", "count", "total", "min", "max", "avg"],
        default="total",
        help="Sort output rows by the selected column (default: total)",
    )
    args = parser.parse_args()

    input_path = Path(args.input_path)
    callsites_file, stats = parse_callsites(input_path)

    print(f"Input: {callsites_file}")
    print_summary(stats, args.sort_by)


if __name__ == "__main__":
    main()

# Made with Bob
