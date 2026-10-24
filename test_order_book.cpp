#include <iostream>

#include "order_book.hpp"

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond << "\n";   \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

using namespace fx;

static Order mk(OrderId id, Side s, Price p, Quantity q, Timestamp ts) {
    return Order{id, s, p, q, ts};
}

void test_no_cross_rests() {
    OrderBook b;
    CHECK(b.add(mk(1, Side::Buy, 100, 10, 1)).empty());
    CHECK(b.add(mk(2, Side::Sell, 105, 10, 2)).empty());
    CHECK(*b.bestBid() == 100);
    CHECK(*b.bestAsk() == 105);
}

void test_full_match() {
    OrderBook b;
    b.add(mk(1, Side::Sell, 100, 10, 1));
    auto t = b.add(mk(2, Side::Buy, 100, 10, 2));
    CHECK(t.size() == 1);
    CHECK(t[0].qty == 10 && t[0].price == 100);
    CHECK(b.restingOrders() == 0);
}

void test_partial_fill() {
    OrderBook b;
    b.add(mk(1, Side::Sell, 100, 10, 1));
    auto t = b.add(mk(2, Side::Buy, 100, 4, 2));
    CHECK(t.size() == 1 && t[0].qty == 4);
    CHECK(b.restingOrders() == 1);          // 6 left on the ask
    CHECK(*b.bestAsk() == 100);
}

void test_time_priority() {
    OrderBook b;
    b.add(mk(1, Side::Sell, 100, 5, 1));    // older
    b.add(mk(2, Side::Sell, 100, 5, 2));    // newer, same price
    auto t = b.add(mk(3, Side::Buy, 100, 5, 3));
    CHECK(t.size() == 1 && t[0].sellId == 1);   // oldest fills first
}

void test_price_priority() {
    OrderBook b;
    b.add(mk(1, Side::Sell, 102, 5, 1));
    b.add(mk(2, Side::Sell, 101, 5, 2));    // better (lower) price
    auto t = b.add(mk(3, Side::Buy, 105, 5, 3));
    CHECK(t.size() == 1 && t[0].sellId == 2 && t[0].price == 101);
}

void test_sweep_levels() {
    OrderBook b;
    b.add(mk(1, Side::Sell, 100, 5, 1));
    b.add(mk(2, Side::Sell, 101, 5, 2));
    auto t = b.add(mk(3, Side::Buy, 101, 8, 3));
    CHECK(t.size() == 2);
    CHECK(t[0].price == 100 && t[0].qty == 5);
    CHECK(t[1].price == 101 && t[1].qty == 3);
}

void test_cancel() {
    OrderBook b;
    b.add(mk(1, Side::Buy, 100, 10, 1));
    CHECK(b.cancel(1));
    CHECK(!b.cancel(1));        // already gone
    CHECK(!b.cancel(999));      // never existed
    CHECK(b.restingOrders() == 0);
    CHECK(!b.bestBid().has_value());
}

int main() {
    test_no_cross_rests();
    test_full_match();
    test_partial_fill();
    test_time_priority();
    test_price_priority();
    test_sweep_levels();
    test_cancel();
    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
