#!/usr/bin/env python3
"""
plot_book.py — Order Book depth (market depth) visualisation.

Reads:  results/book_snapshot.csv
Writes: results/book_depth.png

Install deps (once):
    pip3 install matplotlib pandas numpy
"""

import sys
import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

CSV_PATH = os.path.join(os.path.dirname(__file__), "..", "results", "book_snapshot.csv")
OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "results", "book_depth.png")

def main():
    if not os.path.exists(CSV_PATH):
        print(f"ERROR: {CSV_PATH} not found. Run ./build/sim first.")
        sys.exit(1)

    df = pd.read_csv(CSV_PATH)
    bids = df[df["side"] == "BID"].sort_values("price", ascending=False)
    asks = df[df["side"] == "ASK"].sort_values("price", ascending=True)

    # ── Derived quantities ────────────────────────────────────────────────────
    # Cumulative depth: how much volume is available up to each price
    bids["cum_qty"] = bids["quantity"].cumsum()
    asks["cum_qty"] = asks["quantity"].cumsum()

    mid = (bids["price"].iloc[0] + asks["price"].iloc[0]) / 2
    spread = asks["price"].iloc[0] - bids["price"].iloc[0]

    print(f"Order Book Snapshot")
    print(f"  Best bid  : ${bids['price'].iloc[0]:.2f}  (qty={bids['quantity'].iloc[0]})")
    print(f"  Best ask  : ${asks['price'].iloc[0]:.2f}  (qty={asks['quantity'].iloc[0]})")
    print(f"  Mid price : ${mid:.3f}")
    print(f"  Spread    : ${spread:.2f}  ({spread/mid*10000:.1f} bps)")

    # ── Plot ─────────────────────────────────────────────────────────────────
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    fig.suptitle("Limit Order Book — Market Depth Snapshot", fontsize=14, fontweight="bold")

    # ── Left: Level-by-level bar chart ────────────────────────────────────────
    ax1 = axes[0]
    ax1.barh(bids["price"].astype(str), bids["quantity"],
             color="#4CAF50", alpha=0.85, label="Bids")
    ax1.barh(asks["price"].astype(str), asks["quantity"],
             color="#F44336", alpha=0.85, label="Asks")
    ax1.set_xlabel("Quantity", fontsize=11)
    ax1.set_ylabel("Price ($)", fontsize=11)
    ax1.set_title("Quantity at Each Price Level")
    ax1.legend(fontsize=10)
    ax1.grid(axis="x", linestyle="--", alpha=0.4)
    ax1.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x:,.0f}"))

    # ── Right: Cumulative depth chart ─────────────────────────────────────────
    ax2 = axes[1]
    ax2.fill_between(bids["price"], bids["cum_qty"],
                     step="post", color="#4CAF50", alpha=0.4, label="Bid depth")
    ax2.step(bids["price"], bids["cum_qty"],
             where="post", color="#2E7D32", linewidth=1.5)

    ax2.fill_between(asks["price"], asks["cum_qty"],
                     step="post", color="#F44336", alpha=0.4, label="Ask depth")
    ax2.step(asks["price"], asks["cum_qty"],
             where="post", color="#B71C1C", linewidth=1.5)

    ax2.axvline(mid, color="black", linewidth=1.2, linestyle=":", label=f"Mid ${mid:.2f}")
    ax2.set_xlabel("Price ($)", fontsize=11)
    ax2.set_ylabel("Cumulative Quantity", fontsize=11)
    ax2.set_title("Cumulative Depth (Market Impact)")
    ax2.legend(fontsize=10)
    ax2.grid(linestyle="--", alpha=0.4)
    ax2.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x:,.0f}"))

    plt.tight_layout()
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    plt.savefig(OUT_PATH, dpi=150, bbox_inches="tight")
    print(f"\nChart saved → {OUT_PATH}")
    plt.show()

if __name__ == "__main__":
    main()
