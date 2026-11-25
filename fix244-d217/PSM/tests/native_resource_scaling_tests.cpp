#include "runtime/native_resource_scaling.h"

#include <cstdio>
#include <limits>

int main() {
    using psm::native_resource::ScaleSigned;
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name) {
        if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    check(ScaleSigned(-114000, 1, 100) == -114000, "neutral cost is unchanged by heal multiplier");
    check(ScaleSigned(-114000, .5, 100) == -57000, "continuous expense scales consumption");
    check(ScaleSigned(2000, .5, 20) == 40000, "continuous positive rate scales recovery");
    check(ScaleSigned(-10000, .75, 20) == -7500, "one-time Spirit cost uses configured consumption");
    check(ScaleSigned(-10000, 10, 20) == -100000, "increased cost is not clamped against stale cached amount");
    check(ScaleSigned(4000, 10, .5) == 2000, "one-time healing is separate from expense");
    check(ScaleSigned(0, 10, 100) == 0, "zero rate remains zero");
    check(ScaleSigned(-10000, 0, 100) == 0, "zero consumption disables cost");
    check(ScaleSigned(10000, 1, 0) == 0, "zero recovery disables rate");
    std::int64_t native_rate = 2000;
    native_rate += -114000;
    check(ScaleSigned(native_rate, .5, 100) == -56000, "evaluate aggregate rate not negative modifier alone");
    native_rate += 114000;
    check(native_rate == 2000 && ScaleSigned(native_rate, .5, 100) == 200000,
          "removing action restores native rate without asymmetric scaled reversal");
    check(ScaleSigned(-9000, 1, 1) == -9000 && ScaleSigned(9000, 1, 1) == 9000, "neutral matches vanilla both directions");
    check(ScaleSigned(100, 1, std::numeric_limits<double>::infinity()) == 100, "nonfinite config is rejected");
    check(ScaleSigned(-100, -1, 1) == -100, "invalid negative config is rejected");
    check(ScaleSigned((std::numeric_limits<std::int64_t>::max)(), 1, 100)
              == (std::numeric_limits<std::int64_t>::max)(), "overflow saturates");
    if (!failures) std::puts("native resource scaling: all checks passed");
    return failures ? 1 : 0;
}
