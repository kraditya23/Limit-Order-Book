// bench/lob_benchmark.cpp
//
// Interim throughput / latency harness for the order book.
// It will be superseded by the Google Benchmark suite (see README roadmap) and
// is kept so that the preliminary numbers in the README have a reproducible source.
//
// Build (release flags, matching CMakeLists.txt):
//   g++ -std=c++20 -O3 -march=native -DNDEBUG -Iinclude src/book.cpp bench/lob_benchmark.cpp -o lob_benchmark
// Run:
//   ./lob_benchmark
//
// Runs:
//   Run 1 - aggregate throughput: a single timer around THROUGHPUT_OPS mixed
//           operations on a pre-warmed book. Reports ops/sec and mean time per
//           operation (total time / total operations).
//   Run 2 - per-operation latency distribution: each of LATENCY_SAMPLE_OPS
//           operations is timed individually. Every sample includes two clock
//           reads and is limited by the clock's resolution, so the reported
//           percentiles are upper bounds rather than exact per-operation costs.
//
// Workload (fixed seed):
//   - 40% resting limit adds, 10% crossing limit adds, 20% cancels,
//     20% modifies (70% in-place size reduction, 30% reprice / size change),
//     10% market orders.
//   - Prices are spread over DEPTH_RANGE levels per side around MID so that
//     activity is not concentrated on a single price level.
//
// Known limitations:
//   - The driver does not observe fills, so some cancel/modify targets have
//     already been fully executed by crossing or market orders. The book
//     rejects these with Status::UnknownId; they are reported as dead
//     cancels/modifies and are cheaper than real ones.
//   - Run 1's timer also covers workload generation (RNG draws and live-order
//     bookkeeping), not only book operations.
//   - std::uniform_int_distribution is implementation-defined, so libstdc++ and
//     libc++ builds generate different operation streams from the same seed.
//     Only compare results produced with the same standard library.

#include "lob/book.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <optional>
#include <random>
#include <vector>

using namespace lob;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::size_t WARMUP_ORDERS      = 100'000;  // untimed, builds book depth
constexpr std::size_t THROUGHPUT_OPS     = 500'000;  // Run 1
constexpr std::size_t LATENCY_SAMPLE_OPS = 100'000;  // Run 2

constexpr Price MID         = 100'000;  // reference mid price in ticks
constexpr Price SPREAD      = 10;       // gap between the bid band and the ask band
constexpr Price DEPTH_RANGE = 5'000;    // number of distinct price levels per side

std::mt19937_64 rng(42);  // fixed seed for reproducible operation streams

OrderId next_id_counter = 1;
OrderId next_id() { return next_id_counter++; }

Quantity random_qty() {
    std::uniform_int_distribution<Quantity> d(1, 1000);
    return d(rng);
}

Side random_side() {
    std::uniform_int_distribution<int> d(0, 1);
    return d(rng) == 0 ? Side::Buy : Side::Sell;
}

// Price that never crosses: bids below MID - SPREAD, asks above MID + SPREAD.
Price random_resting_price(Side side) {
    std::uniform_int_distribution<Price> d(0, DEPTH_RANGE);
    return (side == Side::Buy) ? (MID - SPREAD - d(rng))
                               : (MID + SPREAD + d(rng));
}

// Price inside the opposite side's resting band, so the order crosses.
Price random_crossing_price(Side side) {
    std::uniform_int_distribution<Price> d(0, DEPTH_RANGE);
    return (side == Side::Buy) ? (MID + SPREAD + d(rng))
                               : (MID - SPREAD - d(rng));
}

// Total quantity filled for `taker_id` in the given trades.
Quantity filled_qty(const std::vector<Trade>& trades, OrderId taker_id) {
    Quantity total = 0;
    for (const auto& t : trades) if (t.taker_id == taker_id) total += t.qty;
    return total;
}

// Driver-side record of an order believed to be resting; used to pick
// cancel/modify targets. May be stale once the order is filled by another order.
struct LiveOrder {
    OrderId id;
    Side side;
    Price price;
    Quantity remaining;
};

