# Realism Layer: Implementation Guide (C++)

*Arbitrage Detection and Optimization Using Graph Algorithms: Realism module*

## 1. Purpose and How This Module Fits In

The detection and optimization layers answer "does arbitrage exist, and which cycle is best?" under idealized assumptions. The realism layer answers a different question: **does that opportunity survive contact with a real market?**

The module takes a cycle and a market description, and produces a report that separates *theoretical* arbitrage (gross return) from *executable* arbitrage (net return after frictions).

This guide covers six components, in priority order:

1. Transaction fees (required)
2. Bid/ask spread
3. Numerical precision
4. Liquidity and slippage
5. Execution delay and rate movement
6. Capital constraints

It then explains how arbitrage is found in real markets and how you can do something similar on real data.

### 1.1 Agree on an interface first

You do not need the other modules to be finished, but you need to agree with teammates on two things. Suggested C++ definitions:

```cpp
#include <string>
#include <vector>
#include <unordered_map>

struct Edge {
    int    from, to;
    double rate;            // 1 unit of 'from' buys 'rate' units of 'to'
    double feeFrac = 0.0;   // proportional fee, e.g. 0.001 = 0.1%
    double feeFixed = 0.0;  // fixed fee per trade, in units of 'from'
    double depth = 1e18;    // max tradable size in units of 'from'
    double minTrade = 0.0;  // minimum trade size in units of 'from'
};

struct Graph {
    int n;
    std::vector<std::string> names;
    std::vector<Edge> edges;
    // lookup edge index by (from,to)
    std::unordered_map<long long,int> idx;
    static long long key(int u,int v){ return (long long)u*100000+v; }
    const Edge& get(int u,int v) const { return edges[idx.at(key(u,v))]; }
};

// A cycle is a vertex list where first == last, e.g. {0,1,2,0}
using Cycle = std::vector<int>;
```

Until Member 1's graph class is ready, build small test graphs by hand (including the USD -> EUR -> JPY -> USD example with rates 0.90, 160, 0.0072).

### 1.2 A single report struct

Every realism function should contribute to one report, so the final output can compare layers side by side:

```cpp
struct Report {
    double grossReturnPct;   // ideal, frictionless
    double afterFeesPct;
    double afterSpreadPct;
    double afterSlippagePct; // at a given capital
    double expectedNetPct;   // Monte Carlo mean, if run
    double probProfit;       // Monte Carlo
    bool   executable;       // true only if still profitable under chosen constraints
};
```

### 1.3 Feed realism back into detection

If you only filter cycles after Bellman-Ford reports them, you can miss cycles that are profitable *after* costs but were not the one Bellman-Ford happened to return. Better: build a second "net" graph whose edge rates are already adjusted for fees and spread, and run detection on that graph as well. Coordinate this with the detection owner. It is a small change and it makes your section part of the pipeline rather than a post-filter.

## 2. Transaction Fees (Required)

### 2.1 The idea

Every trade costs something. The simplest model charges a fraction `f` of the amount traded, so the effective rate becomes:

```
r' = r * (1 - f)
```

Over a cycle with `k` legs and fees `f_1 ... f_k`:

```
P_net = product of r_i * (1 - f_i)
```

A 0.3% gross return can disappear completely: three trades at 0.1% each cost about 0.3%.

### 2.2 Break-even condition

With a uniform fee `f` and `k` legs, a cycle is only profitable if its gross product satisfies:

```
P_gross > 1 / (1 - f)^k
```

For `f = 0.001` and `k = 3` this is about `1.003009`, so the cycle needs more than 0.30% gross return just to break even. Longer cycles need more, which is a useful insight to report: **fees penalize long cycles**.

### 2.3 Fixed fees

Some venues also charge a flat fee per trade. A fixed fee makes profit depend on *capital*, since a flat cost matters more on small amounts. You must therefore simulate the actual amount flowing through the cycle rather than only multiplying rates.

### 2.4 C++ implementation

```cpp
#include <cmath>

// Simulate the actual amount through a cycle, applying proportional and fixed fees.
double simulateWithFees(const Graph& g, const Cycle& c, double capital) {
    double amount = capital;
    for (size_t i = 0; i + 1 < c.size(); ++i) {
        const Edge& e = g.get(c[i], c[i+1]);
        amount -= e.feeFixed;               // fixed fee in input currency
        if (amount <= 0) return 0.0;
        amount *= e.rate * (1.0 - e.feeFrac);
    }
    return amount;                           // in the starting currency
}

double netReturnPct(const Graph& g, const Cycle& c, double capital) {
    return (simulateWithFees(g, c, capital) / capital - 1.0) * 100.0;
}

// Required gross product for break-even with uniform fee
double breakEvenGross(double f, int legs) {
    return 1.0 / std::pow(1.0 - f, legs);
}
```

