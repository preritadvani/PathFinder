#pragma once
#include <vector>

#include "market.hpp"
#include "thread_safe_queue.hpp"
#include "types.hpp"

namespace fx {

// The exchange. Traders push NewOrders into inbox(); ONE thread runs
// run(), which assigns ids + timestamps and matches orders.
// Only one thread touches the books, so they need no locks.
class Engine {
public:
    ThreadSafeQueue<NewOrder>& inbox() { return inbox_; }

    // Call this on the matching thread. Returns when the inbox is closed.
    void run() {
        while (auto msg = inbox_.pop()) {
            Order o{nextId_++, msg->side, msg->price, msg->qty, ++clock_};
            auto trades = market_.book(msg->symbol).add(o);
            for (const auto& t : trades) trades_.push_back(t);
            ++ordersProcessed_;
        }
    }

    const Market& market() const { return market_; }
    const std::vector<Trade>& trades() const { return trades_; }
    std::size_t ordersProcessed() const { return ordersProcessed_; }

private:
    ThreadSafeQueue<NewOrder> inbox_;
    Market market_;
    std::vector<Trade> trades_;
    OrderId nextId_ = 1;
    Timestamp clock_ = 0;
    std::size_t ordersProcessed_ = 0;
};

}  // namespace fx