// Removes and returns a uniformly random element (swap-with-last, O(1)).
bool pop_random(std::vector<LiveOrder>& v, LiveOrder& out) {
    if (v.empty()) return false;
    std::uniform_int_distribution<std::size_t> d(0, v.size() - 1);
    std::size_t idx = d(rng);
    out = v[idx];
    v[idx] = v.back();
    v.pop_back();
    return true;
}

// Picks a uniformly random index so the caller can update the element in place.
bool peek_random_index(const std::vector<LiveOrder>& v, std::size_t& idx_out) {
    if (v.empty()) return false;
    std::uniform_int_distribution<std::size_t> d(0, v.size() - 1);
    idx_out = d(rng);
    return true;
}

struct Counters {
    std::size_t adds_rest = 0, adds_cross = 0;
    std::size_t cancels = 0, modify_inplace = 0, modify_reprice = 0;
    std::size_t markets = 0, trades = 0;
    std::size_t dead_cancels = 0, dead_modifies = 0;  // targets already filled (Status::UnknownId)
};

// Executes one randomly chosen operation. When `latencies` is non-null the
// operation is timed individually and the duration (ns) is appended to it.
void run_one_op(OrderBook& book, std::vector<LiveOrder>& live, Counters& c,
                Timestamp ts, std::vector<double>* latencies, std::vector<Trade>& trade_buf) {
    std::uniform_int_distribution<int> op_picker(0, 99);
    int roll = op_picker(rng);

    std::optional<Clock::time_point> t0;
    if (latencies) t0 = Clock::now();

    trade_buf.clear();  // keeps capacity, so no allocation in steady state

    if (roll < 40) {                                    // resting limit add
        Side side = random_side();
        Price price = random_resting_price(side);
        Quantity qty = random_qty();
        OrderId id = next_id();
        (void)add_order(book, {.id = id, .side = side, .price = price, .qty = qty, .ts = ts}, trade_buf);
        c.trades += trade_buf.size();
        c.adds_rest++;
        live.push_back({id, side, price, qty});
    } else if (roll < 50) {                             // crossing limit add
        Side side = random_side();
        Price price = random_crossing_price(side);
        Quantity qty = random_qty();
        OrderId id = next_id();
        (void)add_order(book, {.id = id, .side = side, .price = price, .qty = qty, .ts = ts}, trade_buf);
        c.trades += trade_buf.size();
        c.adds_cross++;
        Quantity filled = filled_qty(trade_buf, id);
        if (filled < qty) live.push_back({id, side, price, static_cast<Quantity>(qty - filled)});
    } else if (roll < 70) {                             // cancel
        LiveOrder lo;
        if (pop_random(live, lo)) {
            if (cancel_order(book, lo.id) == Status::UnknownId) c.dead_cancels++;
            c.cancels++;
        }
    } else if (roll < 90) {                             // modify
        std::size_t idx;
        if (peek_random_index(live, idx)) {
            LiveOrder& lo = live[idx];
            std::uniform_int_distribution<int> reprice_roll(0, 9);
            if (reprice_roll(rng) < 7 && lo.remaining > 1) {
                // in-place size reduction at the same price (keeps time priority)
                Quantity new_qty = std::uniform_int_distribution<Quantity>(1, lo.remaining - 1)(rng);
                if (modify_order(book, {.id = lo.id, .new_price = lo.price, .new_qty = new_qty, .ts = ts},
                                 trade_buf) == Status::UnknownId) c.dead_modifies++;
                c.trades += trade_buf.size();
                lo.remaining = new_qty;
                c.modify_inplace++;
            } else {
                // reprice to a new non-crossing level on the same side (cancel + re-add)
                Price new_price = random_resting_price(lo.side);
                Quantity new_qty = random_qty();
                if (modify_order(book, {.id = lo.id, .new_price = new_price, .new_qty = new_qty, .ts = ts},
                                 trade_buf) == Status::UnknownId) c.dead_modifies++;
                c.trades += trade_buf.size();
                lo.price = new_price;
                lo.remaining = new_qty;
                c.modify_reprice++;
            }
        }
    } else {                                            // market order (IOC)
        Side side = random_side();
        OrderId id = next_id();
        (void)market_order(book, {.id = id, .side = side, .price = 0, .qty = random_qty(), .ts = ts}, trade_buf);
        c.trades += trade_buf.size();
        c.markets++;
    }

    if (latencies) {
        auto t1 = Clock::now();
        latencies->push_back(std::chrono::duration<double, std::nano>(t1 - *t0).count());
    }
}

