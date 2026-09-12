#pragma once

#include <vector>
#include <string>
#include <random>
#include <cstdint>
#include "MatchingEngine.h"

// ─────────────────────────────────────────────────────────────────────────────
// Config — defined at namespace scope so GCC 11 can use it as a default arg.
// (Nested structs with default member initializers cannot be used as default
//  function arguments while the enclosing class is still incomplete.)
// ─────────────────────────────────────────────────────────────────────────────
struct SimConfig {
    double      initial_price      = 100.0;
    double      volatility         = 0.0002;  // per-order mid-price sigma
    double      tick_size          = 0.01;
    int         spread_ticks       = 2;       // bid-ask spread in ticks
    uint32_t    min_qty            = 1;
    uint32_t    max_qty            = 200;
    int         num_orders         = 100'000;
    double      passive_ratio      = 0.60;    // 60% passive limit orders
    double      aggr_ratio         = 0.25;    // 25% aggressive (crossing) orders
    // cancel_ratio = 1 - passive_ratio - aggr_ratio = 15%
    int         snapshot_interval  = 10'000;  // print book every N orders
    std::string output_dir         = "results";
};

// ─────────────────────────────────────────────────────────────────────────────
// SimStats — collected after a simulation run
// ─────────────────────────────────────────────────────────────────────────────
struct SimStats {
    uint64_t orders_sent  = 0;
    uint64_t trades_done  = 0;
    uint64_t cancels_done = 0;
    uint64_t total_volume = 0;

    std::vector<uint64_t> latencies;   // per-operation time in nanoseconds

    double percentile(double pct) const;   // pct in [0, 100]
    double p50()  const { return percentile(50.0);  }
    double p90()  const { return percentile(90.0);  }
    double p99()  const { return percentile(99.0);  }
    double p999() const { return percentile(99.9);  }
    double mean() const;

    void print() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// MarketSimulator — generates realistic synthetic order flow
//
// Price model: geometric random walk  mid(t+1) = mid(t) * exp(σ * ε)
//   where ε ~ N(0,1).
//
// Output files (in cfg.output_dir/):
//   latencies.csv      — per-order processing time
//   book_snapshot.csv  — final order book state
// ─────────────────────────────────────────────────────────────────────────────
class MarketSimulator {
public:
    explicit MarketSimulator(SimConfig cfg = SimConfig{});

    SimStats run();

    const OrderBook& book() const { return engine_.book(); }

private:
    SimConfig       cfg_;
    MatchingEngine  engine_;
    std::mt19937_64 rng_{std::random_device{}()};
    double          mid_;
    uint64_t        next_id_ = 1;

    std::vector<uint64_t> live_ids_;

    void  updateMid();
    Order makePassive   (Side side);
    Order makeAggressive(Side side);
    void  writeCsv          (const SimStats& s) const;
    void  writeBookSnapshot ()                  const;
};
