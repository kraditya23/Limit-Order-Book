#pragma once
#include "lob/types.hpp"
#include "lob/order.hpp"

namespace lob {

struct PriceLevel {
    Price price;
    uint64_t total_volume = 0; // Sum of remaining qty of all orders at this level

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
    uint32_t order_count = 0;
};

static_assert(sizeof(PriceLevel) == 64, "PriceLevel must stay exactly one 64-byte cache line");

} // namespace lob