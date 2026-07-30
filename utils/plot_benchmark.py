#!/usr/bin/env python3
"""
plot_benchmark.py
=================
Genera figuras de calidad de paper a partir del CSV producido por
megatron_operator_aware_benchmark.

Uso:
    python3 utils/plot_benchmark.py benchmark_results.csv

Salida (en el mismo directorio que el CSV):
    fig1_miss_ratio.pdf  / .png  — Miss ratio (%) vs. buffer pool size
    fig2_disk_io.pdf     / .png  — Disk I/O count vs. buffer pool size
    fig3_latency.pdf     / .png  — Latency (ms) vs. buffer pool size
    fig4_summary_bars.pdf/ .png  — Grouped bar chart: reducción relativa
                                   de Operator-Aware vs. baselines
"""

import sys
import os
import csv
import math
from pathlib import Path
from collections import defaultdict

# --------------------------------------------------------------------------
# Try to import matplotlib; give a helpful error if missing.
# --------------------------------------------------------------------------
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.ticker as mticker
    from matplotlib.lines import Line2D
except ImportError:
    print("[ERROR] matplotlib is not installed.")
    print("  Install with: pip install matplotlib")
    sys.exit(1)

# --------------------------------------------------------------------------
# Styling constants
# --------------------------------------------------------------------------
POLICY_STYLES = {
    "LRU/Clock-Sweep": {
        "color":     "#e07b54",   # warm orange
        "linestyle": "--",
        "marker":    "o",
        "label":     "LRU/Clock-Sweep",
    },
    "2Q": {
        "color":     "#5b8dd9",   # steel blue
        "linestyle": "-.",
        "marker":    "s",
        "label":     "2Q (Johnson & Shasha)",
    },
    "Operator-Aware": {
        "color":     "#3dba78",   # emerald green
        "linestyle": "-",
        "marker":    "D",
        "label":     "Operator-Aware (this work)",
    },
}

WORKLOAD_META = {
    "W1_NLJ": {
        "title": "W1 — Nested Loop Join\n(outer=DISCARD_QUICKLY, inner=KEEP_HOT)",
        "short": "W1: Nested Loop Join",
    },
    "W2_HJ": {
        "title": "W2 — Hash Join\n(build=KEEP_HOT, probe=DISCARD_QUICKLY)",
        "short": "W2: Hash Join",
    },
    "W3_CONC": {
        "title": "W3 — Concurrent SeqScan + Join\n(scan=DISCARD_QUICKLY, join-build=KEEP_HOT)",
        "short": "W3: Concurrent Scan+Join",
    },
}

WORKLOAD_ORDER = ["W1_NLJ", "W2_HJ", "W3_CONC"]
POLICY_ORDER   = ["LRU/Clock-Sweep", "2Q", "Operator-Aware"]

# --------------------------------------------------------------------------
# Data loading
# --------------------------------------------------------------------------

