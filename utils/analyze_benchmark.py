#!/usr/bin/env python3
"""
analyze_benchmark.py — análisis completo del benchmark OA
Uso: python3 utils/analyze_benchmark.py benchmark_results.csv
"""

import sys
import csv
import os
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    HAS_MPL = True
except ImportError:
    HAS_MPL = False
    print("[WARN] matplotlib no disponible — solo análisis textual")

CSV_FILE = sys.argv[1] if len(sys.argv) > 1 else "benchmark_results.csv"

# ── Cargar datos ──────────────────────────────────────────────────────────────
rows = []
with open(CSV_FILE) as f:
    reader = csv.DictReader(f)
    for row in reader:
        rows.append({
            "workload":   row["workload"],
            "policy":     row["policy"],
            "pool_pages": int(row["pool_pages"]),
            "buffer_pct": float(row["buffer_pct"]),
            "miss_pct":   float(row["miss_ratio_pct"]),
            "misses":     int(row["page_misses"]),
            "writes":     int(row["disk_writes"]),
            "io_total":   int(row["disk_io_total"]),
            "latency_ms": float(row["latency_ms"]),
        })

POLICIES = ["LRU/Clock-Sweep", "2Q", "Operator-Aware"]
COLORS   = {"LRU/Clock-Sweep": "#e74c3c", "2Q": "#3498db", "Operator-Aware": "#2ecc71"}
WORKLOADS = sorted(set(r["workload"] for r in rows))

def by_workload(wl):
    return [r for r in rows if r["workload"] == wl]

def pivot(wl_rows, metric):
    """Returns {policy: [(pool_pages, value)]} sorted by pool_pages."""
    out = defaultdict(list)
    for r in wl_rows:
        out[r["policy"]].append((r["pool_pages"], r[metric]))
    for p in out:
        out[p].sort()
    return out

# ── Análisis textual ──────────────────────────────────────────────────────────
print("\n" + "="*80)
print("  ANÁLISIS DE RESULTADOS — OPERATOR-AWARE BUFFER MANAGEMENT")
print("="*80)

for wl in WORKLOADS:
    wl_rows = by_workload(wl)
    pools = sorted(set(r["pool_pages"] for r in wl_rows))
    print(f"\n{'─'*80}")
    print(f"  WORKLOAD: {wl}")
    print(f"{'─'*80}")

    any_diff = False
    for pool in pools:
        group = {r["policy"]: r for r in wl_rows if r["pool_pages"] == pool}
        misses  = {p: group[p]["misses"]     for p in group}
        latency = {p: group[p]["latency_ms"] for p in group}

        oa_miss  = misses.get("Operator-Aware", 0)
        lru_miss = misses.get("LRU/Clock-Sweep", 0)
        twoq_miss= misses.get("2Q", 0)

        oa_lat   = latency.get("Operator-Aware", 0)
        lru_lat  = latency.get("LRU/Clock-Sweep", 0)

        miss_diff_lru  = ((lru_miss  - oa_miss) / max(lru_miss, 1) * 100)
        miss_diff_twoq = ((twoq_miss - oa_miss) / max(twoq_miss, 1) * 100)

        flag = ""
        if abs(miss_diff_lru) > 1 or abs(miss_diff_twoq) > 1:
            flag = " ← DIFERENCIA DETECTADA"
            any_diff = True

        print(f"  pool={pool:3d} pgs | "
              f"LRU:{lru_miss:6d} misses  "
              f"2Q:{twoq_miss:6d} misses  "
              f"OA:{oa_miss:6d} misses  "
              f"| OA vs LRU: {miss_diff_lru:+.1f}%{flag}")

    if not any_diff:
        print("  ⚠  Todas las políticas producen resultados IDÉNTICOS en este workload.")
    else:
        print("  ✓  Diferencias entre políticas detectadas.")

# ── Diagnóstico global ────────────────────────────────────────────────────────
print("\n" + "="*80)
print("  DIAGNÓSTICO GLOBAL")
print("="*80)

