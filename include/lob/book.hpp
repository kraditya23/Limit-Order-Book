#pragma once
#include <unordered_map>
#include "lob/types.hpp"
#include "lob/order.hpp"
#include "lob/level.hpp"
#include "lob/memory_pool.hpp"

namespace lob {

static constexpr std::size_t ORDER_POOL_CAPACITY = 1 << 20; // 1M orders
static constexpr std::size_t LEVEL_POOL_CAPACITY = 1 << 16; // 64K price levels

struct OrderBook {
    // Roots of the AVL trees for the Bid (Buy) and Ask (Sell) sides
    PriceLevel* bid_tree_root = nullptr;
    PriceLevel* ask_tree_root = nullptr;

    // fast lookup table for O(1) cancellations and modifications
    std::unordered_map<OrderId, Order*> order_map;
    std::unordered_map<Price, PriceLevel*> level_map;

    MemoryPool<Order, ORDER_POOL_CAPACITY> order_pool;
    MemoryPool<PriceLevel, LEVEL_POOL_CAPACITY> level_pool;
};

void add_order(OrderBook &book, OrderId id, Side side, Price price, Quantity qty, Timestamp ts);

} // namespace lob