def load_csv(filepath: str):
    """Returns list of dicts, coercing numeric fields."""
    rows = []
    with open(filepath, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append({
                "workload":        row["workload"].strip(),
                "policy":          row["policy"].strip(),
                "buffer_pct":      int(row["buffer_pct"]),
                "pool_pages":      int(row["pool_pages"]),
                "miss_ratio_pct":  float(row["miss_ratio_pct"]),
                "disk_io_count":   int(row["disk_io_count"]),
                "latency_ms":      float(row["latency_ms"]),
            })
    return rows


def group_data(rows):
    """Returns nested dict: data[workload][policy] = list of (buffer_pct, value) sorted."""
    data = defaultdict(lambda: defaultdict(list))
    for r in rows:
        data[r["workload"]][r["policy"]].append(r)
    # sort each list by buffer_pct
    for wl in data:
        for pol in data[wl]:
            data[wl][pol].sort(key=lambda x: x["buffer_pct"])
    return data


# --------------------------------------------------------------------------
# Figure helpers
# --------------------------------------------------------------------------

def apply_paper_style(ax, xlabel, ylabel, title, workload_key):
    """Apply consistent paper styling to an axis."""
    ax.set_title(WORKLOAD_META[workload_key]["title"],
                 fontsize=10, fontweight="bold", pad=8)
    ax.set_xlabel(xlabel, fontsize=9)
    ax.set_ylabel(ylabel, fontsize=9)
    ax.tick_params(labelsize=8)
    ax.grid(True, which="major", linestyle=":", linewidth=0.6, alpha=0.7)
    ax.grid(True, which="minor", linestyle=":", linewidth=0.3, alpha=0.4)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.set_xlim(5, 90)


def add_legend(fig, axes):
    handles = [
        Line2D([0], [0],
               color=POLICY_STYLES[p]["color"],
               linestyle=POLICY_STYLES[p]["linestyle"],
               marker=POLICY_STYLES[p]["marker"],
               markersize=6,
               linewidth=1.8,
               label=POLICY_STYLES[p]["label"])
        for p in POLICY_ORDER if p in POLICY_STYLES
    ]
    fig.legend(handles=handles, loc="lower center", ncol=3,
               fontsize=8, frameon=True, framealpha=0.9,
               bbox_to_anchor=(0.5, -0.02))


def save_fig(fig, out_dir, basename):
    for ext in ("pdf", "png"):
        path = os.path.join(out_dir, f"{basename}.{ext}")
        fig.savefig(path, bbox_inches="tight", dpi=200)
        print(f"  [saved] {path}")


# --------------------------------------------------------------------------
# Figure 1 — Miss Ratio vs. Buffer Pool Size
# --------------------------------------------------------------------------

def plot_miss_ratio(data, out_dir):
    fig, axes = plt.subplots(1, 3, figsize=(13, 4), sharey=False)
    fig.suptitle(
        "Buffer Miss Ratio (%) vs. Buffer Pool Size",
        fontsize=12, fontweight="bold", y=1.01
    )

    for ax, wl in zip(axes, WORKLOAD_ORDER):
        for pol in POLICY_ORDER:
            if pol not in data.get(wl, {}):
                continue
            rows = data[wl][pol]
            xs = [r["buffer_pct"] for r in rows]
            ys = [r["miss_ratio_pct"] for r in rows]
            st = POLICY_STYLES[pol]
            ax.plot(xs, ys,
                    color=st["color"], linestyle=st["linestyle"],
                    marker=st["marker"], markersize=6, linewidth=1.8,
                    label=st["label"])

        apply_paper_style(ax, "Buffer Pool Size (% of dataset)",
                          "Miss Ratio (%)", "", wl)
        ax.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.1f%%"))
        ax.set_xticks([10, 25, 50, 80])
        ax.set_xticklabels(["10%", "25%", "50%", "80%"])

    add_legend(fig, axes)
    fig.tight_layout()
    save_fig(fig, out_dir, "fig1_miss_ratio")
    plt.close(fig)


# --------------------------------------------------------------------------
# Figure 2 — Disk I/O Count vs. Buffer Pool Size
# --------------------------------------------------------------------------

def plot_disk_io(data, out_dir):
    fig, axes = plt.subplots(1, 3, figsize=(13, 4), sharey=False)
    fig.suptitle(
        "Disk I/O Count vs. Buffer Pool Size",
        fontsize=12, fontweight="bold", y=1.01
    )

    for ax, wl in zip(axes, WORKLOAD_ORDER):
        for pol in POLICY_ORDER:
            if pol not in data.get(wl, {}):
                continue
            rows = data[wl][pol]
            xs = [r["buffer_pct"] for r in rows]
            ys = [r["disk_io_count"] for r in rows]
            st = POLICY_STYLES[pol]
            ax.plot(xs, ys,
                    color=st["color"], linestyle=st["linestyle"],
                    marker=st["marker"], markersize=6, linewidth=1.8)

        apply_paper_style(ax, "Buffer Pool Size (% of dataset)",
                          "Disk I/O Requests", "", wl)
        ax.yaxis.set_major_formatter(mticker.FuncFormatter(
            lambda v, _: f"{int(v):,}"))
        ax.set_xticks([10, 25, 50, 80])
        ax.set_xticklabels(["10%", "25%", "50%", "80%"])

    add_legend(fig, axes)
    fig.tight_layout()
    save_fig(fig, out_dir, "fig2_disk_io")
    plt.close(fig)


# --------------------------------------------------------------------------
# Figure 3 — Execution Latency vs. Buffer Pool Size
# --------------------------------------------------------------------------

def plot_latency(data, out_dir):
    fig, axes = plt.subplots(1, 3, figsize=(13, 4), sharey=False)
    fig.suptitle(
        "Execution Latency (ms) vs. Buffer Pool Size",
        fontsize=12, fontweight="bold", y=1.01
    )

    for ax, wl in zip(axes, WORKLOAD_ORDER):
        for pol in POLICY_ORDER:
            if pol not in data.get(wl, {}):
                continue
            rows = data[wl][pol]
            xs = [r["buffer_pct"] for r in rows]
            ys = [r["latency_ms"] for r in rows]
            st = POLICY_STYLES[pol]
            ax.plot(xs, ys,
                    color=st["color"], linestyle=st["linestyle"],
                    marker=st["marker"], markersize=6, linewidth=1.8)

        apply_paper_style(ax, "Buffer Pool Size (% of dataset)",
                          "Latency (ms)", "", wl)
        ax.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.1f"))
        ax.set_xticks([10, 25, 50, 80])
        ax.set_xticklabels(["10%", "25%", "50%", "80%"])

    add_legend(fig, axes)
    fig.tight_layout()
    save_fig(fig, out_dir, "fig3_latency")
    plt.close(fig)


# --------------------------------------------------------------------------
# Figure 4 — W1 NLJ: Disk I/O vs Absolute Pool Size
#   Shows the threshold effect: when pool < inner_size (12 pages),
#   all policies do 6100 I/O. When pool ≥ inner_size, KEEP_HOT allows
#   the inner table to survive, dropping to 112 I/O.
#   Also adds a second panel: W3 normalized latency (LRU=1.0 baseline).
# --------------------------------------------------------------------------

def plot_w1_threshold_and_w3_latency(data, out_dir):
    """
    Fig 4a (left): W1 NLJ — Disk I/O vs absolute pool pages.
        Shows the dramatic threshold effect at the inner-table size boundary.
    Fig 4b (right): W3 — Normalized latency (LRU = 1.0 baseline).
        Shows Operator-Aware consistently below or equal to baselines.
    """
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))
    fig.suptitle(
        "Fig. 4 — W1: I/O Threshold Effect & W3: Normalized Latency",
        fontsize=12, fontweight="bold", y=1.01
    )

    # ---- Panel A: W1 NLJ Disk I/O vs pool_pages (absolute) ----------------
    wl_data = data.get("W1_NLJ", {})
    for pol in POLICY_ORDER:
        if pol not in wl_data:
            continue
        rows = wl_data[pol]
        xs = [r["pool_pages"] for r in rows]
        ys = [r["disk_io_count"] for r in rows]
        st = POLICY_STYLES[pol]
        ax1.plot(xs, ys,
                 color=st["color"], linestyle=st["linestyle"],
                 marker=st["marker"], markersize=7, linewidth=2.0,
                 label=st["label"])

    # Annotate the threshold
    ax1.axvline(x=12, color="#aaaaaa", linestyle=":", linewidth=1.5)
    ax1.text(12.5, ax1.get_ylim()[1] * 0.85 if ax1.get_ylim()[1] > 0 else 5000,
             "inner_size\n= 12 pages",
             fontsize=8, color="#666666", va="top")

    ax1.set_xlabel("Buffer Pool Size (pages)", fontsize=9)
    ax1.set_ylabel("Disk I/O Requests", fontsize=9)
    ax1.set_title(
        "W1 — NLJ: Disk I/O vs. Buffer Pool Size\n"
        "(threshold at inner table size = 12 pages)",
        fontsize=10, fontweight="bold", pad=8
    )
    ax1.yaxis.set_major_formatter(mticker.FuncFormatter(
        lambda v, _: f"{int(v):,}"))
    ax1.grid(True, linestyle=":", linewidth=0.6, alpha=0.7)
    ax1.spines["top"].set_visible(False)
    ax1.spines["right"].set_visible(False)
    ax1.tick_params(labelsize=8)
    ax1.legend(fontsize=8, framealpha=0.9)

    # Annotate reduction arrow
    ax1.annotate(
        "98.2% I/O\nreduction",
        xy=(12, 112), xytext=(25, 3000),
        arrowprops=dict(arrowstyle="->", color="#333333", lw=1.2),
        fontsize=8, color="#333333", ha="center",
        bbox=dict(boxstyle="round,pad=0.3", facecolor="#f0f0f0", alpha=0.8)
    )

    # ---- Panel B: W3 Normalized Latency ------------------------------------
    wl3 = data.get("W3_CONC", {})
    pcts = sorted(set(r["buffer_pct"] for rows in wl3.values() for r in rows))

    # Build latency dict per policy per pct
    lat = {}
    for pol in POLICY_ORDER:
        lat[pol] = {}
        for r in wl3.get(pol, []):
            lat[pol][r["buffer_pct"]] = r["latency_ms"]

    # Normalize by LRU latency
    lru_key = "LRU/Clock-Sweep"
    norm_pcts = [p for p in pcts if p in lat.get(lru_key, {})]

    for pol in POLICY_ORDER:
        norm_ys = []
        xs = []
        for p in norm_pcts:
            lru_lat = lat.get(lru_key, {}).get(p, None)
            pol_lat = lat.get(pol, {}).get(p, None)
            if lru_lat and pol_lat and lru_lat > 0:
                norm_ys.append(pol_lat / lru_lat)
                xs.append(p)
        if not xs:
            continue
        st = POLICY_STYLES[pol]
        ax2.plot(xs, norm_ys,
                 color=st["color"], linestyle=st["linestyle"],
                 marker=st["marker"], markersize=7, linewidth=2.0,
                 label=st["label"])

    ax2.axhline(1.0, color="#aaaaaa", linestyle="--", linewidth=1.2,
                label="LRU baseline (= 1.0)")
    ax2.set_xlabel("Buffer Pool Size (% of dataset)", fontsize=9)
    ax2.set_ylabel("Normalized Latency (LRU = 1.0)", fontsize=9)
    ax2.set_title(
        "W3 — Concurrent Scan+Join: Normalized Latency\n"
        "(< 1.0 means faster than LRU/Clock-Sweep baseline)",
        fontsize=10, fontweight="bold", pad=8
    )
    ax2.set_xticks(norm_pcts)
    ax2.set_xticklabels([f"{p}%" for p in norm_pcts])
    ax2.grid(True, linestyle=":", linewidth=0.6, alpha=0.7)
    ax2.spines["top"].set_visible(False)
    ax2.spines["right"].set_visible(False)
    ax2.tick_params(labelsize=8)
    ax2.legend(fontsize=8, framealpha=0.9)

    fig.tight_layout(rect=[0, 0, 1, 0.97])
    save_fig(fig, out_dir, "fig4_threshold_and_latency")
    plt.close(fig)


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 plot_benchmark.py benchmark_results.csv")
        sys.exit(1)

    csv_path = sys.argv[1]
    if not os.path.exists(csv_path):
        print(f"[ERROR] File not found: {csv_path}")
        sys.exit(1)

    out_dir = os.path.dirname(os.path.abspath(csv_path))
    print(f"\n[plot_benchmark] Loading: {csv_path}")
    print(f"[plot_benchmark] Output dir: {out_dir}\n")

    rows = load_csv(csv_path)
    data = group_data(rows)

    print(f"  Workloads found: {sorted(data.keys())}")
    for wl in data:
        print(f"    {wl}: {sorted(data[wl].keys())}")
    print()

    print("[Fig 1] Miss ratio curves...")
    plot_miss_ratio(data, out_dir)

    print("[Fig 2] Disk I/O curves...")
    plot_disk_io(data, out_dir)

    print("[Fig 3] Latency curves...")
    plot_latency(data, out_dir)

    print("[Fig 4] W1 I/O threshold + W3 normalized latency...")
    plot_w1_threshold_and_w3_latency(data, out_dir)

    print("\n[plot_benchmark] Done. Files generated:")
    for f in sorted(Path(out_dir).glob("fig*.p*")):
        size_kb = f.stat().st_size / 1024
        print(f"  {f.name}  ({size_kb:.1f} KB)")


if __name__ == "__main__":
    main()