w1 = by_workload("W1_NLJ")
pools_w1 = sorted(set(r["pool_pages"] for r in w1))
print("\nW1 (NLJ) — ¿KEEP_HOT funciona ahora?")
for pool in pools_w1:
    group = {r["policy"]: r for r in w1 if r["pool_pages"] == pool}
    oa   = group.get("Operator-Aware",   {}).get("misses", 0)
    lru  = group.get("LRU/Clock-Sweep",  {}).get("misses", 0)
    tq   = group.get("2Q",               {}).get("misses", 0)
    diff_lru  = lru - oa
    diff_twoq = tq  - oa
    status = "✓ OA GANA" if (diff_lru > 0 or diff_twoq > 0) else ("= EMPATE")
    print(f"  pool={pool:3d}: LRU={lru:6d}  2Q={tq:6d}  OA={oa:6d}  "
          f"(OA ahorra vs LRU: {diff_lru:+6d} misses)  [{status}]")

print("\nZona crítica (pool=12,13,14) — debería mostrar máxima ventaja de OA:")
for pool in [12, 13, 14]:
    group = {r["policy"]: r for r in w1 if r["pool_pages"] == pool}
    if not group:
        continue
    oa  = group.get("Operator-Aware",  {}).get("misses", "N/A")
    lru = group.get("LRU/Clock-Sweep", {}).get("misses", "N/A")
    tq  = group.get("2Q",              {}).get("misses", "N/A")
    print(f"  pool={pool}: LRU={lru}  2Q={tq}  OA={oa}")

print("\nW2 (HJ) — 100% miss ratio en todas las políticas:")
w2 = by_workload("W2_HJ")
all_100 = all(r["miss_pct"] >= 99.9 for r in w2)
if all_100:
    print("  ⚠  W2 sigue con 100% miss en TODAS — el HashJoin es single-pass build.")
    print("     Sin re-probe no hay rescans: KEEP_HOT no tiene efecto estructural aquí.")
    print("     Causa raíz residual: W2 necesita un build+re-probe para que OA sea observable.")
else:
    print("  ✓  Diferencias detectadas en W2.")

print("\nW4 (Tight NLJ, pool=inner+2) — máxima presión:")
w4 = by_workload("W4_TIGHT_NLJ")
for r in w4:
    print(f"  {r['policy']:20s}: misses={r['misses']:6d}  miss_ratio={r['miss_pct']:.2f}%  "
          f"latency={r['latency_ms']:.2f}ms")
w4_oa  = next((r["misses"] for r in w4 if r["policy"] == "Operator-Aware"), None)
w4_lru = next((r["misses"] for r in w4 if r["policy"] == "LRU/Clock-Sweep"), None)
if w4_oa is not None and w4_lru is not None:
    if w4_lru > w4_oa:
        print(f"  ✓  OA reduce misses en {w4_lru - w4_oa} vs LRU en W4")
    else:
        print("  ⚠  W4 empata — pool=inner+2 aún puede no crear presión suficiente")

# ── Gráficas ──────────────────────────────────────────────────────────────────
if not HAS_MPL:
    print("\n[INFO] Instala matplotlib: pip install matplotlib")
    sys.exit(0)

OUT_DIR = os.path.dirname(os.path.abspath(CSV_FILE))
fig, axes = plt.subplots(2, 2, figsize=(16, 12))
fig.suptitle("Operator-Aware Buffer Management — Benchmark Results\n"
             "(post-fix: streaming SeqScan + zona crítica 13-14 pgs + misses/writes separados)",
             fontsize=13, fontweight="bold")

def plot_metric(ax, wl, metric, ylabel, title, log_y=False):
    data = pivot(by_workload(wl), metric)
    for pol in POLICIES:
        if pol not in data:
            continue
        xs, ys = zip(*data[pol])
        ax.plot(xs, ys, marker="o", label=pol, color=COLORS[pol], linewidth=2.5, markersize=7)
    ax.set_xlabel("Pool size (pages)", fontsize=10)
    ax.set_ylabel(ylabel, fontsize=10)
    ax.set_title(title, fontsize=10)
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)
    if log_y:
        ax.set_yscale("log")
    # Sombra zona crítica
    ax.axvspan(12, 14, alpha=0.10, color="gold")
    ax.axvline(12, color="gray", linestyle="--", alpha=0.5, linewidth=1)
    ax.axvline(14, color="gray", linestyle="--", alpha=0.5, linewidth=1)

