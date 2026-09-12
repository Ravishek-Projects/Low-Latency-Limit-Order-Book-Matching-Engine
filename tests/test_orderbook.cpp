#include <gtest/gtest.h>
#include "MatchingEngine.h"

// Helper: build an order with a simple counter timestamp
static uint64_t ts_counter = 0;
static Order makeOrder(uint64_t id, Side side, double price, uint32_t qty) {
    return Order{id, side, price, qty, ++ts_counter};
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. No match — orders rest in book
// ─────────────────────────────────────────────────────────────────────────────
TEST(OrderBook, NoMatchRestingOrders) {
    MatchingEngine eng;
    // Buy at 100, sell at 101 → no crossing
    auto t1 = eng.processOrder(makeOrder(1, Side::BUY,  100.0, 10));
    auto t2 = eng.processOrder(makeOrder(2, Side::SELL, 101.0, 10));

    EXPECT_TRUE(t1.empty());
    EXPECT_TRUE(t2.empty());
    EXPECT_EQ(eng.book().orderCount(), 2u);
    EXPECT_EQ(toDoublePrice(*eng.book().bestBid()), 100.0);
    EXPECT_EQ(toDoublePrice(*eng.book().bestAsk()), 101.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Exact match — full fill, both orders removed
// ─────────────────────────────────────────────────────────────────────────────
TEST(MatchingEngine, ExactFullMatch) {
    MatchingEngine eng;
    eng.processOrder(makeOrder(1, Side::BUY,  100.0, 50));
    auto trades = eng.processOrder(makeOrder(2, Side::SELL, 100.0, 50));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].maker_id, 1u);
    EXPECT_EQ(trades[0].taker_id, 2u);
    EXPECT_EQ(trades[0].quantity, 50u);
    EXPECT_EQ(toDoublePrice(trades[0].price), 100.0);
    EXPECT_EQ(eng.book().orderCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Partial fill — buyer has more quantity than the resting sell
// ─────────────────────────────────────────────────────────────────────────────
TEST(MatchingEngine, PartialFillBuySide) {
    MatchingEngine eng;
    eng.processOrder(makeOrder(1, Side::SELL, 100.0, 30));
    auto trades = eng.processOrder(makeOrder(2, Side::BUY, 100.0, 100));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 30u);   // only 30 filled
    // Remaining 70 bid should be in book
    EXPECT_EQ(eng.book().orderCount(), 1u);
    EXPECT_TRUE(eng.book().hasBids());
    EXPECT_FALSE(eng.book().hasAsks());
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Partial fill — seller has more quantity than the resting buy
// ─────────────────────────────────────────────────────────────────────────────
TEST(MatchingEngine, PartialFillSellSide) {
    MatchingEngine eng;
    eng.processOrder(makeOrder(1, Side::BUY,  100.0, 20));
    auto trades = eng.processOrder(makeOrder(2, Side::SELL, 100.0, 80));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].quantity, 20u);
    EXPECT_EQ(eng.book().orderCount(), 1u);
    EXPECT_TRUE(eng.book().hasAsks());
    EXPECT_FALSE(eng.book().hasBids());
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Multi-level sweep — aggressive order sweeps multiple price levels
// ─────────────────────────────────────────────────────────────────────────────
TEST(MatchingEngine, MultiLevelSweep) {
    MatchingEngine eng;
    // Three sell orders at different prices
    eng.processOrder(makeOrder(1, Side::SELL, 100.0, 10));
    eng.processOrder(makeOrder(2, Side::SELL, 100.5, 10));
    eng.processOrder(makeOrder(3, Side::SELL, 101.0, 10));

    // Big buy that crosses all three
    auto trades = eng.processOrder(makeOrder(4, Side::BUY, 101.0, 30));

    ASSERT_EQ(trades.size(), 3u);
    // Trades execute at resting (maker) prices, lowest ask first
    EXPECT_EQ(toDoublePrice(trades[0].price), 100.0);
    EXPECT_EQ(toDoublePrice(trades[1].price), 100.5);
    EXPECT_EQ(toDoublePrice(trades[2].price), 101.0);
    EXPECT_EQ(eng.book().orderCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Time priority — earlier order fills first within same price level
// ─────────────────────────────────────────────────────────────────────────────
TEST(MatchingEngine, TimePriority) {
    MatchingEngine eng;
    // Two bids at same price; order 1 arrived first
    eng.processOrder(makeOrder(1, Side::BUY, 100.0, 10));
    eng.processOrder(makeOrder(2, Side::BUY, 100.0, 10));

    // Sell only 10 → should fill against order 1 (earlier)
    auto trades = eng.processOrder(makeOrder(3, Side::SELL, 100.0, 10));

    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].maker_id, 1u);   // order 1 is the maker
    EXPECT_EQ(trades[0].taker_id, 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. Cancel — order removed, no longer fillable
// ─────────────────────────────────────────────────────────────────────────────
TEST(MatchingEngine, CancelOrder) {
    MatchingEngine eng;
    eng.processOrder(makeOrder(1, Side::BUY, 100.0, 50));
    EXPECT_EQ(eng.book().orderCount(), 1u);

    bool cancelled = eng.cancelOrder(1);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(eng.book().orderCount(), 0u);
    EXPECT_FALSE(eng.book().hasBids());
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Cancel non-existent order returns false
// ─────────────────────────────────────────────────────────────────────────────
TEST(MatchingEngine, CancelNonExistent) {
    MatchingEngine eng;
    EXPECT_FALSE(eng.cancelOrder(999));
}

// ─────────────────────────────────────────────────────────────────────────────
// 9. Price priority — buy at better price fills before lower bid
// ─────────────────────────────────────────────────────────────────────────────
TEST(MatchingEngine, PricePriority) {
    MatchingEngine eng;
    // Two bids: 101 and 100; sell should match against 101 first
    eng.processOrder(makeOrder(1, Side::BUY, 100.0, 10));
    eng.processOrder(makeOrder(2, Side::BUY, 101.0, 10));

    auto trades = eng.processOrder(makeOrder(3, Side::SELL, 100.0, 10));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].maker_id, 2u);             // higher bid fills first
    EXPECT_EQ(toDoublePrice(trades[0].price), 101.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. Spread calculation
// ─────────────────────────────────────────────────────────────────────────────
TEST(OrderBook, Spread) {
    MatchingEngine eng;
    eng.processOrder(makeOrder(1, Side::BUY,   99.0, 10));
    eng.processOrder(makeOrder(2, Side::SELL, 101.0, 10));

    auto spread = eng.book().spread();
    ASSERT_TRUE(spread.has_value());
    EXPECT_NEAR(toDoublePrice(*spread), 2.0, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────────────
// 11. Cancel resting order then fill → no trade with cancelled order
// ─────────────────────────────────────────────────────────────────────────────
TEST(MatchingEngine, CancelThenNoFill) {
    MatchingEngine eng;
    eng.processOrder(makeOrder(1, Side::BUY, 100.0, 50));
    eng.cancelOrder(1);
    auto trades = eng.processOrder(makeOrder(2, Side::SELL, 100.0, 50));
    EXPECT_TRUE(trades.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// 12. Multiple partial fills across a single large resting order
// ─────────────────────────────────────────────────────────────────────────────
TEST(MatchingEngine, MultiplePartialFillsSameLevel) {
    MatchingEngine eng;
    // Large resting sell of 100
    eng.processOrder(makeOrder(1, Side::SELL, 100.0, 100));

    // Three small buys fill it down
    auto t1 = eng.processOrder(makeOrder(2, Side::BUY, 100.0, 30));
    auto t2 = eng.processOrder(makeOrder(3, Side::BUY, 100.0, 30));
    auto t3 = eng.processOrder(makeOrder(4, Side::BUY, 100.0, 40));

    ASSERT_EQ(t1[0].quantity, 30u);
    ASSERT_EQ(t2[0].quantity, 30u);
    ASSERT_EQ(t3[0].quantity, 40u);
    EXPECT_EQ(eng.book().orderCount(), 0u);
}
