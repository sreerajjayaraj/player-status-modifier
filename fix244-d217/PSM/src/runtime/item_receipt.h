#pragma once

#include "mod_logic.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace psm::items {
using Receipt = std::array<std::byte, 0xC8>;

inline int64_t Quantity(const Receipt& item) {
    int64_t value = 0;
    std::memcpy(&value, item.data() + 0x10, sizeof(value));
    return value;
}

inline bool PrepareReceipt(const Receipt& original, const int64_t current,
                           Receipt& adjusted, const char** reason = nullptr) {
    adjusted = original;
    const auto amount = Quantity(original);
    int64_t scaled = amount;
    const auto declined = [reason](const char* why) {
        if (reason) *reason = why;
        return false;
    };
    if (current < 0) return declined("negative-destination-quantity");
    if (!TryScaleItemGain(amount, &scaled)) return declined("native-scaling-declined");
    if (scaled > std::numeric_limits<int64_t>::max() - current) return declined("destination-sum-overflow");
    std::memcpy(adjusted.data() + 0x10, &scaled, sizeof(scaled));
    if (reason) *reason = "prepared";
    return true;
}

inline bool FitsPlannedOccupancy(int64_t current, int64_t original, int64_t adjusted,
                                int64_t stack_size) {
    if (current < 0 || original <= 0 || adjusted < original || stack_size <= 1 ||
        adjusted > std::numeric_limits<int64_t>::max() - current) return false;
    const auto slots = [stack_size](int64_t quantity) {
        return quantity / stack_size + (quantity % stack_size != 0);
    };
    return slots(current + original) == slots(current + adjusted);
}
} // namespace psm::items
