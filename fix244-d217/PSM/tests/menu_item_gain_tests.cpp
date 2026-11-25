#include "config.h"
#include "menu_item_policy_v1.h"
#include "runtime/runtime_state.h"
#include <Windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstdio>

std::uint32_t RunMenuPolicyV1Test(const psm::menu_item_policy_v1::Request*, std::int64_t*);

namespace {
void Configure(double factor, bool general = true, bool runtime = true) {
    ModConfig config{};
    config.general.enabled = general;
    config.items.gain_multiplier = factor;
    SetConfigSnapshot(L"", config);
    g_runtime_enabled.store(runtime, std::memory_order_release);
}
}  // namespace

int RunMenuItemGainTests() {
    using Request = psm::menu_item_policy_v1::Request;
    constexpr uint16_t kStone = 16892;
    constexpr DWORD kIncomingError = 0x5511;
    unsigned checks = 0;
    int failures = 0;
    const auto check = [&](bool ok, const char* why) {
        ++checks;
        if (!ok) ++failures;
        std::printf("%s: menu gain %s\n", ok ? "PASS" : "FAIL", why);
    };

    Configure(3);
    Request request{sizeof(Request), 1, kStone, 0, 0, 1, 999};
    std::int64_t out = 1;
    SetLastError(kIncomingError);
    const auto csr = _mm_getcsr();
    const auto result = RunMenuPolicyV1Test(&request, &out);
    check(result == 1 && out == 3 && request.quantity == 1 &&
          GetLastError() == kIncomingError && _mm_getcsr() == csr,
          "V1 returns one proposed quantity and preserves input and thread state");
    const std::int64_t server_plan = result == 1 ? out : request.quantity;
    const std::int64_t client_plan = result == 1 ? out : request.quantity;
    check(server_plan == 3 && client_plan == server_plan, "one result feeds both authored mirror plans");
    Configure(2);
    check(RunMenuPolicyV1Test(&request, &out) == 1 && out == 2, "current factor applies");
    request.stack_limit = 1;
    check(RunMenuPolicyV1Test(&request, &out) == 0 && out == 2, "singleton preserves original");
    request.stack_limit = 999;
    request.version = 2;
    check(RunMenuPolicyV1Test(&request, &out) == 0 && out == 2, "unknown ABI preserves original");
    request.version = 1;
    request.size = 16;
    check(RunMenuPolicyV1Test(&request, &out) == 0 && out == 2, "truncated request preserves original");
    request.size = sizeof(Request);
    request.quantity = 999999;
    check(RunMenuPolicyV1Test(&request, &out) == 0 && out == 2, "out-of-range result preserves original");
    request.quantity = 1;
    Configure(3, false);
    check(RunMenuPolicyV1Test(&request, &out) == 0 && out == 2, "disabled user config preserves original");
    Configure(3, true, false);
    check(RunMenuPolicyV1Test(&request, &out) == 0 && out == 2, "disabled runtime preserves original");
    Configure(3);
    check(RunMenuPolicyV1Test(nullptr, &out) == 0 && out == 2, "missing request leaves output untouched");
    check(RunMenuPolicyV1Test(&request, nullptr) == 0, "missing output declines the candidate");
    std::printf("Menu Add Item policy %u checks, %d failures\n", checks, failures);
    Configure(3);
    return failures;
}
