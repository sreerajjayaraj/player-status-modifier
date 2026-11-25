#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <intrin.h>

namespace psm::items {
// INI factors are decimal settings. The shortest round-trip decimal form of
// the frozen double preserves familiar factors such as 1.2 (5 -> 6), while
// integer arithmetic computes floor(quantity * decimal-factor) without first
// rounding a signed-qword quantity to double. No allocation or locale state.
inline bool TryScaleDecimalQuantity(int64_t amount, double factor, int64_t& target) {
    target = amount;
    if (amount <= 0 || !std::isfinite(factor) || factor <= 0) return false;
    char buffer[64]{};
    const auto converted = std::to_chars(buffer,buffer+sizeof(buffer),factor,std::chars_format::general);
    if (converted.ec != std::errc{}) return false;
    uint64_t coefficient = 0;
    int fractional_digits = 0;
    bool decimal_point = false;
    const char* cursor = buffer;
    for (; cursor != converted.ptr && *cursor != 'e' && *cursor != 'E'; ++cursor) {
        if (*cursor == '.' && !decimal_point) { decimal_point = true; continue; }
        if (*cursor < '0' || *cursor > '9') return false;
        const auto digit = uint64_t(*cursor-'0');
        if (coefficient > (std::numeric_limits<uint64_t>::max()-digit)/10) return false;
        coefficient = coefficient*10+digit;
        if (decimal_point) ++fractional_digits;
    }
    int exponent = 0;
    if (cursor != converted.ptr) {
        ++cursor;
        bool negative = false;
        if (cursor != converted.ptr && (*cursor == '+' || *cursor == '-')) { negative = *cursor == '-'; ++cursor; }
        if (cursor == converted.ptr) return false;
        for (; cursor != converted.ptr; ++cursor) {
            if (*cursor < '0' || *cursor > '9' || exponent > 400) return false;
            exponent = exponent*10+(*cursor-'0');
        }
        if (negative) exponent = -exponent;
    }
    if (!coefficient) return false;
    exponent -= fractional_digits;
    uint64_t high = 0;
    uint64_t low = _umul128(static_cast<uint64_t>(amount),coefficient,&high);
    constexpr auto maximum = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    if (exponent >= 0) {
        if (high || low > maximum) return false;
        for (int i = 0; i < exponent; ++i) {
            if (low > maximum/10) return false;
            low *= 10;
        }
    } else {
        // Divide the full 128-bit value by ten, retaining both quotient limbs.
        // Repeated floor division equals floor(product / 10^n), so fractional
        // denominators never need to fit a native integer type.
        for (int i = 0; i < -exponent; ++i) {
            uint64_t remainder = 0;
            const auto quotient_low = _udiv128(high%10,low,10,&remainder);
            high /= 10;
            low = quotient_low;
            if (!high && !low) return false; // Existing zero-result policy: passthrough.
        }
    }
    if (high || !low || low > maximum) return false;
    target = static_cast<int64_t>(low);
    return true;
}
} // namespace psm::items
