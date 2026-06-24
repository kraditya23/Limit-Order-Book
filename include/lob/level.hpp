#pragma once
#include "lob/types.hpp"
#include "lob/order.hpp"

namespace lob {

struct PriceLevel {
    Price price;
    Quantity total_volume = 0; // Sum of remaining qty of all orders at this level
    uint32_t order_count = 0;

    // Intrusive doubly linked list of orders(FIFO)
    // head_order is the oldest order
    // tail_order is the latest order
    Order* head_order = nullptr;
    Order* tail_order = nullptr;

    // Intrusive AVL tree pointers
    PriceLevel* parent = nullptr;
    PriceLevel* left = nullptr;
    PriceLevel* right = nullptr;
    int height = 1;
};

} // namespace lob