# W1: page misses vs pool_size (log scale)
ax = axes[0, 0]
plot_metric(ax, "W1_NLJ", "misses", "Page Misses (log)",
            "W1 NLJ — Page Misses vs Pool Size\n(zona amarilla = pool∈[12,14], zona crítica)", log_y=True)
ylim = ax.get_ylim()
ax.text(13, ylim[0] * 1.5, "critical\nzone", ha="center", fontsize=7, color="goldenrod", fontstyle="italic")

# W1: latency vs pool_size
ax = axes[0, 1]
plot_metric(ax, "W1_NLJ", "latency_ms", "Latency (ms)",
            "W1 NLJ — Latency vs Pool Size")

# W2: misses (bar chart — should all be equal)
ax = axes[1, 0]
w2_data = by_workload("W2_HJ")
pools_w2 = sorted(set(r["pool_pages"] for r in w2_data))
x = list(range(len(pools_w2)))
width = 0.25
for i, pol in enumerate(POLICIES):
    ys = [next((r["misses"] for r in w2_data
                if r["pool_pages"] == p and r["policy"] == pol), 0) for p in pools_w2]
    ax.bar([xi + i * width for xi in x], ys, width, label=pol,
           color=COLORS[pol], alpha=0.8, edgecolor="white")
ax.set_xticks([xi + width for xi in x])
ax.set_xticklabels([str(p) for p in pools_w2])
ax.set_xlabel("Pool size (pages)", fontsize=10)
ax.set_ylabel("Page Misses", fontsize=10)
ax.set_title("W2 Hash Join — Page Misses\n(single-pass build: no rescans → KEEP_HOT sin efecto estructural)", fontsize=10)
ax.legend(fontsize=8)
ax.grid(True, alpha=0.3, axis="y")

# W4: tight NLJ bar + latency line
ax = axes[1, 1]
w4_data = by_workload("W4_TIGHT_NLJ")
if w4_data:
    pol_labels = [r["policy"] for r in w4_data]
    pol_misses  = [r["misses"]     for r in w4_data]
    pol_latency = [r["latency_ms"] for r in w4_data]
    colors_bar  = [COLORS.get(p, "#aaa") for p in pol_labels]
    bars = ax.bar(pol_labels, pol_misses, color=colors_bar, alpha=0.85, edgecolor="white", zorder=3)
    ax.set_ylabel("Page Misses", fontsize=10)
    ax2 = ax.twinx()
    ax2.plot(pol_labels, pol_latency, "D--", color="black", markersize=9,
             label="Latency", linewidth=2, zorder=4)
    ax2.set_ylabel("Latency (ms)", fontsize=10)
    pool_size = w4_data[0]["pool_pages"] if w4_data else "?"
    ax.set_title(f"W4 Tight NLJ — pool={pool_size} pgs (inner+2)\n"
                 "máxima presión: 2 frames libres para outer de 200 pgs", fontsize=10)
    ax.grid(True, alpha=0.3, axis="y", zorder=0)
    for bar, val in zip(bars, pol_misses):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 1,
                str(val), ha="center", va="bottom", fontsize=10, fontweight="bold")
    handles = [mpatches.Patch(color=COLORS[p], label=p) for p in POLICIES if p in COLORS]
    handles.append(plt.Line2D([0], [0], color="black", marker="D", linestyle="--", label="Latency (ms)"))
    ax.legend(handles=handles, fontsize=7, loc="upper right")
else:
    ax.text(0.5, 0.5, "No W4 data", ha="center", va="center", transform=ax.transAxes)

plt.tight_layout()
out_path = os.path.join(OUT_DIR, "benchmark_analysis.png")
plt.savefig(out_path, dpi=150, bbox_inches="tight")
print(f"\n[PNG] Gráficas guardadas en: {out_path}")
print("="*80 + "\n")
