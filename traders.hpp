#pragma once
#include <random>
#include <string>

#include "thread_safe_queue.hpp"
#include "types.hpp"

namespace fx {

// A very simple "noise trader": sends random buy/sell orders with prices
// scattered around a fair price. Same seed => same orders (reproducible).
class RandomTrader {
public:
    RandomTrader(std::string symbol, Price fairPrice, unsigned seed)
        : symbol_(std::move(symbol)), fair_(fairPrice), rng_(seed) {}

    NewOrder next() {
        std::uniform_int_distribution<int> sideDist(0, 1);
        std::uniform_int_distribution<int> offset(-20, 20);   // +/- 20 ticks
        std::uniform_int_distribution<int> qtyDist(1, 100);

        NewOrder o;
        o.symbol = symbol_;
        o.side   = sideDist(rng_) ? Side::Buy : Side::Sell;
        o.price  = fair_ + offset(rng_);
        o.qty    = qtyDist(rng_);
        return o;
    }

    // Send `count` orders to the exchange inbox.
    void run(ThreadSafeQueue<NewOrder>& inbox, int count) {
        for (int i = 0; i < count; ++i) inbox.push(next());
    }

private:
    std::string symbol_;
    Price fair_;
    std::mt19937 rng_;
};

}  // namespace fx