### 2.5 What to report

- Gross vs. net return for each detected cycle.
- A sweep over fee values (for example 0%, 0.01%, 0.05%, 0.1%, 0.2%, 0.5%) showing **how many opportunities survive** at each level. This is likely your most convincing plot.
- A breakdown by cycle length (3, 4, 5+ legs).

### 2.6 Choosing realistic values

Do not invent numbers. Look up current fee schedules and cite them. Crypto exchanges typically charge on the order of 0.1% per trade for ordinary taker orders, with lower tiers for high volume. FX is usually quoted without an explicit commission, with costs embedded in the spread (next section). Verify current figures before putting them in your report.

## 3. Bid/Ask Spread

### 3.1 Why this matters most

In real markets you buy at the **ask** and sell at the **bid**, and the ask is higher than the bid. Going around a cycle means crossing the spread on every leg. For liquid markets this is usually the single biggest reason apparent arbitrage is not real.

Your proposal assumes spreads are zero. Adding them is the biggest realism gain per line of code.

### 3.2 A consistent synthetic market

Random independent rates will produce huge fake arbitrage. A better generator builds a market that is **consistent by construction** and then injects realistic imperfections:

1. Give each currency a latent value `p_i` (its value in some common unit).
2. The fair mid rate from `u` to `v` is `mid(u,v) = p_u / p_v`.
3. Add small independent noise to each pair's mid quote: `mid *= exp(sigma * Z)`, where `Z ~ N(0,1)`. This is what creates inconsistency across triangles.
4. Apply a spread `s` (as a fraction) in both directions:

```
r(u->v) = mid * (1 - s/2)
r(v->u) = (1/mid) * (1 - s/2)
```

A round trip `u -> v -> u` then returns about `(1 - s/2)^2`, roughly `1 - s`, so you always lose the spread. A cycle is profitable only when the noise across pairs exceeds the combined spread, which is exactly how real markets behave.

The key experiment is then: **fix the noise `sigma`, vary the spread `s`, and plot how many arbitrage cycles remain.** At `s = 0` there will be many; as `s` grows they vanish.

### 3.3 C++ implementation

```cpp
#include <random>

Graph generateMarket(int n, double sigma, double spread, unsigned seed,
                     double feeFrac = 0.0, double density = 1.0) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> N01(0.0, 1.0);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::uniform_real_distribution<double> logValue(std::log(0.001), std::log(1000.0));

    Graph g; g.n = n;
    std::vector<double> p(n);
    for (int i = 0; i < n; ++i) {
        p[i] = std::exp(logValue(rng));
        g.names.push_back("C" + std::to_string(i));
    }
    auto add = [&](int u, int v, double rate) {
        Edge e; e.from = u; e.to = v; e.rate = rate; e.feeFrac = feeFrac;
        g.idx[Graph::key(u, v)] = (int)g.edges.size();
        g.edges.push_back(e);
    };
    for (int u = 0; u < n; ++u)
        for (int v = u + 1; v < n; ++v) {
            if (U(rng) > density) continue;           // sparse graphs
            double mid = (p[u] / p[v]) * std::exp(sigma * N01(rng));
            add(u, v, mid * (1.0 - spread / 2.0));
            add(v, u, (1.0 / mid) * (1.0 - spread / 2.0));
        }
    return g;
}
```

Save the seed with every experiment so results are reproducible.

### 3.4 Using real quotes

Real data comes as a bid and an ask for each pair. For a pair `BASE/QUOTE`:

```cpp
addEdge(BASE, QUOTE, bid);        // sell 1 BASE, receive 'bid' QUOTE
addEdge(QUOTE, BASE, 1.0 / ask);  // spend 1 QUOTE, receive 1/ask BASE
```

The spread is then embedded automatically in the two directional rates.

## 4. Numerical Precision

### 4.1 The problem

Floating-point arithmetic introduces rounding error. Taking `-ln(r)` and summing along a cycle can give a total like `-1e-16` for a cycle that is mathematically break-even. Without protection, your detector will report fake arbitrage. Bellman-Ford can also keep "improving" distances by rounding noise and fail to converge cleanly.

