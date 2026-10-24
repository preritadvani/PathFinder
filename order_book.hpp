#pragma once
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "types.hpp"

namespace fx {

// One order book = the waiting buy orders (bids) and sell orders (asks)
// for ONE market. Matching rule: price-time priority.
class OrderBook {
public:
    // Add an order. Returns the trades it caused (may be empty).
    // Any unfilled remainder rests in the book.
    std::vector<Trade> add(Order o) {
        std::vector<Trade> trades;
        if (o.side == Side::Buy) match(o, asks_, trades);
        else                     match(o, bids_, trades);

        if (o.qty > 0) rest(o);
        return trades;
    }

    // Remove a resting order. Returns true if it existed.
    bool cancel(OrderId id) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;
        auto [side, price] = it->second;
        index_.erase(it);
        if (side == Side::Buy) return eraseFrom(bids_, price, id);
        return eraseFrom(asks_, price, id);
    }

    std::optional<Price> bestBid() const {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
    }
    std::optional<Price> bestAsk() const {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin()->first;
    }

    std::size_t restingOrders() const { return index_.size(); }

private:
    // Bids: highest price first.  Asks: lowest price first.
    // Each price level is a FIFO queue (oldest order at the front).
    using BidSide = std::map<Price, std::deque<Order>, std::greater<Price>>;
    using AskSide = std::map<Price, std::deque<Order>>;

    template <typename BookSide>
    void match(Order& incoming, BookSide& opposite, std::vector<Trade>& trades) {
        while (incoming.qty > 0 && !opposite.empty()) {
            auto levelIt = opposite.begin();             // best price on the other side
            Price levelPrice = levelIt->first;

            bool crosses = (incoming.side == Side::Buy) ? (incoming.price >= levelPrice)
                                                         : (incoming.price <= levelPrice);
            if (!crosses) break;

            auto& queue = levelIt->second;
            while (incoming.qty > 0 && !queue.empty()) {
                Order& resting = queue.front();          // oldest order first
                Quantity q = std::min(incoming.qty, resting.qty);

                Trade t{};
                t.buyId  = (incoming.side == Side::Buy) ? incoming.id : resting.id;
                t.sellId = (incoming.side == Side::Buy) ? resting.id  : incoming.id;
                t.price  = levelPrice;                   // trade at the resting order's price
                t.qty    = q;
                t.ts     = incoming.ts;
                trades.push_back(t);

                incoming.qty -= q;
                resting.qty  -= q;
                if (resting.qty == 0) {
                    index_.erase(resting.id);
                    queue.pop_front();
                }
            }
            if (queue.empty()) opposite.erase(levelIt);
        }
    }

    void rest(const Order& o) {
        if (o.side == Side::Buy) bids_[o.price].push_back(o);
        else                     asks_[o.price].push_back(o);
        index_[o.id] = {o.side, o.price};
    }

    template <typename BookSide>
    bool eraseFrom(BookSide& side, Price price, OrderId id) {
        auto lvl = side.find(price);
        if (lvl == side.end()) return false;
        auto& q = lvl->second;
        for (auto it = q.begin(); it != q.end(); ++it) {
            if (it->id == id) {
                q.erase(it);
                if (q.empty()) side.erase(lvl);
                return true;
            }
        }
        return false;
    }

    BidSide bids_;
    AskSide asks_;
    std::unordered_map<OrderId, std::pair<Side, Price>> index_;  // id -> where it rests
};

}  // namespace fx
