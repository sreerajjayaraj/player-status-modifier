#pragma once
#include <array>
#include <algorithm>
#include <cstdint>

namespace resource_hud {
inline bool IsNegativeContribution(int64_t base, int64_t discount, int64_t bonus) {
    // Native modifier evaluation prioritizes discount over bonus. This tests
    // a surviving component, never the signed delta from adding/removing it.
    return base < 0 && discount >= 0 && discount < 1000000 && bonus >= 0;
}
class State {
public:
    void Reset() { *this = {}; }
    void RecordCost(unsigned role, int64_t before, int64_t after, int64_t maximum, uint64_t now) {
        if (role >= costs_.size() || maximum <= 0 || before > maximum ||
            after < 0 || after >= before) return;
        auto& cost = costs_[role];
        cost = {after, maximum, now, true};
    }
    void SetContinuous(unsigned mask) { continuous_ = mask & 3; }
    bool Active(unsigned role, uint64_t now) const {
        return role < costs_.size() &&
            ((continuous_ & (1u << role)) || Fresh(costs_[role], now, 250));
    }
    int64_t Display(unsigned role, int64_t current, int64_t maximum, uint64_t now) const {
        if (role >= costs_.size()) return current;
        const auto& cost = costs_[role];
        // A short presentation event uses the actual committed balance. The
        // authoritative resource can recover immediately, including to full.
        if (cost.maximum != maximum || !Fresh(cost, now, 120)) return current;
        return std::min(current, cost.after / 1000 * 1000);
    }
private:
    struct Cost { int64_t after = 0, maximum = 0; uint64_t tick = 0; bool valid = false; };
    static bool Fresh(const Cost& cost, uint64_t now, uint64_t duration) {
        return cost.valid && now >= cost.tick && now - cost.tick <= duration;
    }
    std::array<Cost, 2> costs_{};
    unsigned continuous_ = 0;
};
}
