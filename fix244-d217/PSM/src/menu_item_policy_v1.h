
#pragma once

#include "runtime/item_gain_math.h"
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace psm::menu_item_policy_v1 {
// Win64 contract from the provider's versioned Add Item policy export.
// The provider supplies validated native stack metadata and consumes one
// quantity for both mirrored plans. The callback never invokes the provider.
struct Request {
    std::uint32_t size;
    std::uint32_t version;
    std::uint16_t item_key;
    std::uint16_t reserved16;
    std::uint32_t reserved32;
    std::int64_t quantity;
    std::int64_t stack_limit;
};
static_assert(sizeof(Request) == 32 && offsetof(Request, quantity) == 16);

inline bool Quantity(const Request& request, double multiplier, std::int64_t& result) {
    constexpr std::int64_t max_quantity = 999999;
    if (request.size < sizeof(Request) || request.version != 1 ||
        request.item_key == 0xFFFF || request.reserved16 || request.reserved32 ||
        request.quantity < 1 || request.quantity > max_quantity || request.stack_limit <= 1 ||
        !std::isfinite(multiplier) || multiplier <= 0 || multiplier == 1) return false;
    std::int64_t candidate = request.quantity;
    if (!items::TryScaleDecimalQuantity(request.quantity, multiplier, candidate) ||
        candidate < 1 || candidate > max_quantity || candidate == request.quantity) return false;
    result = candidate;
    return true;
}
}
