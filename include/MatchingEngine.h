#pragma once

#include <vector>
#include "OrderBook.h"
#include "Order.h"

// ─────────────────────────────────────────────────────────────────────────────
// MatchingEngine owns the OrderBook and processes incoming orders.
//
// Algorithm (price-time priority):
//   BUY  order: match against lowest asks first (ascending price)
//   SELL order: match against highest bids first (descending price)
//
// Within the same price level, the FIFO queue (std::list) ensures that
// the earliest resting order fills first — that is "time priority".
// ─────────────────────────────────────────────────────────────────────────────
class MatchingEngine {
public:
    // Process an incoming order; may produce zero or more trades.
    // Any unfilled residual is rested in the book.
    std::vector<Trade> processOrder(Order order);

    // Direct cancel (no matching attempt)
    bool cancelOrder(uint64_t order_id) {
        return book_.cancelOrder(order_id);
    }

    const OrderBook& book() const { return book_; }

    // Statistics
    uint64_t totalOrdersProcessed() const { return orders_processed_; }
    uint64_t totalTradesExecuted()  const { return trades_executed_; }

private:
    OrderBook book_;
    uint64_t  orders_processed_ = 0;
    uint64_t  trades_executed_  = 0;

    std::vector<Trade> matchBuy (Order& order);
    std::vector<Trade> matchSell(Order& order);
};
