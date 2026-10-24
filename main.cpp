#include <iostream>
#include <thread>
#include <vector>

#include "engine.hpp"
#include "traders.hpp"

int main() {
    fx::Engine engine;

    // 1. Start the matching thread.
    std::thread matcher([&] { engine.run(); });

    // 2. Start 3 trader threads, each sending 1000 orders on EUR/USD.
    //    Price 10850 ticks = 1.0850.
    std::vector<std::thread> traders;
    for (unsigned seed = 1; seed <= 3; ++seed) {
        traders.emplace_back([&engine, seed] {
            fx::RandomTrader t("EUR/USD", 10850, seed);
            t.run(engine.inbox(), 1000);
        });
    }
    for (auto& t : traders) t.join();

    // 3. No more orders coming: close the inbox and wait for the matcher.
    engine.inbox().close();
    matcher.join();

    // 4. Report.
    std::cout << "Orders processed: " << engine.ordersProcessed() << "\n";
    std::cout << "Trades executed:  " << engine.trades().size() << "\n";
    for (const auto& [symbol, book] : engine.market().all()) {
        std::cout << symbol << "  best bid: ";
        if (auto b = book.bestBid()) std::cout << *b; else std::cout << "-";
        std::cout << "  best ask: ";
        if (auto a = book.bestAsk()) std::cout << *a; else std::cout << "-";
        std::cout << "  resting orders: " << book.restingOrders() << "\n";
    }
    return 0;
}
