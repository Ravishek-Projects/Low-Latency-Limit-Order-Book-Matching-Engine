#include "OrderBook.h"
#include <iostream>
#include <iomanip>

// ─────────────────────────────────────────────────────────────────────────────
// addOrder — insert a resting order into the correct side & price level.
//
// try_emplace is used instead of emplace:
//   • If the price level already exists → returns existing iterator, no alloc.
//   • If the price level is new → constructs PriceLevel(price, &pool_)
//     IN-PLACE using placement-new inside the map node.
//     No temporary is created; no move constructor is invoked.
// ─────────────────────────────────────────────────────────────────────────────
void OrderBook::addOrder(const Order& order) {
    if (order.quantity == 0) [[unlikely]] return;

    if (order.side == Side::BUY) {
        // try_emplace(key, ctor_args...) — passes (price, &pool_) to PriceLevel ctor
        auto [it, _] = bids_.try_emplace(order.price, order.price, &pool_);
        it->second.push(order);
        order_map_[order.id] = {Side::BUY, order.price,
                                 std::prev(it->second.orders.end())};
    } else {
        auto [it, _] = asks_.try_emplace(order.price, order.price, &pool_);
        it->second.push(order);
        order_map_[order.id] = {Side::SELL, order.price,
                                 std::prev(it->second.orders.end())};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// cancelOrder — O(1) via stored iterator.
//
// order_map_ lookup:  O(1) hash map
// level erase:        O(1) list erase by iterator (node returned to pool_)
// level cleanup:      O(log N) map erase if level becomes empty
// ─────────────────────────────────────────────────────────────────────────────
bool OrderBook::cancelOrder(uint64_t order_id) {
    auto map_it = order_map_.find(order_id);
    if (map_it == order_map_.end()) [[unlikely]] return false;

    auto& loc = map_it->second;

    if (loc.side == Side::BUY) {
        auto level_it = bids_.find(loc.price);
        if (level_it != bids_.end()) [[likely]] {
            level_it->second.erase(loc.it);            // O(1), returns node to pool_
            if (level_it->second.empty()) [[unlikely]]
                bids_.erase(level_it);
        }
    } else {
        auto level_it = asks_.find(loc.price);
        if (level_it != asks_.end()) [[likely]] {
            level_it->second.erase(loc.it);
            if (level_it->second.empty()) [[unlikely]]
                asks_.erase(level_it);
        }
    }

    order_map_.erase(map_it);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// reduceFront — partial fill of the oldest order at a price level
// ─────────────────────────────────────────────────────────────────────────────
void OrderBook::reduceFront(Side side, int64_t price, uint32_t qty) {
    if (side == Side::BUY) {
        auto it = bids_.find(price);
        if (it != bids_.end()) [[likely]] it->second.reduceFront(qty);
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end()) [[likely]] it->second.reduceFront(qty);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// popFront — fully remove the oldest order at a price level (full fill)
// ─────────────────────────────────────────────────────────────────────────────
void OrderBook::popFront(Side side, int64_t price) {
    if (side == Side::BUY) {
        auto it = bids_.find(price);
        if (it == bids_.end()) [[unlikely]] return;
        auto& level = it->second;
        order_map_.erase(level.orders.front().id);
        level.orders.pop_front();                  // returns node to pool_
        if (level.empty()) [[unlikely]] bids_.erase(it);
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) [[unlikely]] return;
        auto& level = it->second;
        order_map_.erase(level.orders.front().id);
        level.orders.pop_front();
        if (level.empty()) [[unlikely]] asks_.erase(it);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────
std::optional<int64_t> OrderBook::bestBid() const {
    if (bids_.empty()) [[unlikely]] return std::nullopt;
    return bids_.begin()->first;
}

std::optional<int64_t> OrderBook::bestAsk() const {
    if (asks_.empty()) [[unlikely]] return std::nullopt;
    return asks_.begin()->first;
}

std::optional<int64_t> OrderBook::spread() const {
    auto bid = bestBid();
    auto ask = bestAsk();
    if (!bid || !ask) return std::nullopt;
    return *ask - *bid;
}

// ─────────────────────────────────────────────────────────────────────────────
// printTop — console visualisation of the top N price levels
// ─────────────────────────────────────────────────────────────────────────────
void OrderBook::printTop(int levels) const {
    std::vector<std::pair<int64_t, uint64_t>> ask_levels;
    int cnt = 0;
    for (auto& [p, lv] : asks_) {
        if (cnt++ >= levels) break;
        ask_levels.push_back({p, lv.total_qty});
    }

    std::cout << "\n╔══════════════════════════════════╗\n";
    std::cout << "║          ORDER  BOOK             ║\n";
    std::cout << "╠══════════════════════════════════╣\n";
    std::cout << "║  SIDE   PRICE        QTY          ║\n";
    std::cout << "╠══════════════════════════════════╣\n";

    for (auto it = ask_levels.rbegin(); it != ask_levels.rend(); ++it)
        std::cout << "║  ASK    "
                  << std::fixed << std::setprecision(2) << std::setw(9)
                  << toDoublePrice(it->first)
                  << "   " << std::setw(8) << it->second << "   ║\n";

    auto sp = spread();
    if (sp)
        std::cout << "║  ── spread: " << std::setw(6) << std::fixed
                  << std::setprecision(2) << toDoublePrice(*sp)
                  << " ──────────────  ║\n";
    else
        std::cout << "║  ── (no spread) ────────────────  ║\n";

    cnt = 0;
    for (auto& [p, lv] : bids_) {
        if (cnt++ >= levels) break;
        std::cout << "║  BID    "
                  << std::fixed << std::setprecision(2) << std::setw(9)
                  << toDoublePrice(p)
                  << "   " << std::setw(8) << lv.total_qty << "   ║\n";
    }
    std::cout << "╚══════════════════════════════════╝\n";
}
