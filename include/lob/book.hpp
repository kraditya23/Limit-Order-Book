#pragma once
#include <unordered_map>
#include <vector>
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
    std::unordered_map<Price, PriceLevel*> bid_level_map;
    std::unordered_map<Price, PriceLevel*> ask_level_map; 

    MemoryPool<Order, ORDER_POOL_CAPACITY> order_pool;
    MemoryPool<PriceLevel, LEVEL_POOL_CAPACITY> level_pool;
};

std::vector<Trade> add_order(OrderBook &book, OrderId id, Side side, Price price, Quantity qty, Timestamp ts);
void cancel_order(OrderBook& book, OrderId id);
std::vector<Trade> modify_order(OrderBook& book, OrderId id, Price new_price, Quantity new_qty, Timestamp ts);

// market order: sweeps opposite side, returns all fills. unfilled remainder is dropped (IOC)
std::vector<Trade> market_order(OrderBook& book, OrderId id, Side side, Quantity qty, Timestamp ts);

// best price queries: O(log n) tree traversal
Price best_bid(const OrderBook& book);
Price best_ask(const OrderBook& book);

} // namespace lob