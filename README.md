# Limit Order Book Engine

A high-performance C++20 limit order book engine built for low-latency trading systems.

## Architecture

- **Price levels** implemented as AVL tree nodes with intrusive FIFO order queues — O(log n) level insertion, O(1) best bid/ask
- **Order tracking** via dual hash maps (`order_map`, `level_map`) for O(1) cancel and O(1) level lookup on the insert hot path
- **Memory pool** (`MemoryPool<T, Capacity>`) with bump-pointer + free-list allocation — zero heap allocation on the hot path
- Fixed-point `int64_t` pricing, no floating point anywhere

## Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Current Status

| Stage | Description | Status |
|-------|-------------|--------|
| 1 | Scaffolding — CMake, core headers | Done |
| 2 | Standalone AVL tree + stress test | Done |
| 3 | Memory pool | Done |
| 4 | `add_order` — book insertion, level_map + AVL sync | Done |
| 5 | `cancel_order` — O(1) removal via order_map | Done |
| 6 | `modify_order` — qty reduction / price-time priority | Done |
| 7 | Market order matching / sweep logic | Current |
| 8 | GoogleTest suite | Pending |
| 9 | Google Benchmark | Pending |
| 10 | pybind11 Python bindings | Pending |
