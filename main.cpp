#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <chrono>
#include "MatchingEngine.h"

// ─────────────────────────────────────────────────────────────────────────────
// Simple demo / manual test:
//   1. Print an initial order book snapshot
//   2. Simulate 20 random orders and print every trade
//   3. Print final book state
// ─────────────────────────────────────────────────────────────────────────────

static uint64_t g_id = 1;
static uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

int main() {
    MatchingEngine engine;

    std::cout << "════════════════════════════════════════\n";
    std::cout << "   Limit Order Book + Matching Engine   \n";
    std::cout << "════════════════════════════════════════\n\n";

    // ── Seed the book with resting orders ────────────────────────────────────
    std::cout << ">> Seeding resting orders...\n";

    // BID side (descending prices)
    engine.processOrder(Order{g_id++, Side::BUY,  100.00, 200, now_ns()});
    engine.processOrder(Order{g_id++, Side::BUY,   99.50, 150, now_ns()});
    engine.processOrder(Order{g_id++, Side::BUY,   99.50, 100, now_ns()}); // same price, later
    engine.processOrder(Order{g_id++, Side::BUY,   99.00, 300, now_ns()});
    engine.processOrder(Order{g_id++, Side::BUY,   98.50,  50, now_ns()});

    // ASK side (ascending prices)
    engine.processOrder(Order{g_id++, Side::SELL, 101.00, 100, now_ns()});
    engine.processOrder(Order{g_id++, Side::SELL, 101.50, 200, now_ns()});
    engine.processOrder(Order{g_id++, Side::SELL, 102.00, 150, now_ns()});
    engine.processOrder(Order{g_id++, Side::SELL, 102.50,  75, now_ns()});

    engine.book().printTop(5);

    // ── Incoming aggressive orders ────────────────────────────────────────────
    std::cout << "\n>> Processing aggressive orders...\n\n";

    auto printTrades = [](const std::vector<Trade>& trades, uint64_t order_id) {
        if (trades.empty()) {
            std::cout << "   Order #" << order_id << " → rested in book (no match)\n";
        } else {
            for (auto& t : trades) {
                std::cout << "   TRADE  maker=" << t.maker_id
                          << "  taker=" << t.taker_id
                          << "  price=" << std::fixed << std::setprecision(2)
                          << toDoublePrice(t.price)
                          << "  qty=" << t.quantity << "\n";
            }
        }
    };

    // 1. Buy that crosses best ask (101.00) exactly
    std::cout << "[1] BUY  qty=100  price=101.00\n";
    printTrades(engine.processOrder(Order{g_id++, Side::BUY,  101.00, 100, now_ns()}), g_id-1);

    // 2. Sell that crosses best bid (100.00)
    std::cout << "\n[2] SELL qty=150  price=100.00\n";
    printTrades(engine.processOrder(Order{g_id++, Side::SELL, 100.00, 150, now_ns()}), g_id-1);

    // 3. Big buy that sweeps multiple ask levels
    std::cout << "\n[3] BUY  qty=400  price=102.50  (sweeps multiple levels)\n";
    printTrades(engine.processOrder(Order{g_id++, Side::BUY,  102.50, 400, now_ns()}), g_id-1);

    // 4. Non-crossing order — rests in book
    std::cout << "\n[4] BUY  qty=50   price=97.00  (non-crossing, rests)\n";
    printTrades(engine.processOrder(Order{g_id++, Side::BUY,   97.00,  50, now_ns()}), g_id-1);

    // 5. Cancel the order we just added
    uint64_t cancel_id = g_id - 1;
    std::cout << "\n[5] CANCEL order #" << cancel_id << "\n";
    bool ok = engine.cancelOrder(cancel_id);
    std::cout << "   Cancel " << (ok ? "SUCCESS" : "FAILED") << "\n";

    // ── Final book state ──────────────────────────────────────────────────────
    std::cout << "\n>> Final book state:";
    engine.book().printTop(5);

    std::cout << "\n>> Stats:\n";
    std::cout << "   Orders processed : " << engine.totalOrdersProcessed() << "\n";
    std::cout << "   Trades executed  : " << engine.totalTradesExecuted()  << "\n";
    std::cout << "   Orders in book   : " << engine.book().orderCount()    << "\n";

    return 0;
}