void print_counters(const Counters& c, std::size_t total_ops) {
    std::printf("Total ops:          %zu\n", total_ops);
    std::printf("  adds (rest):      %zu\n", c.adds_rest);
    std::printf("  adds (crossing):  %zu\n", c.adds_cross);
    std::printf("  cancels:          %zu\n", c.cancels);
    std::printf("  modify (inplace): %zu\n", c.modify_inplace);
    std::printf("  modify (reprice): %zu\n", c.modify_reprice);
    std::printf("  market orders:    %zu\n", c.markets);
    std::printf("  trades produced:  %zu\n", c.trades);
    std::printf("  dead cancels:     %zu\n", c.dead_cancels);
    std::printf("  dead modifies:    %zu\n", c.dead_modifies);
}

} // namespace

int main() {
    std::puts("=== LOB Benchmark (interim harness) ===\n");

    OrderBook book;
    std::vector<LiveOrder> live;
    live.reserve(WARMUP_ORDERS + THROUGHPUT_OPS);

    std::vector<Trade> trade_buf;
    trade_buf.reserve(8);  // reused for every call; grows at most a few times

    // ---- Warmup: build book depth (untimed) ----
    for (std::size_t i = 0; i < WARMUP_ORDERS; ++i) {
        Side side = random_side();
        Price price = random_resting_price(side);
        Quantity qty = random_qty();
        OrderId id = next_id();
        trade_buf.clear();
        (void)add_order(book, {.id = id, .side = side, .price = price, .qty = qty}, trade_buf);
        live.push_back({id, side, price, qty});
    }
    std::printf("Warmup complete: %zu resting orders built.\n\n", live.size());

    // ---- Run 1: aggregate throughput ----
    Counters c1;
    auto t0 = Clock::now();
    for (std::size_t i = 0; i < THROUGHPUT_OPS; ++i) {
        run_one_op(book, live, c1, static_cast<Timestamp>(i), nullptr, trade_buf);
    }
    auto t1 = Clock::now();

    double total_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double ops_per_sec = (static_cast<double>(THROUGHPUT_OPS) / total_ns) * 1e9;
    double avg_ns_per_op = total_ns / static_cast<double>(THROUGHPUT_OPS);

    std::printf("---- Run 1: Aggregate Throughput ----\n");
    print_counters(c1, THROUGHPUT_OPS);
    std::printf("Wall time:          %.3f ms\n", total_ns / 1e6);
    std::printf("Throughput:         %.0f ops/sec\n", ops_per_sec);
    std::printf("Avg latency:        %.1f ns/op\n\n", avg_ns_per_op);

    // ---- Run 2: per-operation latency distribution (upper bounds) ----
    Counters c2;
    std::vector<double> latencies;
    latencies.reserve(LATENCY_SAMPLE_OPS);
    for (std::size_t i = 0; i < LATENCY_SAMPLE_OPS; ++i) {
        run_one_op(book, live, c2, static_cast<Timestamp>(i + THROUGHPUT_OPS), &latencies, trade_buf);
    }

    std::sort(latencies.begin(), latencies.end());
    double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    double p50 = latencies[latencies.size() * 50 / 100];
    double p99 = latencies[latencies.size() * 99 / 100];
    double p999 = latencies[std::min(latencies.size() - 1, latencies.size() * 999 / 1000)];

    std::printf("---- Run 2: Per-Op Latency Distribution (upper bound, includes timer overhead) ----\n");
    std::printf("Samples:  %zu\n", latencies.size());
    std::printf("Mean:     %.1f ns\n", mean);
    std::printf("p50:      %.1f ns\n", p50);
    std::printf("p99:      %.1f ns\n", p99);
    std::printf("p99.9:    %.1f ns\n", p999);

    return 0;
}