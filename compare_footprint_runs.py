#!/usr/bin/env python3

import argparse
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


def build_ordered_component_data(rows1, rows2):
    map1 = {label: (virt, rss) for label, virt, rss in rows1}
    map2 = {label: (virt, rss) for label, virt, rss in rows2}

    ordered_labels = [label for label, _, _ in rows1]
    for label, _, _ in rows2:
        if label not in map1:
            ordered_labels.append(label)

    return ordered_labels, map1, map2


def print_comparison_table(ordered_labels, map1, map2):
    label_width = max(len(label) for label in ordered_labels + ["Component"])
    num_width = max(
        7,
        max(
            len(str(value))
            for label in ordered_labels
            for value in (*map1.get(label, (0, 0)), *map2.get(label, (0, 0)))
        ),
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


def svg_escape(text: str) -> str:
    return text.replace("&", "&").replace("<", "<").replace(">", ">")


def plot_rss_comparison_svg(ordered_labels, map1, map2, run1_label, run2_label, output_path: Path):
    rss1_values = [map1.get(label, (0, 0))[1] for label in ordered_labels]
    rss2_values = [map2.get(label, (0, 0))[1] for label in ordered_labels]
    max_rss = max(rss1_values + rss2_values + [1])

    chart_width = max(900, len(ordered_labels) * 90)
    chart_height = 520
    margin_left = 90
    margin_right = 30
    margin_top = 60
    margin_bottom = 170
    plot_width = chart_width - margin_left - margin_right
    plot_height = chart_height - margin_top - margin_bottom

    group_width = plot_width / max(len(ordered_labels), 1)
    bar_width = min(24.0, group_width * 0.28)

    def y_scale(value: int) -> float:
        return margin_top + plot_height - (value / max_rss) * plot_height

    svg_parts = [
        f"<svg xmlns='http://www.w3.org/2000/svg' width='{chart_width}' height='{chart_height}' viewBox='0 0 {chart_width} {chart_height}'>",
        "<style>",
        "text { font-family: Arial, sans-serif; fill: #222; }",
        ".title { font-size: 20px; font-weight: bold; }",
        ".axis { stroke: #333; stroke-width: 1; }",
        ".grid { stroke: #cccccc; stroke-width: 1; stroke-dasharray: 4 4; }",
        ".legend { font-size: 13px; }",
        ".label { font-size: 12px; }",
        ".barvalue { font-size: 10px; }",
        ".tick { font-size: 11px; }",
        "</style>",
        f"<text x='{chart_width / 2}' y='30' text-anchor='middle' class='title'>RSS Comparison by VM Component</text>",
    ]

    tick_count = 5
    for tick in range(tick_count + 1):
        value = max_rss * tick / tick_count
        y = margin_top + plot_height - (plot_height * tick / tick_count)
        svg_parts.append(
            f"<line x1='{margin_left}' y1='{y:.2f}' x2='{chart_width - margin_right}' y2='{y:.2f}' class='grid' />"
        )
        svg_parts.append(
            f"<text x='{margin_left - 10}' y='{y + 4:.2f}' text-anchor='end' class='tick'>{int(round(value))}</text>"
        )

    svg_parts.append(
        f"<line x1='{margin_left}' y1='{margin_top}' x2='{margin_left}' y2='{margin_top + plot_height}' class='axis' />"
    )
    svg_parts.append(
        f"<line x1='{margin_left}' y1='{margin_top + plot_height}' x2='{chart_width - margin_right}' y2='{margin_top + plot_height}' class='axis' />"
    )
    svg_parts.append(
        f"<text x='25' y='{margin_top + plot_height / 2}' text-anchor='middle' transform='rotate(-90 25 {margin_top + plot_height / 2})' class='label'>RSS (KB)</text>"
    )

    color1 = "#4e79a7"
    color2 = "#f28e2b"

    legend_x = chart_width - margin_right - 320
    legend_y = 40
    legend_row_gap = 20
    svg_parts.append(f"<rect x='{legend_x}' y='{legend_y}' width='14' height='14' fill='{color1}' />")
    svg_parts.append(
        f"<text x='{legend_x + 20}' y='{legend_y + 12}' class='legend'>{svg_escape(run1_label)}</text>"
    )
    svg_parts.append(
        f"<rect x='{legend_x}' y='{legend_y + legend_row_gap}' width='14' height='14' fill='{color2}' />"
    )
    svg_parts.append(
        f"<text x='{legend_x + 20}' y='{legend_y + legend_row_gap + 12}' class='legend'>{svg_escape(run2_label)}</text>"
    )

    for index, label in enumerate(ordered_labels):
        center_x = margin_left + group_width * (index + 0.5)
        x1 = center_x - bar_width - 3
        x2 = center_x + 3
        rss1 = rss1_values[index]
        rss2 = rss2_values[index]
        y1 = y_scale(rss1)
        y2 = y_scale(rss2)
        h1 = margin_top + plot_height - y1
        h2 = margin_top + plot_height - y2
        label_y = margin_top + plot_height + 18

        svg_parts.append(
            f"<rect x='{x1:.2f}' y='{y1:.2f}' width='{bar_width:.2f}' height='{h1:.2f}' fill='{color1}' />"
        )
        svg_parts.append(
            f"<rect x='{x2:.2f}' y='{y2:.2f}' width='{bar_width:.2f}' height='{h2:.2f}' fill='{color2}' />"
        )

        bar1_label_y = max(y1 - 4, margin_top - 2)
        bar2_label_y = max(y2 - 4, margin_top - 2)
        svg_parts.append(
            f"<text x='{x1 + bar_width / 2:.2f}' y='{bar1_label_y:.2f}' text-anchor='middle' class='barvalue'>{rss1}</text>"
        )
        svg_parts.append(
            f"<text x='{x2 + bar_width / 2:.2f}' y='{bar2_label_y:.2f}' text-anchor='middle' class='barvalue'>{rss2}</text>"
        )

        svg_parts.append(
            f"<text x='{center_x:.2f}' y='{label_y}' text-anchor='end' transform='rotate(-45 {center_x:.2f} {label_y})' class='label'>{svg_escape(label)}</text>"
        )

    svg_parts.append("</svg>")
    output_path.write_text("\n".join(svg_parts), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(
        description="Compare VM component footprint summaries from two collect_openj9_footprint.py output directories."
    )
    parser.add_argument("run1", help="First footprint collection directory")
    parser.add_argument("run2", help="Second footprint collection directory")
    parser.add_argument(
        "--graph-output",
        default="rss_comparison.svg",
        help="Output SVG path for the RSS comparison chart (default: rss_comparison.svg)",
    )
    args = parser.parse_args()

    run1 = Path(args.run1)
    run2 = Path(args.run2)

    rows1 = parse_vm_components(run1)
    rows2 = parse_vm_components(run2)

    ordered_labels, map1, map2 = build_ordered_component_data(rows1, rows2)
    print_comparison_table(ordered_labels, map1, map2)

    graph_labels = [label for label in ordered_labels if label != "Totals"]
    output_path = Path(args.graph_output)
    plot_rss_comparison_svg(graph_labels, map1, map2, run1.name, run2.name, output_path)
    print(f"\nWrote RSS comparison graph to {output_path}")


if __name__ == "__main__":
    main()

# Made with Bob