Your proposal already lists "cycles with returns very close to 0%" as a test case, so this is directly relevant.

### 4.2 Techniques

**1. Epsilon tolerance.** Treat a cycle as profitable only if its log-weight is below `-eps` (for example `1e-9`), or equivalently its product exceeds `1 + eps`.

**2. Tolerance in relaxation.** In Bellman-Ford and SPFA, only relax when the improvement is meaningful:

```cpp
const double EPS = 1e-12;
if (dist[u] + w < dist[v] - EPS) { dist[v] = dist[u] + w; pred[v] = u; }
```

**3. Higher precision.** Use `long double` (typically 80-bit on x86) for accumulating products and logs. For final profit figures, consider fixed-point integers (for example amounts in 1e-8 units) or a decimal library, since money should not be computed in binary floating point.

**4. Prefer the log domain for sums, multiply for final checks.** Detect in the log domain, then confirm by recomputing the product directly from the original rates. A cycle that fails the direct check is a rounding artifact.

### 4.3 C++ implementation

```cpp
constexpr long double EPS_PROFIT = 1e-9L;

long double cycleProduct(const Graph& g, const Cycle& c) {
    long double p = 1.0L;
    for (size_t i = 0; i + 1 < c.size(); ++i)
        p *= (long double)g.get(c[i], c[i+1]).rate;
    return p;
}

bool isProfitable(const Graph& g, const Cycle& c) {
    return cycleProduct(g, c) > 1.0L + EPS_PROFIT;
}
```

### 4.4 Tests to write

- A cycle with exact product 1 (for example rates 2, 0.5) must **not** be flagged.
- A cycle with product `1 + 1e-6` must be flagged.
- A cycle with product `1 + 1e-12` should be classified as break-even under your epsilon (document your threshold).
- Very large and very small rates (for example 1e-6 and 1e6) must not overflow or lose accuracy.
- Compare `float`, `double`, and `long double` on the same near-zero cases and report differences. This is an easy, interesting result for your report.

## 5. Liquidity and Slippage

### 5.1 The idea

A quoted rate applies only up to a limited size. Trading more pushes you through worse prices, called **slippage**. This makes profit depend on how much you trade.

### 5.2 Model A: linear slippage

Simple and easy to analyze. For trade size `x` (in input currency) on an edge with depth `L`:

```
r_eff(x) = r * (1 - k * x / L)
```

where `k` controls how steeply the price worsens. The output is `x * r_eff(x)`, which is a downward parabola in `x`.

### 5.3 Model B: tiered order book

More realistic. Each edge has price levels with limited size, and a trade consumes the best level first, then the next, and so on:

```cpp
struct Level { double rate; double size; };   // size in input currency

// Fill 'x' units of input currency through the book; returns output amount.
// Levels must be sorted from best to worst rate.
double fillBook(const std::vector<Level>& book, double x) {
    double out = 0.0;
    for (const Level& lv : book) {
        double take = std::min(x, lv.size);
        out += take * lv.rate;
        x -= take;
        if (x <= 1e-15) break;
    }
    return (x > 1e-15) ? -1.0 : out;   // -1 means insufficient liquidity
}
```

Then chain the legs. The output of one leg is the input of the next:

```cpp
double simulateWithBooks(const std::vector<std::vector<Level>>& books, double capital) {
    double amount = capital;
    for (const auto& b : books) {
        amount = fillBook(b, amount);
        if (amount < 0) return -1.0;
    }
    return amount;
}
```

### 5.4 Profit as a function of capital

With slippage, profit is not proportional to capital. Small trades capture the full quoted discrepancy but earn little in absolute terms; large trades earn more per unit until slippage overtakes the gain. There is an optimal amount.

```cpp
double profitAt(const Graph& g, const Cycle& c, double x) {
    return simulateWithFees(g, c, x) - x;   // swap in book-based simulation when available
}

// Golden-section search, valid if profit(x) is unimodal (typical for concave slippage)
double bestCapital(const Graph& g, const Cycle& c, double lo, double hi) {
    const double phi = (std::sqrt(5.0) - 1.0) / 2.0;
    double a = lo, b = hi;
    double x1 = b - phi * (b - a), x2 = a + phi * (b - a);
    double f1 = profitAt(g, c, x1), f2 = profitAt(g, c, x2);
    for (int it = 0; it < 100; ++it) {
        if (f1 < f2) { a = x1; x1 = x2; f1 = f2; x2 = a + phi * (b - a); f2 = profitAt(g, c, x2); }
        else         { b = x2; x2 = x1; f2 = f1; x1 = b - phi * (b - a); f1 = profitAt(g, c, x1); }
    }
    return (a + b) / 2.0;
}
```

