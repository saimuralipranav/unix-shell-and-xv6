#!/usr/bin/env python3
"""
generate_plot.py - Generate MLFQ scheduling timeline SVG visualization
Parses [PLOT] <tick> <pid> <queue> logs and outputs a publication-quality SVG plot.
"""

import sys
import os
import re

PID_NAMES = {
    4: "PID 4 (CPU-Bound 1)",
    5: "PID 5 (CPU-Bound 2)",
    6: "PID 6 (I/O-Bound 1)",
    7: "PID 7 (I/O-Bound 2)",
    8: "PID 8 (Mixed Workload)",
}

PID_COLORS = {
    4: "#E63946",  # Crimson Red
    5: "#F4A261",  # Sandy Orange
    6: "#2A9D8F",  # Teal Green
    7: "#457B9D",  # Steel Blue
    8: "#9B5DE5",  # Purple
}

def parse_plot_data(filepath):
    data = {}
    if not os.path.exists(filepath):
        print(f"Error: File {filepath} does not exist.")
        return data

    with open(filepath, "r") as f:
        for line in f:
            line = line.strip()
            if "[PLOT]" in line:
                parts = line.split("[PLOT]")[1].strip().split()
                if len(parts) >= 3:
                    try:
                        tick = int(parts[0])
                        pid = int(parts[1])
                        queue = int(parts[2])
                        if pid not in data:
                            data[pid] = []
                        data[pid].append((tick, queue))
                    except ValueError:
                        continue
    return data

