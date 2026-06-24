#pragma once

#include <cstdint>

namespace lob {

using Price = int64_t;      // fixed point ticks, eg: 10050 = $100.50
using Quantity = uint32_t;  // max ~4 billion shares per order, plenty
using OrderId = uint64_t;   // monotonically increasing, never reused
using Timestamp = int64_t; // nanoseconds since epoch

enum class Side : uint8_t {
    Buy = 0,
    Sell = 1
};

} // namespace lob