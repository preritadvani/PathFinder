#pragma once
#include <string>
#include <unordered_map>

#include "order_book.hpp"

namespace fx {

// A Market holds one OrderBook per symbol ("EUR/USD", "GBP/USD", ...).
// Books are created the first time a symbol is used.
class Market {
public:
    OrderBook& book(const std::string& symbol) { return books_[symbol]; }

    const std::unordered_map<std::string, OrderBook>& all() const { return books_; }

private:
    std::unordered_map<std::string, OrderBook> books_;
};

}  // namespace fx