If you are unsure the curve is unimodal (it may not be with tiered books), also do a coarse scan over capital values (for example logarithmically spaced) and pick the best, then refine locally.

### 5.5 Maximum tradable size

The cycle's capacity is limited by its tightest leg, after converting through earlier legs. Compute each leg's depth limit in terms of the *starting* currency by dividing by the cumulative product of earlier effective rates, then take the minimum.

### 5.6 What to report

- Profit-vs-capital curve for a few cycles.
- The optimal capital and the profit at that point.
- How the maximum profit shrinks as depth decreases.

## 6. Execution Delay and Rate Movement

### 6.1 The idea

A cycle is a sequence of trades, and each takes time. Rates move meanwhile, so the rates you actually get on later legs differ from those that made the cycle look profitable. Sometimes the move helps, but on average, because other traders are closing the same gap, it hurts.

This is best studied with **Monte Carlo simulation**: run the cycle many times under random price movement and look at the *distribution* of outcomes rather than a single number.

### 6.2 Model

Let leg `i` execute at time `i * delta` after detection. Each rate evolves multiplicatively:

```
r_i(t) = r_i(0) * exp( -0.5 * sigma^2 * t + sigma * sqrt(t) * Z )
```

where `sigma` is the volatility per unit time and `Z ~ N(0,1)`.

A simplification: this treats each edge's noise as independent. In reality `u -> v` and `v -> u` move together (one is roughly the reciprocal of the other). For a more careful model, drive each *pair* with one shock and derive the reverse direction from it. State your simplification in the report.

### 6.3 Opportunity decay

Add the effect of other traders closing the gap. Model the discrepancy as shrinking exponentially:

```
(P_gross(t) - 1) = (P_gross(0) - 1) * exp(-lambda * t)
```

where `lambda` is the decay rate. A larger `lambda` means a more competitive market.

### 6.4 Leg failure

Each leg may fail (order rejected, price moved past your limit) with probability `q`. If leg `j` fails, you are left holding an intermediate currency and must unwind, which costs you the spread and any adverse move. A simple approach: charge a fixed unwind penalty (for example one extra spread plus a fee) when failure occurs. The probability of completing all `k` legs is `(1 - q)^k`.

### 6.5 C++ implementation

```cpp
struct MCResult {
    double meanNetPct, probProfit, p5, p95;
};

MCResult monteCarloCycle(const Graph& g, const Cycle& c, double capital,
                         double sigma, double delta, int runs, unsigned seed,
                         double legFailProb = 0.0, double unwindPenaltyFrac = 0.002) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> N01(0.0, 1.0);
    std::uniform_real_distribution<double> U(0.0, 1.0);

    std::vector<double> results; results.reserve(runs);
    for (int r = 0; r < runs; ++r) {
        double amount = capital;
        bool failed = false;
        for (size_t i = 0; i + 1 < c.size(); ++i) {
            if (U(rng) < legFailProb) { failed = true; break; }
            const Edge& e = g.get(c[i], c[i+1]);
            double t = (double)(i + 1) * delta;
            double shock = std::exp(-0.5 * sigma * sigma * t + sigma * std::sqrt(t) * N01(rng));
            amount -= e.feeFixed;
            amount *= e.rate * shock * (1.0 - e.feeFrac);
        }
        if (failed) amount = capital * (1.0 - unwindPenaltyFrac);
        results.push_back((amount / capital - 1.0) * 100.0);
    }
    std::sort(results.begin(), results.end());
    MCResult m{};
    double sum = 0; int wins = 0;
    for (double x : results) { sum += x; if (x > 0) ++wins; }
    m.meanNetPct  = sum / runs;
    m.probProfit  = (double)wins / runs;
    m.p5          = results[(size_t)(0.05 * runs)];
    m.p95         = results[(size_t)(0.95 * runs)];
    return m;
}
```

Needs `<algorithm>`, `<random>`, `<cmath>`, `<vector>`. Use at least 10,000 runs and fix the seed.

### 6.6 What to report

