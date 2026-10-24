#pragma once
#include <cstdint>
#include <string>

namespace fx {

using Price     = std::int64_t;   // price in ticks (whole number, e.g. 10850 = 1.0850)
using Quantity  = std::int64_t;   // units of the base currency
using OrderId   = std::uint64_t;  // unique id, assigned by the engine
using Timestamp = std::uint64_t;  // sequence number, goes up by 1 per event

enum class Side { Buy, Sell };

// What a trader SENDS to the exchange (no id / timestamp yet).
struct NewOrder {
    std::string symbol;  // market name, e.g. "EUR/USD"
    Side        side;
    Price       price;
    Quantity    qty;
};

// What lives INSIDE the order book (the engine adds id + timestamp).
struct Order {
    OrderId   id;
    Side      side;
    Price     price;
    Quantity  qty;   // remaining quantity
    Timestamp ts;
};

// One match between a buy order and a sell order.
struct Trade {
    OrderId   buyId;
    OrderId   sellId;
    Price     price;
    Quantity  qty;
    Timestamp ts;
};

}  // namespace fx
