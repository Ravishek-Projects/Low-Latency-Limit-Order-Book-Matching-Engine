#include <iostream>
#include <iomanip>
#include <string>
#include "MarketSimulator.h"

// Optional: pin this process to CPU core 0 to reduce scheduling jitter
// and get cleaner latency numbers.
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
static void pinToCore(int core_id = 0) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0)
        std::cout << "  [CPU affinity] Pinned to core " << core_id << "\n";
    else
        std::cout << "  [CPU affinity] Could not pin (run as root for best results)\n";
}
#else
static void pinToCore(int = 0) {}
#endif

int main(int argc, char* argv[]) {
    std::cout << "════════════════════════════════════════════════\n";
    std::cout << "   Market Simulator — Limit Order Book Engine   \n";
    std::cout << "════════════════════════════════════════════════\n\n";

    // ── Configuration ──────────────────────────────────────────────────────
    SimConfig cfg;
    cfg.initial_price      = 100.0;
    cfg.volatility         = 0.0003;   // ~0.03% per order → realistic intraday
    cfg.tick_size          = 0.01;
    cfg.spread_ticks       = 2;        // 2-cent spread
    cfg.min_qty            = 1;
    cfg.max_qty            = 500;
    cfg.num_orders         = 100'000;
    cfg.passive_ratio      = 0.60;     // 60% passive
    cfg.aggr_ratio         = 0.25;     // 25% aggressive
    // cancel_ratio = 15% (remainder)
    cfg.snapshot_interval  = 10'000;
    cfg.output_dir         = "results";

    // Override num_orders from command line: ./sim 50000
    if (argc > 1) cfg.num_orders = std::stoi(argv[1]);

    std::cout << "Config:\n";
    std::cout << "  Initial price  : $" << cfg.initial_price << "\n";
    std::cout << "  Volatility     : " << cfg.volatility * 100 << "% per order\n";
    std::cout << "  Spread         : " << cfg.spread_ticks << " ticks ("
              << cfg.spread_ticks * cfg.tick_size << ")\n";
    std::cout << "  Orders         : " << cfg.num_orders << "\n";
    std::cout << "  Mix            : " << cfg.passive_ratio * 100 << "% passive, "
              << cfg.aggr_ratio * 100 << "% aggressive, "
              << (1.0 - cfg.passive_ratio - cfg.aggr_ratio) * 100 << "% cancel\n\n";

    // ── Pin to core for clean latency measurements ─────────────────────────
    pinToCore(0);

    // ── Run ────────────────────────────────────────────────────────────────
    std::cout << "\nRunning simulation...\n";
    MarketSimulator sim(cfg);
    SimStats stats = sim.run();

    // ── Results ────────────────────────────────────────────────────────────
    stats.print();

    // ── Final book ────────────────────────────────────────────────────────
    std::cout << "\nFinal order book state:";
    sim.book().printTop(5);

    std::cout << "\nTo visualise results, run:\n";
    std::cout << "  python3 scripts/plot_latency.py\n";
    std::cout << "  python3 scripts/plot_book.py\n\n";

    return 0;
}
