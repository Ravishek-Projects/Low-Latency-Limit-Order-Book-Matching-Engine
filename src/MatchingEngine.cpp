#include "MatchingEngine.h"

// ─────────────────────────────────────────────────────────────────────────────
// processOrder — entry point; returns all trades generated
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Trade> MatchingEngine::processOrder(Order order) {
    ++orders_processed_;

    // Fast-path: zero-quantity order is a no-op
    if (order.quantity == 0) [[unlikely]] return {};

    std::vector<Trade> trades =
        (order.side == Side::BUY) ? matchBuy(order) : matchSell(order);

    // Rest any unfilled residual in the book
    if (order.quantity > 0) [[likely]] book_.addOrder(order);

    trades_executed_ += trades.size();
    return trades;
}

// ─────────────────────────────────────────────────────────────────────────────
// matchBuy — aggressive buy sweeps the ask side upward.
//
// [[unlikely]] on the price check: once we enter the loop, we expect to match
// (otherwise why did we enter?).  No match means the loop body rarely runs
// past the first check — compiling the break path as cold code is correct.
//
// [[likely]] on full-fill: statistically, most individual resting orders get
// fully consumed rather than partially.  Placing the pop_front path in the
// hot section improves instruction cache utilisation.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Trade> MatchingEngine::matchBuy(Order& order) {
    std::vector<Trade> trades;

    while (order.quantity > 0 && book_.hasAsks()) {
        auto& [best_price, level] = *book_.asks().begin();

        if (best_price > order.price) [[unlikely]] break;  // no cross

        auto& resting   = level.orders.front();
        uint32_t fill   = std::min(order.quantity, resting.quantity);

        trades.push_back({resting.id, order.id, best_price, fill});
        order.quantity          -= fill;
        const uint32_t remaining = resting.quantity - fill;

        if (remaining == 0) [[likely]] {
            book_.popFront(Side::SELL, best_price);   // full fill → remove
        } else {
            book_.reduceFront(Side::SELL, best_price, fill);  // partial fill
        }
    }
    return trades;
}

// ─────────────────────────────────────────────────────────────────────────────
// matchSell — mirror image: aggressive sell sweeps the bid side downward.
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Trade> MatchingEngine::matchSell(Order& order) {
    std::vector<Trade> trades;

    while (order.quantity > 0 && book_.hasBids()) {
        auto& [best_price, level] = *book_.bids().begin();

        if (best_price < order.price) [[unlikely]] break;  // no cross

        auto& resting   = level.orders.front();
        uint32_t fill   = std::min(order.quantity, resting.quantity);

        trades.push_back({resting.id, order.id, best_price, fill});
        order.quantity          -= fill;
        const uint32_t remaining = resting.quantity - fill;

        if (remaining == 0) [[likely]] {
            book_.popFront(Side::BUY, best_price);
        } else {
            book_.reduceFront(Side::BUY, best_price, fill);
        }
    }
    return trades;
}