- Expected net return, probability of profit, and 5th/95th percentile for each cycle.
- A plot of probability of profit against delay `delta` (it should fall as delay grows).
- A plot of expected return against volatility `sigma`.
- Comparison of the detected "best" cycle versus the cycle that is best *after* simulation. They are often different, which is itself a useful finding.

## 7. Capital Constraints

### 7.1 The idea

You have finite starting money, trades have minimum and maximum sizes, and real venues trade in discrete lots. These constraints rule out some cycles and cap the profit of others.

### 7.2 What to model

- **Starting capital** `C0`: the upper bound on what you can put in.
- **Minimum trade size:** a leg is infeasible if the amount flowing in is below the venue's minimum. This is why tiny amounts cannot exploit tiny discrepancies.
- **Maximum trade size:** per-trade or per-venue limits (overlaps with depth from Section 5).
- **Lot size / precision:** amounts are rounded down to a multiple of the lot size, leaving small remainders ("dust").
- **Inventory:** you must hold the starting currency; for a multi-venue setup you also need balances pre-positioned (see Section 9).

### 7.3 C++ implementation

```cpp
struct Constraints {
    double startCapital = 10000.0;
    double lotSize      = 0.0;       // 0 = continuous
};

// Returns final amount, or -1 if infeasible under constraints
double simulateConstrained(const Graph& g, const Cycle& c, double x, const Constraints& k) {
    if (x > k.startCapital) return -1.0;
    double amount = x;
    for (size_t i = 0; i + 1 < c.size(); ++i) {
        const Edge& e = g.get(c[i], c[i+1]);
        if (amount < e.minTrade || amount > e.depth) return -1.0;
        if (k.lotSize > 0) amount = std::floor(amount / k.lotSize) * k.lotSize;
        amount -= e.feeFixed;
        amount *= e.rate * (1.0 - e.feeFrac);
    }
    return amount;
}
```

The optimal capital search from Section 5 then runs over `[minimum feasible, startCapital]`.

### 7.4 What to report

- Which cycles become infeasible at small capital.
- Net profit as a function of starting capital, showing the point where profit stops growing.

## 8. Testing the Realism Module

Write known-answer tests where you can compute the expected value by hand:

1. **Fee test:** rates 0.90, 160, 0.0072 with 0.1% fee per leg must give net product `1.0368 * 0.999^3 = 1.03257...`
2. **Break-even test:** gross product exactly `1 / (1 - f)^k` must be classified as not executable under your epsilon.
3. **Spread consistency test:** a market generated with `sigma = 0` and any `s > 0` must have **zero** arbitrage cycles.
4. **Monotonicity tests:** more fees, wider spread, or more delay must never *increase* net return on average.
5. **Slippage test:** as depth tends to infinity, results must converge to the no-slippage answer.
6. **Monte Carlo sanity test:** with `sigma = 0` and no failures, the simulation must equal the deterministic result.
7. **Reproducibility test:** the same seed gives identical output.

If you are given a validation framework by the testing owner, wrap these as unit tests there.

## 9. How Arbitrage Is Found in Real Markets

### 9.1 Types of arbitrage that actually exist

**Triangular arbitrage.** What your project models: a cycle of three (or more) currencies on the *same* venue. Because only one exchange is involved, there is no transfer delay, which makes it the most natural to automate. On major currency pairs and big crypto exchanges, these gaps are tiny and last very briefly.

**Cross-exchange (spatial) arbitrage.** The same asset is priced differently on two venues. You buy on the cheap venue and sell on the expensive one. The difficulty is moving funds between venues, so practitioners keep **pre-funded balances on both** and rebalance later rather than transferring per trade.

**Statistical arbitrage.** Not risk-free: it bets that related assets (for example two stocks that usually move together) will revert after diverging. It relies on statistics and risk management rather than a guaranteed cycle.

**Basis / cash-and-carry arbitrage.** A futures price diverges from the spot price by more than the cost of carry (interest, storage). One buys spot and sells the future (or the reverse) and locks in the difference at expiry.

**ETF / index arbitrage.** The price of an ETF drifts from the value of its underlying basket, and authorized participants trade the gap away.

**DeFi arbitrage.** Decentralized exchanges price assets through automated formulas, so prices on different pools or chains can differ. Searchers trade the gap, sometimes using flash loans (borrowing within one transaction) and competing on transaction ordering and fees.

### 9.2 How professionals find opportunities

