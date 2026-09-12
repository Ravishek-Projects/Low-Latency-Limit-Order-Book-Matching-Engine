#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Price is stored as an integer (price × PRICE_SCALE) to avoid floating-point
// comparison bugs.  Example: $100.25  →  10025000
// ─────────────────────────────────────────────────────────────────────────────
constexpr int64_t PRICE_SCALE = 100'000;  // 5 decimal places

inline int64_t toIntPrice(double price) {
    return static_cast<int64_t>(price * PRICE_SCALE + 0.5);
}
inline double toDoublePrice(int64_t price) {
    return static_cast<double>(price) / PRICE_SCALE;
}

// ─────────────────────────────────────────────────────────────────────────────
enum class Side : uint8_t { BUY, SELL };

// ─────────────────────────────────────────────────────────────────────────────
// Aligning to 64 bytes keeps a single Order within one cache line.
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(64) Order {
    uint64_t id;           // unique order identifier
    int64_t  price;        // integer price (use toIntPrice / toDoublePrice)
    uint64_t timestamp;    // nanoseconds since epoch (insertion time)
    uint32_t quantity;     // remaining quantity (decremented on partial fills)
    Side     side;         // BUY or SELL

    // Convenience constructor that accepts a human-readable double price
    Order(uint64_t id_, Side side_, double price_, uint32_t qty_, uint64_t ts_)
        : id(id_), price(toIntPrice(price_)), timestamp(ts_),
          quantity(qty_), side(side_) {}

    Order() = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// A trade is the result of matching two opposing orders.
// ─────────────────────────────────────────────────────────────────────────────
struct Trade {
    uint64_t maker_id;      // resting order (was already in the book)
    uint64_t taker_id;      // aggressive order (just arrived)
    int64_t  price;         // execution price (maker's price)
    uint32_t quantity;      // filled quantity
};
