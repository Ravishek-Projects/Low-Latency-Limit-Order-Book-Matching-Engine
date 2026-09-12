#!/usr/bin/env python3
"""
plot_latency.py — Latency distribution chart for the Order Book engine.

Reads:  results/latencies.csv
Writes: results/latency_chart.png

Install deps (once):
    pip3 install matplotlib pandas numpy
"""

import sys
import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

CSV_PATH = os.path.join(os.path.dirname(__file__), "..", "results", "latencies.csv")
OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "results", "latency_chart.png")

def main():
    if not os.path.exists(CSV_PATH):
        print(f"ERROR: {CSV_PATH} not found. Run ./build/sim first.")
        sys.exit(1)

    df = pd.read_csv(CSV_PATH)
    lat = df["latency_ns"].values

    # ── Statistics ────────────────────────────────────────────────────────────
    p50  = np.percentile(lat, 50)
    p90  = np.percentile(lat, 90)
    p99  = np.percentile(lat, 99)
    p999 = np.percentile(lat, 99.9)
    mean = np.mean(lat)

    print(f"Latency Statistics (nanoseconds)")
    print(f"  Mean   : {mean:>10.1f} ns")
    print(f"  p50    : {p50:>10.1f} ns")
    print(f"  p90    : {p90:>10.1f} ns")
    print(f"  p99    : {p99:>10.1f} ns")
    print(f"  p99.9  : {p999:>10.1f} ns")
    print(f"  Max    : {np.max(lat):>10.1f} ns")
    print(f"  N      : {len(lat):>10,}")

    # ── Plot ─────────────────────────────────────────────────────────────────
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    fig.suptitle("Order Book Engine — Latency Distribution", fontsize=14, fontweight="bold")

    # Left: histogram (cap at p99.9 so outliers don't squash the chart)
    ax1 = axes[0]
    cap = np.percentile(lat, 99.9)
    clipped = lat[lat <= cap]
    ax1.hist(clipped, bins=120, color="#2196F3", edgecolor="none", alpha=0.85)
    for val, label, colour in [
        (p50,  "p50",   "#4CAF50"),
        (p99,  "p99",   "#FF5722"),
        (p999, "p99.9", "#9C27B0"),
    ]:
        ax1.axvline(val, color=colour, linewidth=1.8, linestyle="--",
                    label=f"{label} = {val:.0f} ns")
    ax1.set_xlabel("Latency (ns)", fontsize=11)
    ax1.set_ylabel("Count", fontsize=11)
    ax1.set_title("Histogram (capped at p99.9)")
    ax1.legend(fontsize=9)
    ax1.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x:,.0f}"))
    ax1.grid(axis="y", linestyle="--", alpha=0.4)

    # Right: CDF (log x-scale — the standard way HFT teams present latency)
    ax2 = axes[1]
    sorted_lat = np.sort(lat)
    cdf = np.arange(1, len(sorted_lat) + 1) / len(sorted_lat) * 100
    ax2.plot(sorted_lat, cdf, color="#2196F3", linewidth=1.5)
    for val, label, colour in [
        (p50,  "p50",   "#4CAF50"),
        (p99,  "p99",   "#FF5722"),
        (p999, "p99.9", "#9C27B0"),
    ]:
        ax2.axvline(val, color=colour, linewidth=1.5, linestyle="--",
                    label=f"{label} = {val:.0f} ns")
    ax2.set_xscale("log")
    ax2.set_xlabel("Latency (ns, log scale)", fontsize=11)
    ax2.set_ylabel("Percentile (%)", fontsize=11)
    ax2.set_title("CDF — Log Scale")
    ax2.set_ylim(0, 100)
    ax2.legend(fontsize=9)
    ax2.grid(which="both", linestyle="--", alpha=0.4)
    ax2.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x:,.0f}"))

    plt.tight_layout()
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    plt.savefig(OUT_PATH, dpi=150, bbox_inches="tight")
    print(f"\nChart saved → {OUT_PATH}")
    plt.show()

if __name__ == "__main__":
    main()
