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

// outcome of every call that can change the book
// [[nodiscard]] on the type : ignoring the returned Status is a warning (an error under -Werror)
enum class [[nodiscard]] Status : uint8_t {
    Ok,
    DuplicateId,    // id collides with an order currently resting in the book
    UnknownId,      // cancel/modify for an order that is not resting
    InvalidQty      // qty == 0 on add/market
};


// RULE : every new field added later gets a default member initializer.
// so that the existing call sites keep compiling
struct OrderRequest {
    OrderId id;
    Side side;
    Price price;        // ignored by market_order
    Quantity qty;
    Timestamp ts = 0;
};

// Cancel/replace of a resting order
struct ModifyRequest {
    OrderId id;
    Price new_price;
    Quantity new_qty;      // 0 = cancel order
    Timestamp ts = 0;
};

// Contract: if the Status is not Ok, neither the book nor the 'trades' change
// Trades are appended to 'trades', the caller decides when to clear it
Status add_order(OrderBook &book, const OrderRequest& req, std::vector<Trade>& trades);
Status cancel_order(OrderBook& book, OrderId id);
Status modify_order(OrderBook& book, const ModifyRequest& req, std::vector<Trade>& trades);

// market order: sweeps opposite side, appends all trades done. unfilled remainder is dropped (IOC)
Status market_order(OrderBook& book, const OrderRequest& req, std::vector<Trade>& trades);

// best price queries: O(log n) tree traversal
Price best_bid(const OrderBook& book);
Price best_ask(const OrderBook& book);

} // namespace lob