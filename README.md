# Limit Order Book Engine

A limit order book and matching engine written in C++20. It implements price-time priority matching for limit and market orders, built on data structures chosen for predictable, cache-friendly behaviour: intrusive FIFO queues, an intrusive AVL tree of price levels, fixed-point prices and pooled order storage.

## Features

- **Limit orders** with price-time priority. An order that crosses the spread executes against resting orders at their prices, best price first and oldest order first; any remainder rests in the book.
- **Market orders** execute against the opposite side; any unfilled quantity is cancelled (immediate-or-cancel).
- **Cancel** by order id.
- **Modify** with exchange-style priority rules: reducing size at the same price keeps the order's place in the queue; changing price or increasing size moves it to the back.
- **Explicit results.** Every call that can change the book returns a `Status` (`Ok`, `DuplicateId`, `UnknownId`, `InvalidQty`). A rejected request leaves the book and the trade output untouched.
- **Fixed-point prices** (`int64_t` ticks); no floating point in the matching path.

## Performance (preliminary)

Single thread, 500,000 mixed operations on a book pre-filled with 100,000 resting orders across 5,000 price levels per side (40% resting adds, 10% crossing adds, 20% cancels, 20% modifies, 10% market orders). Median of 11 runs on an Apple M3 MacBook Air, Apple Clang 17, `-O3 -march=native`.

| Metric | Median | Range over 11 runs |
|---|---|---|
| Throughput | 6.1 M ops/s | 5.0 – 6.8 M ops/s |
| Mean time per operation | 163 ns | 148 – 202 ns |
| p99 latency per operation | ≤ 542 ns | 458 – 625 ns |
| p99.9 latency per operation | ≤ 959 ns | 833 – 1,083 ns |

Percentiles are upper bounds: each sample includes two clock reads, and the macOS timer resolves only ~42 ns. Methodology and known limitations are documented in [`bench/lob_benchmark.cpp`](bench/lob_benchmark.cpp); cycle-accurate tail latency on a pinned Linux core is on the roadmap.

## Usage

```cpp
#include "lob/book.hpp"
using namespace lob;

OrderBook book;
std::vector<Trade> trades;   // fills are appended here

(void)add_order(book, {.id = 1, .side = Side::Sell, .price = 10100, .qty = 50}, trades);  // rests
Status s = add_order(book, {.id = 2, .side = Side::Buy, .price = 10200, .qty = 30}, trades);

// s == Status::Ok
// trades: one fill, maker 1 / taker 2, 30 @ 10100 (the resting order's price)
// order 1 keeps resting with 20; best_ask(book) == 10100

if (cancel_order(book, 1) != Status::Ok) { /* order was not resting */ }
```

Requests are plain structs filled with designated initializers, so every call site names its fields and new optional fields can be added without breaking existing code. `Status` is `[[nodiscard]]`, so ignoring a result is a compiler warning.

## Design

```
OrderBook
├── bid tree, ask tree      AVL trees of PriceLevel, ordered by price
│     └── PriceLevel  ──►  Order ⇄ Order ⇄ Order      intrusive FIFO, oldest first
├── order_map               OrderId → Order*           cancel / modify lookup
├── bid / ask level maps    Price → PriceLevel*        existing-level lookup on insert
└── memory pools            Order, PriceLevel          preallocated, free-list reuse
```

- **One node, two roles.** `PriceLevel` is both a node of the AVL tree and the head of a doubly linked list of its orders. Each order links to its neighbours and its level directly, so cancelling is a pointer splice with no search.
- **Cache-line sized records.** `Order` and `PriceLevel` are exactly 64 bytes, enforced with `static_assert`; fields are ordered so both fit without growing past one line.
- **Memory pools.** Orders and price levels come from fixed-capacity pools (raw aligned storage, placement new, bump pointer plus free list), so they are never individually heap-allocated.
- **Iterative AVL tree with parent pointers.** Insertion and removal rebalance upward from the changed node. Removing a level with two children moves its in-order successor into place rather than copying data, so pointers held elsewhere stay valid.
- **Separate bid and ask level maps.** An earlier shared map let a bid and an ask at the same price overwrite each other, a bug found during stress testing.

| Operation | Cost (L = price levels on a side) |
|---|---|
| Add without matching | O(log L): best-price check on the opposite side, plus a tree insert if the level is new |
| Cancel / reduce size | O(1) average; O(log L) if the level becomes empty |
| Best bid / best ask | O(log L) |
| Match across k price levels | O(k log L) plus one step per fill |

## Build

Requires a C++20 compiler and CMake 3.14+. Builds with GCC 13, Clang 19 and Apple Clang 17.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                      # builds the lob_core static library
```

Interim benchmark harness and AVL stress test:

```bash
g++ -std=c++20 -O3 -march=native -DNDEBUG -Iinclude src/book.cpp bench/lob_benchmark.cpp -o lob_benchmark
g++ -std=c++20 -O2 -I. tests/standalone_avl.cpp tests/test_standalone.cpp -o avl_test
```

## Project layout

```
include/lob/   public headers: types, order, price level, memory pool, book API
src/           matching engine and AVL tree implementation
bench/         interim throughput / latency harness
tests/         standalone AVL reference implementation and invariant stress test
```

## Roadmap

The matching engine core is complete. Next:

- **Test suite:** GoogleTest unit and behaviour tests, an invariant checker, differential fuzzing against a reference order book, sanitizer builds, and CI on Linux and macOS.
- **Market data:** a Nasdaq TotalView-ITCH 5.0 parser and a book builder that reconstructs the full-depth book for every symbol over a full trading day, sharing this engine's data structures.
- **Benchmarks:** Google Benchmark microbenchmarks and full-day ITCH replay, with tail latency (p50 / p99 / p99.9) measured on a pinned Linux core.
- **Optimisation**, each change measured before and after: cached best prices, an open-addressing hash map, cache-line alignment, and array-based price levels near the top of the book.
- **Later:** time-in-force (IOC / FOK), self-trade prevention, Python bindings.

## License

MIT