- **Data feeds.** They consume direct, real-time market data (often every order book update), not periodic polling.
- **Speed.** Detection and execution happen in microseconds to milliseconds, using co-located servers near the exchange, optimized network paths, and sometimes specialized hardware such as FPGAs.
- **Small graphs, specialized code.** With a limited set of tradable pairs, firms often check specific triangles directly (a simple loop over triples) rather than running general Bellman-Ford. Bellman-Ford is the clean theoretical tool; production systems favor hand-tuned, incremental updates that only re-check cycles touched by a changed quote.
- **Cost-aware detection.** Fees, rebates, and spreads are built into the search. The question is never "is the product above 1?" but "is it above 1 after all costs and expected slippage?"
- **Risk and inventory management.** They hold balances in several currencies and venues, limit position size, and monitor for partial fills.
- **Competition.** The same gap is visible to many participants, so the fastest and cheapest wins. This is why opportunities close so quickly and why your Monte Carlo layer should include decay.

A realistic takeaway for your report: in efficient, liquid markets, simple arbitrage at retail speed is rare because well-funded firms with lower costs and lower latency remove it first.

## 10. Can You Do This Yourself?

You can build and test a real-data **detector** as a learning exercise. Treat actually trading real money as a separate matter, which this project does not require. Real trading involves fees, risk of loss, exchange terms of service, tax, and in some places regulation. Nothing here is financial advice.

### 10.1 A realistic path for a student project

1. **Get data.** Many crypto exchanges publish free market data through public APIs, and some provide historical data downloads. Use order book or best bid/ask data, ideally from a WebSocket stream rather than repeated REST calls. Check each provider's current documentation, rate limits, and terms before use. For FX, free tick data is harder to get, so crypto or recorded historical datasets are the easier option.
2. **Build the graph from bid/ask.** Use the mapping in Section 3.4. Each pair gives two directed edges whose rates already include the spread.
3. **Apply fees.** Use the exchange's published fee schedule for the edge feeFrac.
4. **Run detection** (Bellman-Ford and your variants) on each snapshot, then pass detected cycles through your realism functions.
5. **Log everything.** For each detected cycle, record the timestamp, cycle, gross and net return. Track **how long** each opportunity persists across successive snapshots. Lifetime is a key finding, since real opportunities often last less than a second.
6. **Backtest or paper-trade.** Simulate acting on opportunities with realistic latency (for example, using the quote from 200 ms after detection, not at detection) and report the profit that would have resulted. Do **not** execute real orders for the project.

### 10.2 Pitfalls to understand

- **Non-simultaneous snapshots.** Quotes for different pairs arrive at different times. A "cycle" built from slightly stale quotes can be a phantom. This violates your proposal's assumption 6, so mention it.
- **Top-of-book only.** Best bid/ask shows only the first level of liquidity. A real fill beyond that size gets worse prices (Section 5).
- **Fake arbitrage from data issues.** Missing quotes, crossed books, stale ticks, and bad prints can all create impossible profits. Add data validation.
- **Survivorship in backtests.** If you only look at opportunities that were detected, you must still model whether you could have *reached* them in time.

A good result for the report is a plot of **detected opportunities per hour versus fee level and latency assumption**. It will likely show that apparent opportunities are common, executable ones are rare, and almost none survive realistic latency, which is exactly the lesson of the realism layer.

## 11. Suggested Work Plan

1. Agree on the cycle and graph interface with teammates (Section 1).
2. Implement fees and the break-even sweep (Section 2).
3. Implement the spread-based market generator (Section 3).
4. Add epsilon handling and precision tests (Section 4).
5. Add slippage and the profit-vs-capital search (Section 5).
6. Add capital constraints (Section 7).
7. Add the Monte Carlo delay model (Section 6) if time allows.
8. Optionally run the real-data detector and lifetime analysis (Section 10).
9. Produce plots: opportunities versus fee, opportunities versus spread, profit versus capital, and probability of profit versus delay.

Steps 1 to 4 cover what your proposal requires. Steps 5 to 9 are what would make the realism layer stand out.

## 12. Summary

The realism layer does not search for arbitrage. It decides which detected cycles are **real**. Fees and spreads shrink returns, precision handling removes false positives, liquidity and capital limits cap how much can be earned, and delay with competition erodes what remains. Measuring how each step reduces the number and size of profitable cycles is the core result of this module, and it mirrors why professional arbitrage requires speed, low cost, and careful risk control.