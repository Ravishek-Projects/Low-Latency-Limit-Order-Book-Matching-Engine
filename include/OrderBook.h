#pragma once

#include <map>
#include <unordered_map>
#include <memory_resource>
#include <functional>
#include <optional>
#include "PriceLevel.h"

// ─────────────────────────────────────────────────────────────────────────────
// OrderBook — stores two sorted sides with O(1) cancel via order lookup map.
//
// OPTIMISATION OVERVIEW
// ─────────────────────────────────────────────────────────────────────────────
// 1. pool_ (pmr::unsynchronized_pool_resource)
//    Owns all memory for order list nodes across ALL price levels.
//    Pre-allocates large chunks from the OS; subsequent alloc/free is pure
//    freelist pointer arithmetic — no syscalls, no lock, O(1).
//    Expected gain: 40-60% insert latency reduction, dramatic p99 improvement.
//
// 2. try_emplace instead of emplace
//    Constructs PriceLevel in-place only when a new price level is needed,
//    avoiding any temporary construction + move.
//
// 3. [[likely]] / [[unlikely]] on hot branches
//    Guides the compiler's branch predictor and instruction cache layout.
// ─────────────────────────────────────────────────────────────────────────────
class OrderBook {
public:
    // Bids: highest price at begin()
    using BidMap = std::map<int64_t, PriceLevel, std::greater<int64_t>>;
    // Asks: lowest price at begin()
    using AskMap = std::map<int64_t, PriceLevel>;

    // ── Mutators ─────────────────────────────────────────────────────────────
    void addOrder(const Order& order);
    bool cancelOrder(uint64_t order_id);
    void reduceFront(Side side, int64_t price, uint32_t qty);
    void popFront   (Side side, int64_t price);

    // ── Accessors ─────────────────────────────────────────────────────────────
    const BidMap& bids() const { return bids_; }
    const AskMap& asks() const { return asks_; }
    bool hasBids() const { return !bids_.empty(); }
    bool hasAsks() const { return !asks_.empty(); }

    std::optional<int64_t> bestBid() const;
    std::optional<int64_t> bestAsk() const;
    std::optional<int64_t> spread()  const;

    size_t orderCount() const { return order_map_.size(); }
    void   printTop(int levels = 5) const;

private:
    // ── Pool resource (must be declared BEFORE bid_/ask_ maps) ───────────────
    // pmr::unsynchronized_pool_resource:
    //   - Not thread-safe (fine; our engine is single-threaded)
    //   - Acquires large chunks from the OS upstream allocator
    //   - Maintains per-size freelists for O(1) alloc/dealloc
    //   - Never returns memory to OS until destroyed (OrderBook lifetime)
    std::pmr::unsynchronized_pool_resource pool_;

    BidMap bids_;
    AskMap asks_;

    // O(1) cancel: maps order_id → direct iterator into its price level's list
    struct OrderLocation {
        Side                              side;
        int64_t                           price;
        std::pmr::list<Order>::iterator   it;   // direct O(1) handle
    };
    std::unordered_map<uint64_t, OrderLocation> order_map_;
};
