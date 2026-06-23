#pragma once
#include <unordered_map>
#include "lob/types.hpp"
#include "lob/order.hpp"
#include "lob/level.hpp"

namespace lob {

struct OrderBook {
    // Roots of the AVL trees for the Bid (Buy) and Ask (Sell) sides
    PriceLevel* bid_tree_root = nullptr;
    PriceLevel* ask_tree_root = nullptr;

    // fast lookup table for O(1) cancellations and modifications
    std::unordered_map<OrderId, Order*> order_map;
};

} // namespace lob