def generate_svg(data, output_svg_path):
    if not data:
        print("No plot data found.")
        return

    # Find tick range
    all_ticks = [t for points in data.values() for t, q in points]
    if not all_ticks:
        return
    min_tick = min(all_ticks)
    max_tick = max(all_ticks)

    # SVG canvas dimensions
    width = 1100
    height = 620
    margin_left = 90
    margin_right = 240
    margin_top = 80
    margin_bottom = 80

    plot_w = width - margin_left - margin_right
    plot_h = height - margin_top - margin_bottom

    def x_coord(tick):
        if max_tick == min_tick:
            return margin_left
        return margin_left + (tick - min_tick) / (max_tick - min_tick) * plot_w

    def y_coord(q):
        # Q0 at top, Q3 at bottom
        return margin_top + (q / 3.0) * plot_h

    svg = []
    svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="100%" height="100%" style="background:#ffffff; font-family: -apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica, Arial, sans-serif;">')

    # Styles
    svg.append("""
    <style>
        .title { font-size: 20px; font-weight: bold; fill: #1D3557; }
        .subtitle { font-size: 13px; fill: #6C757D; }
        .axis-label { font-size: 14px; font-weight: 600; fill: #2B2D42; }
        .tick-label { font-size: 12px; fill: #4A5568; }
        .grid-line { stroke: #E2E8F0; stroke-width: 1; stroke-dasharray: 4,4; }
        .q-band { fill: #F8FAFC; stroke: #CBD5E1; stroke-width: 1; }
        .boost-line { stroke: #E63946; stroke-width: 2; stroke-dasharray: 6,4; }
        .boost-label { font-size: 12px; font-weight: bold; fill: #E63946; text-anchor: middle; }
        .legend-text { font-size: 12px; fill: #2B2D42; font-weight: 500; }
    </style>
    """)

    # Background
    svg.append(f'<rect width="{width}" height="{height}" fill="#ffffff"/>')

    # Title & Subtitle
    svg.append(f'<text x="{margin_left}" y="36" class="title">MLFQ Scheduler Timeline &amp; Priority Transition Analysis</text>')
    svg.append(f'<text x="{margin_left}" y="56" class="subtitle">xv6 Multi-Level Feedback Queue: 4 Priority Levels (Q0-Q3), Dynamic Demotion, and 48-Tick Starvation Boost</text>')

    # Queue row backgrounds & grid
    queue_names = [
        "Queue 0 (Quantum: 1 tick)",
        "Queue 1 (Quantum: 4 ticks)",
        "Queue 2 (Quantum: 8 ticks)",
        "Queue 3 (Quantum: 16 ticks)"
    ]

    for q in range(4):
        y = y_coord(q)
        # horizontal baseline
        svg.append(f'<line x1="{margin_left}" y1="{y}" x2="{margin_left + plot_w}" y2="{y}" stroke="#94A3B8" stroke-width="1.5"/>')
        # Queue labels on Y-axis
        svg.append(f'<text x="{margin_left - 15}" y="{y + 4}" text-anchor="end" class="tick-label" font-weight="bold">Q{q}</text>')
        svg.append(f'<text x="{margin_left + plot_w + 10}" y="{y + 4}" text-anchor="start" class="tick-label" fill="#64748B">{queue_names[q]}</text>')

    # Vertical gridlines for ticks
    tick_step = 5
    for t in range(min_tick, max_tick + 1):
        if t % tick_step == 0 or t == max_tick:
            x = x_coord(t)
            svg.append(f'<line x1="{x}" y1="{margin_top}" x2="{x}" y2="{margin_top + plot_h}" class="grid-line"/>')
            svg.append(f'<text x="{x}" y="{margin_top + plot_h + 20}" text-anchor="middle" class="tick-label">{t}</text>')

    # Priority boost marker at tick 48
    if min_tick <= 48 <= max_tick:
        bx = x_coord(48)
        svg.append(f'<line x1="{bx}" y1="{margin_top - 10}" x2="{bx}" y2="{margin_top + plot_h + 10}" class="boost-line"/>')
        svg.append(f'<rect x="{bx - 55}" y="{margin_top - 26}" width="110" height="20" rx="4" fill="#FFE3E3" stroke="#E63946" stroke-width="1"/>')
        svg.append(f'<text x="{bx}" y="{margin_top - 12}" class="boost-label">Boost (Tick 48)</text>')

    # X-axis title
    svg.append(f'<text x="{margin_left + plot_w/2}" y="{margin_top + plot_h + 50}" text-anchor="middle" class="axis-label">Time Elapsed (Timer Ticks)</text>')
    # Y-axis title
    svg.append(f'<text x="25" y="{margin_top + plot_h/2}" text-anchor="middle" transform="rotate(-90 25 {margin_top + plot_h/2})" class="axis-label">MLFQ Priority Queue</text>')

    # Draw lines and points for each process
    # Slight jitter offsets for visual separation when multiple processes share same queue
    pids = sorted(data.keys())
    for idx, pid in enumerate(pids):
        points = data[pid]
        color = PID_COLORS.get(pid, "#333333")
        y_offset = (idx - len(pids)/2.0) * 4.5  # slight vertical spread so points don't occlude

        path_d = []
        for i, (t, q) in enumerate(points):
            x = x_coord(t)
            y = y_coord(q) + y_offset
            if i == 0:
                path_d.append(f"M {x:.1f} {y:.1f}")
            else:
                path_d.append(f"L {x:.1f} {y:.1f}")

        # Polyline
        svg.append(f'<path d="{" ".join(path_d)}" fill="none" stroke="{color}" stroke-width="2.2" stroke-opacity="0.85"/>')

        # Circles
        for t, q in points:
            x = x_coord(t)
            y = y_coord(q) + y_offset
            svg.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.5" fill="{color}" stroke="#ffffff" stroke-width="1"/>')

    # Legend box
    leg_x = margin_left + plot_w + 10
    leg_y = margin_top + 140
    svg.append(f'<rect x="{leg_x}" y="{leg_y}" width="215" height="{len(pids)*28 + 35}" rx="6" fill="#F8FAFC" stroke="#E2E8F0" stroke-width="1.5"/>')
    svg.append(f'<text x="{leg_x + 12}" y="{leg_y + 22}" font-size="13px" font-weight="bold" fill="#1E293B">Processes (Workloads)</text>')

    for idx, pid in enumerate(pids):
        color = PID_COLORS.get(pid, "#333333")
        label = PID_NAMES.get(pid, f"PID {pid}")
        ly = leg_y + 44 + idx * 26
        svg.append(f'<circle cx="{leg_x + 20}" cy="{ly - 4}" r="5" fill="{color}"/>')
        svg.append(f'<text x="{leg_x + 34}" y="{ly}" class="legend-text">{label}</text>')

    # Watermark (equivalent to plt.text in matplotlib)
    svg.append(f'<text x="{width - 20}" y="20" text-anchor="end" alignment-baseline="hanging" font-size="14px" fill="gray" opacity="0.7">bommisetty.pranav</text>')

    svg.append('</svg>')

    with open(output_svg_path, "w") as f:
        f.write("\n".join(svg))
    print(f"Plot successfully saved to {output_svg_path}")

def main():
    log_path = "mlfq_output.txt"
    if len(sys.argv) > 1:
        log_path = sys.argv[1]
    elif not os.path.exists(log_path):
        log_path = "/home/smp/.gemini/antigravity-ide/brain/1cfd8d16-2da5-4e9b-bd2e-da10f0bbc981/scratch/mlfq_output.txt"

    output_path = "mlfq_timeline.svg"
    if len(sys.argv) > 2:
        output_path = sys.argv[2]

    data = parse_plot_data(log_path)
    generate_svg(data, output_path)

if __name__ == "__main__":
    main()
