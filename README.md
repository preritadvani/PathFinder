# fx_engine

A small C++20 market-simulation engine. Simulated traders send buy/sell orders
to an exchange, which keeps an order book per currency pair and matches orders
using **price-time priority**. This is the platform the arbitrage detection,
optimization and realism (fees/slippage) work plugs into.

## Build and run

```bash
cmake -S . -B build
cmake --build build
./build/fx_main                 # run the simulation
ctest --test-dir build          # run the tests
```

Needs: a C++20 compiler (g++ 10+/clang 12+) and CMake 3.16+.

## Layout

| File | What it does |
|---|---|
| `include/types.hpp` | `Price`, `Quantity`, `Side`, `NewOrder`, `Order`, `Trade` |
| `include/order_book.hpp` | One order book: add, match, cancel, best bid/ask |
| `include/market.hpp` | One `OrderBook` per symbol (`"EUR/USD"`, ...) |
| `include/thread_safe_queue.hpp` | Inbox that many threads push into, one thread pops |
| `include/traders.hpp` | `RandomTrader`: seeded random orders around a fair price |
| `include/engine.hpp` | The exchange: pops orders, assigns id + timestamp, matches |
| `src/main.cpp` | Demo: 3 trader threads + 1 matching thread |
| `tests/test_order_book.cpp` | Tests for matching, priority, partial fills, cancel |

## How it works

```
trader threads --push--> ThreadSafeQueue --pop--> Engine (1 thread) --> Market --> OrderBook(s)
                                                       |
                                                       +--> trades_ (list of executed trades)
```

- Only the engine thread touches the order books, so the books need no locks.
- Prices are **integers in ticks** (`10850` = 1.0850). Never use `double` for
  prices inside the book: rounding errors break matching.
- Ids and timestamps are assigned by the engine in arrival order. Timestamp is
  a sequence counter, not a clock.
- Traders use a seeded RNG. Note: with several trader threads, the *arrival
  order* at the queue can vary between runs.

## Where each teammate plugs in

- **Detection**: read the books via `Engine::market()` (`bestBid()`, `bestAsk()`
  per symbol), build the exchange-rate graph, run Bellman-Ford.
- **Optimization**: compare/choose among the cycles detection finds.
- **Realism**: fees and slippage. Slippage needs book *depth*, which is not
  exposed yet: add a read-only depth accessor to `OrderBook` (see TODO).

## TODO / next steps

- [ ] Depth accessor on `OrderBook` (list of price levels with total qty)
- [ ] Several symbols with linked prices (EUR/USD, GBP/USD, EUR/GBP) + a "shock" that creates a mispricing
- [ ] Market maker trader (quotes both sides)
- [ ] Binary event log, then RLE + Huffman compression
- [ ] Snapshot struct + detector interface for the arbitrage code
- [ ] Benchmarks (orders/sec) and CSV export for plots

## Conventions

- All code in namespace `fx`, header-only for now.
- Add a test in `tests/` for every new behaviour in the order book.
- Debug with: `-fsanitize=address,undefined`.
