#include "MarketSimulator.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <cassert>

// ─────────────────────────────────────────────────────────────────────────────
// SimStats helpers
// ─────────────────────────────────────────────────────────────────────────────
double SimStats::percentile(double pct) const {
    if (latencies.empty()) return 0.0;
    // We sort a copy — latencies is already sorted after run() completes
    auto sorted = latencies;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(pct / 100.0 * (sorted.size() - 1));
    return static_cast<double>(sorted[idx]);
}

double SimStats::mean() const {
    if (latencies.empty()) return 0.0;
    double sum = static_cast<double>(
        std::accumulate(latencies.begin(), latencies.end(), uint64_t{0}));
    return sum / static_cast<double>(latencies.size());
}

void SimStats::print() const {
    std::cout << "\n╔═══════════════════════════════════════════╗\n";
    std::cout << "║         SIMULATION RESULTS                ║\n";
    std::cout << "╠═══════════════════════════════════════════╣\n";
    std::cout << "║  Orders sent    : " << std::setw(10) << orders_sent  << "             ║\n";
    std::cout << "║  Trades done    : " << std::setw(10) << trades_done  << "             ║\n";
    std::cout << "║  Cancels done   : " << std::setw(10) << cancels_done << "             ║\n";
    std::cout << "║  Volume traded  : " << std::setw(10) << total_volume << "             ║\n";
    std::cout << "╠═══════════════════════════════════════════╣\n";
    std::cout << "║  LATENCY (nanoseconds)                    ║\n";
    std::cout << "║  Mean  : " << std::setw(8) << std::fixed << std::setprecision(1) << mean()  << " ns                    ║\n";
    std::cout << "║  p50   : " << std::setw(8) << p50()  << " ns                    ║\n";
    std::cout << "║  p90   : " << std::setw(8) << p90()  << " ns                    ║\n";
    std::cout << "║  p99   : " << std::setw(8) << p99()  << " ns                    ║\n";
    std::cout << "║  p99.9 : " << std::setw(8) << p999() << " ns                    ║\n";
    std::cout << "╚═══════════════════════════════════════════╝\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// MarketSimulator
// ─────────────────────────────────────────────────────────────────────────────
MarketSimulator::MarketSimulator(SimConfig cfg)
    : cfg_(std::move(cfg)), mid_(cfg_.initial_price) {}

// ── Price model ───────────────────────────────────────────────────────────────
// Geometric Brownian Motion step: mid *= exp(sigma * N(0,1))
void MarketSimulator::updateMid() {
    std::normal_distribution<double> noise(0.0, cfg_.volatility);
    mid_ *= std::exp(noise(rng_));
    // Snap to tick grid
    mid_ = std::round(mid_ / cfg_.tick_size) * cfg_.tick_size;
}

// ── Order factories ───────────────────────────────────────────────────────────
// Passive order: price DOES NOT cross the spread (rests in book)
Order MarketSimulator::makePassive(Side side) {
    std::uniform_int_distribution<uint32_t> qty_dist(cfg_.min_qty, cfg_.max_qty);
    // Passive buy: at or below best bid; passive sell: at or above best ask
    double half_spread = cfg_.spread_ticks * cfg_.tick_size / 2.0;
    double price = (side == Side::BUY)
                   ? mid_ - half_spread - cfg_.tick_size  // 1 tick behind bid
                   : mid_ + half_spread + cfg_.tick_size; // 1 tick behind ask
    price = std::round(price / cfg_.tick_size) * cfg_.tick_size;
    return Order{next_id_++, side, price, qty_dist(rng_), 0};
}

// Aggressive order: price CROSSES the spread (will immediately match)
Order MarketSimulator::makeAggressive(Side side) {
    std::uniform_int_distribution<uint32_t> qty_dist(cfg_.min_qty, cfg_.max_qty / 2);
    double half_spread = cfg_.spread_ticks * cfg_.tick_size / 2.0;
    double price = (side == Side::BUY)
                   ? mid_ + half_spread + cfg_.tick_size  // above ask → crosses
                   : mid_ - half_spread - cfg_.tick_size; // below bid → crosses
    price = std::round(price / cfg_.tick_size) * cfg_.tick_size;
    return Order{next_id_++, side, price, qty_dist(rng_), 0};
}

// ─────────────────────────────────────────────────────────────────────────────
// run — main simulation loop
// ─────────────────────────────────────────────────────────────────────────────
SimStats MarketSimulator::run() {
    SimStats stats;
    stats.latencies.reserve(cfg_.num_orders);

    std::uniform_real_distribution<double> action_dist(0.0, 1.0);
    std::uniform_int_distribution<int>     side_dist(0, 1);

    // Warm up: seed both sides of the book so cancels have something to hit
    for (int i = 0; i < 50; ++i) {
        Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        auto o = makePassive(side);
        live_ids_.push_back(o.id);
        engine_.processOrder(o);
    }

    for (int i = 0; i < cfg_.num_orders; ++i) {
        // Evolve the mid-price every order
        updateMid();

        double action = action_dist(rng_);
        Side   side   = static_cast<Side>(side_dist(rng_));

        auto t0 = std::chrono::high_resolution_clock::now();

        if (action < cfg_.passive_ratio) {
            // ── Passive limit order ───────────────────────────────────────────
            auto o = makePassive(side);
            live_ids_.push_back(o.id);
            engine_.processOrder(o);
            ++stats.orders_sent;

        } else if (action < cfg_.passive_ratio + cfg_.aggr_ratio) {
            // ── Aggressive (crossing) order ───────────────────────────────────
            auto o = makeAggressive(side);
            auto trades = engine_.processOrder(o);
            ++stats.orders_sent;
            stats.trades_done += trades.size();
            for (auto& t : trades) stats.total_volume += t.quantity;

        } else {
            // ── Cancel a random live order ────────────────────────────────────
            if (!live_ids_.empty()) {
                std::uniform_int_distribution<size_t> idx_dist(0, live_ids_.size() - 1);
                size_t idx = idx_dist(rng_);
                engine_.cancelOrder(live_ids_[idx]);
                live_ids_.erase(live_ids_.begin() + idx);
                ++stats.cancels_done;
            }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        stats.latencies.push_back(
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));

        // Progress snapshot every N orders
        if ((i + 1) % cfg_.snapshot_interval == 0) {
            std::cout << "  [" << std::setw(7) << (i + 1) << "] mid=$"
                      << std::fixed << std::setprecision(2) << mid_
                      << "  book orders=" << engine_.book().orderCount()
                      << "  trades=" << stats.trades_done << "\n";
        }
    }

    writeCsv(stats);
    writeBookSnapshot();
    return stats;
}

// ─────────────────────────────────────────────────────────────────────────────
// CSV output
// ─────────────────────────────────────────────────────────────────────────────
void MarketSimulator::writeCsv(const SimStats& s) const {
    std::filesystem::create_directories(cfg_.output_dir);
    std::ofstream f(cfg_.output_dir + "/latencies.csv");
    f << "latency_ns\n";
    for (auto lat : s.latencies) f << lat << "\n";
    std::cout << "  >> Latency data written to " << cfg_.output_dir << "/latencies.csv\n";
}

void MarketSimulator::writeBookSnapshot() const {
    std::filesystem::create_directories(cfg_.output_dir);
    std::ofstream f(cfg_.output_dir + "/book_snapshot.csv");
    f << "side,price,quantity\n";

    // Write top 20 levels of each side
    int cnt = 0;
    for (auto& [p, lv] : engine_.book().bids()) {
        if (cnt++ >= 20) break;
        f << "BID," << std::fixed << std::setprecision(2)
          << toDoublePrice(p) << "," << lv.total_qty << "\n";
    }
    cnt = 0;
    for (auto& [p, lv] : engine_.book().asks()) {
        if (cnt++ >= 20) break;
        f << "ASK," << std::fixed << std::setprecision(2)
          << toDoublePrice(p) << "," << lv.total_qty << "\n";
    }
    std::cout << "  >> Book snapshot written to " << cfg_.output_dir << "/book_snapshot.csv\n";
}
