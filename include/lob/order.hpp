#pragma once
#include "lob/types.hpp"

namespace lob {

// forward declaration so we can point to it
struct PriceLevel;

struct Order {
    OrderId id;
    Timestamp timestamp;
    Side side;
    Price price;
    Quantity initial_qty;
    Quantity remaining_qty;

    // intrusive doubly linked list pointers for FIFO queue at a specific price
    Order* prev = nullptr;
    Order* next = nullptr;

    // Pointer back to parent price level for O(1) cancellations
    PriceLevel* parent_level = nullptr;
};

} // namespace lob