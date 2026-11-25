#pragma once

#include <cstdint>
#include <cmath>
#include <limits>

namespace psm::native_resource {

inline std::int64_t ScaleSigned(const std::int64_t value,
                                const double consumption,
                                const double recovery) {
    const double multiplier = value < 0 ? consumption : recovery;
    if (value == 0 || multiplier == 1.0 || !std::isfinite(multiplier) || multiplier < 0) {
        return value;
    }
    const long double scaled = static_cast<long double>(value) * multiplier;
    constexpr auto lo = (std::numeric_limits<std::int64_t>::min)();
    constexpr auto hi = (std::numeric_limits<std::int64_t>::max)();
    if (scaled <= static_cast<long double>(lo)) return lo;
    if (scaled >= static_cast<long double>(hi)) return hi;
    return static_cast<std::int64_t>(scaled);
}

}  // namespace psm::native_resource
