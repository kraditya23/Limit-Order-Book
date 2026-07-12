# Limit Order Book Engine

A low-latency limit order book engine written in C++20, built around the same core primitives production matching engines use: fixed-point pricing, intrusive FIFO queues, O(1) hot-path operations, and a custom memory allocator to keep the matching loop off the heap.

**Throughput: 6.8M+ orders/sec · ~147ns average latency per operation** (see [Benchmarks](#benchmarks))

## Why this exists

Most "order book" projects on GitHub are `std::map<price, std::queue<Order>>` wrappers that allocate on every insert and call it a day. This one is built the way a real matching engine is built: no floating point anywhere in the pricing path, no heap allocation on the hot path, O(1) cancel and O(1) best-bid/ask, and a memory pool with placement-new to avoid the double-construction UB that naive pool implementations fall into.

## Architecture

**Price levels — AVL tree**
Each price level is a node in a self-balancing AVL tree, giving O(log n) level insertion/removal and O(1) access to best bid/ask via cached leftmost/rightmost pointers. Each level owns an intrusive FIFO queue of resting orders, preserving strict price-time priority.

**Order tracking — dual hash maps**
- `order_map`: order ID → order location, for O(1) cancel/modify
- `bid_level_map` / `ask_level_map`: price → level node, for O(1) level lookup on the insert hot path (split by side to eliminate bid/ask collisions at the same price — a bug caught during stress testing on a shared map)

**Memory pool**
`MemoryPool<T, Capacity>` uses bump-pointer allocation with a free-list for reuse — zero heap allocation once the pool is warmed. Storage is raw bytes with placement-new/manual destructor calls, avoiding the double-construction undefined behavior that a naive `std::vector<T>`-backed pool would hit.

**Pricing**
All prices are fixed-point `int64_t`. No floating point anywhere in the matching path — deterministic, no rounding drift, no cross-platform float inconsistency.

**Matching**
`add_order` sweeps the opposite side of the book at the order's limit price before resting any remainder, so the book is provably never left in a crossed state. Matching returns `std::vector<Trade>` directly rather than allocating through callbacks.

## Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

Requires a C++20 compiler (GCC 11+ / Clang 14+) and CMake 3.16+.

## Benchmarks

Measured via a `chrono`-based harness under `Release` build (`-O3`), single-instrument, mixed add/cancel/modify workload:

| Metric | Result |
|---|---|
| Throughput | 6.8M+ ops/sec |
| Average latency | ~147ns/op |
| Allocation on hot path | None (post-warmup) |

Full benchmark methodology and reproducible harness: `benchmarks/` *(Google Benchmark suite landing in Stage 9 — see Roadmap)*.

## Current Status

| Stage | Description | Status |
|---|---|---|
| 1 | Scaffolding — CMake, core headers | Done |
| 2 | Standalone AVL tree + stress test | Done |
| 3 | Memory pool (bump-pointer + free-list) | Done |
| 4 | `add_order` — book insertion, level map + AVL sync | Done |
| 5 | `cancel_order` — O(1) removal via order_map | Done |
| 6 | `modify_order` — qty reduction / price-time priority | Done |
| 7 | Market order matching / sweep logic | Done |
| 7.1 | Crossing limit orders — `add_order` sweeps opposite book before resting | Done |
| 7.2 | Self-trade prevention | In progress |
| 7.3 | Time-in-force (IOC, FOK, day-order expiry) | Planned |
| 7.4 | Order validation / rejection at API boundary | Planned |
| 7.5 | Multi-instrument support (per-symbol book + router) | Planned |
| 8 | GoogleTest suite (AVL tree, memory pool, matching correctness) | Planned |
| 9 | Google Benchmark — formal, reproducible perf suite | Planned |
| 10 | pybind11 — Python bindings for research/backtesting use | Planned |

## Roadmap

Beyond Stage 10, in rough priority order:
- **Persistence** — write-ahead log for crash recovery / order book reconstruction
- **Market data publishing** — top-of-book and full-depth feed output (ITCH-style binary encoding)
- **Network layer** — TCP/UDP order entry gateway, likely with kernel-bypass (DPDK) as a stretch goal
- **Lock-free multi-threading** — currently single-threaded by design (simplifies correctness reasoning); a lock-free SPSC/MPSC path is the natural next step once correctness is locked down by the GoogleTest suite

## Design Principles

1. **Correctness before speed** — every optimization (memory pool, split level maps, void-return cancel) followed a stress test that exposed a real bug, not a hypothetical one.
2. **No hidden allocation** — if it's on the matching path, it doesn't call `new`.
3. **No floating point** — fixed-point pricing throughout; determinism matters more than convenience.
4. **Benchmarks are load-bearing** — numbers in this README come from a real harness in this repo, not estimates.

## License

MIT