#include <benchmark/benchmark.h>
#include "MatchingEngine.h"
#include <chrono>
#include <random>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static uint64_t next_id() {
    static uint64_t id = 1;
    return id++;
}

static uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

// ─────────────────────────────────────────────────────────────────────────────
// BM_AddLimitOrder
// Measures the latency of inserting a resting (non-crossing) order.
// The order goes into the bid at 99.0 while all asks are at 101.0 → no match.
// ─────────────────────────────────────────────────────────────────────────────
static void BM_AddLimitOrder(benchmark::State& state) {
    MatchingEngine eng;
    // Seed the ask side so the book is not empty
    for (int i = 0; i < 100; ++i)
        eng.processOrder(Order{next_id(), Side::SELL, 101.0 + i * 0.01, 100, now_ns()});

    for (auto _ : state) {
        Order o{next_id(), Side::BUY, 99.0, 10, now_ns()};
        benchmark::DoNotOptimize(eng.processOrder(o));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddLimitOrder)->Iterations(500'000)->Unit(benchmark::kNanosecond);

// ─────────────────────────────────────────────────────────────────────────────
// BM_CancelOrder
// Each iteration:
//   1. (timer paused) insert a fresh resting order
//   2. (timer running) cancel it → measures pure O(1) cancel latency
// Using PauseTiming/ResumeTiming ensures we only measure the cancel, not insert.
// Book is seeded with 10,000 other orders so the hash map is realistically sized.
// ─────────────────────────────────────────────────────────────────────────────
static void BM_CancelOrder(benchmark::State& state) {
    MatchingEngine eng;
    // Seed book with background orders at different prices
    const int SEED = 10'000;
    for (int i = 0; i < SEED; ++i)
        eng.processOrder(Order{next_id(), Side::BUY, 90.0 + i * 0.001, 50, now_ns()});

    for (auto _ : state) {
        // Add the order we are about to cancel (don't count this in the timer)
        state.PauseTiming();
        uint64_t id = next_id();
        eng.processOrder(Order{id, Side::BUY, 100.0, 10, now_ns()});
        state.ResumeTiming();

        // This is what we're actually measuring
        benchmark::DoNotOptimize(eng.cancelOrder(id));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CancelOrder)->Unit(benchmark::kNanosecond);

// ─────────────────────────────────────────────────────────────────────────────
// BM_MatchingThroughput
// Alternates between adding a bid and a sell that crosses it.
// Each iteration produces exactly one trade — measures matching throughput.
// ─────────────────────────────────────────────────────────────────────────────
static void BM_MatchingThroughput(benchmark::State& state) {
    MatchingEngine eng;
    uint32_t qty = 10;
    for (auto _ : state) {
        // Add resting bid
        eng.processOrder(Order{next_id(), Side::BUY,  100.0, qty, now_ns()});
        // Add aggressive sell that matches it
        auto trades = eng.processOrder(Order{next_id(), Side::SELL, 100.0, qty, now_ns()});
        benchmark::DoNotOptimize(trades);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MatchingThroughput)->Iterations(500'000)->Unit(benchmark::kNanosecond);

// ─────────────────────────────────────────────────────────────────────────────
// BM_MultiLevelSweep
// Each iteration:
//   - Adds `levels` resting sells across `levels` price levels
//   - Sends one large buy that sweeps all of them
// Shows latency of a multi-level sweep (worst-case for a taker)
// ─────────────────────────────────────────────────────────────────────────────
static void BM_MultiLevelSweep(benchmark::State& state) {
    MatchingEngine eng;
    const int levels = static_cast<int>(state.range(0));

    for (auto _ : state) {
        // Seed `levels` resting sells
        for (int i = 0; i < levels; ++i) {
            eng.processOrder(Order{
                next_id(), Side::SELL,
                100.0 + i * 0.01,
                10u, now_ns()
            });
        }
        // Big buy sweeps all of them
        auto trades = eng.processOrder(Order{
            next_id(), Side::BUY,
            100.0 + (levels - 1) * 0.01 + 0.001,
            static_cast<uint32_t>(10 * levels),
            now_ns()
        });
        benchmark::DoNotOptimize(trades);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MultiLevelSweep)->Arg(1)->Arg(5)->Arg(10)->Unit(benchmark::kNanosecond);

// ─────────────────────────────────────────────────────────────────────────────
// BM_RandomOrderFlow
// Simulates realistic mixed order flow:
//   70% limit orders (resting), 20% cancels, 10% market-crossing orders
// ─────────────────────────────────────────────────────────────────────────────
static void BM_RandomOrderFlow(benchmark::State& state) {
    MatchingEngine eng;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int>      action_dist(0, 9);
    std::uniform_int_distribution<uint32_t> qty_dist(1, 100);

    std::vector<uint64_t> live_ids;
    live_ids.reserve(1000);

    for (auto _ : state) {
        int action = action_dist(rng);
        if (action < 7 || live_ids.empty()) {
            // 70% → add resting order
            uint64_t id = next_id();
            live_ids.push_back(id);
            Side   side  = (action < 4) ? Side::BUY : Side::SELL;
            double price = (side == Side::BUY) ? 99.5 : 100.5; // non-crossing
            eng.processOrder(Order{id, side, price, qty_dist(rng), now_ns()});
        } else if (action < 9) {
            // 20% → cancel random live order
            int idx = std::uniform_int_distribution<int>(0, (int)live_ids.size()-1)(rng);
            eng.cancelOrder(live_ids[idx]);
            live_ids.erase(live_ids.begin() + idx);
        } else {
            // 10% → crossing order (triggers a match)
            eng.processOrder(Order{next_id(), Side::BUY, 101.0, 10, now_ns()});
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RandomOrderFlow)->Iterations(200'000)->Unit(benchmark::kNanosecond);
