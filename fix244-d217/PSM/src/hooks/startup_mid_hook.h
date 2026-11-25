#pragma once
#include <safetyhook.hpp>
#include <cstdint>
#include <utility>
namespace psm::startup {
inline constexpr unsigned kMaxMidAttempts = 3;
inline constexpr unsigned kMidRetryDelayMs = 250;
// Every attempt must resolve the complete native contract again. Missing or
// changed contracts abort immediately; no stale address or foreign chain is used.
template<class Resolve, class Create, class Report, class Wait>
SafetyHookMid CreateValidatedMid(Resolve&& resolve, Create&& create, Report&& report, Wait&& wait) {
    for (unsigned attempt = 1; attempt <= kMaxMidAttempts; ++attempt) {
        const auto address = resolve();
        if (!address) return {};
        auto result = create(address);
        if (result) return std::move(*result);
        report(attempt, address, result.error());
        if (attempt < kMaxMidAttempts) wait(kMidRetryDelayMs);
    }
    return {};
}
struct MidFailure {
    const char* category = "unknown-mid-error";
    int mid_code = -1;
    int inline_code = -1;
    int allocator_code = -1;
    uintptr_t instruction = 0;
};
inline MidFailure Describe(const safetyhook::MidHook::Error& error) {
    MidFailure result{};
    result.mid_code = static_cast<int>(error.type);
    if (error.type == safetyhook::MidHook::Error::BAD_ALLOCATION) {
        result.category = "stub-allocation";
        result.allocator_code = static_cast<int>(error.allocator_error);
    } else if (error.type == safetyhook::MidHook::Error::BAD_INLINE_HOOK) {
        const auto& inner = error.inline_hook_error;
        result.inline_code = static_cast<int>(inner.type);
        if (inner.type == safetyhook::InlineHook::Error::BAD_ALLOCATION) {
            result.category = "trampoline-allocation";
            result.allocator_code = static_cast<int>(inner.allocator_error);
        } else {
            result.instruction = reinterpret_cast<uintptr_t>(inner.ip);
            switch (inner.type) {
            case safetyhook::InlineHook::Error::FAILED_TO_DECODE_INSTRUCTION: result.category="instruction-decode"; break;
            case safetyhook::InlineHook::Error::SHORT_JUMP_IN_TRAMPOLINE: result.category="short-jump"; break;
            case safetyhook::InlineHook::Error::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE: result.category="relative-out-of-range"; break;
            case safetyhook::InlineHook::Error::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE: result.category="unsupported-instruction"; break;
            case safetyhook::InlineHook::Error::FAILED_TO_UNPROTECT: result.category="memory-protection"; break;
            case safetyhook::InlineHook::Error::NOT_ENOUGH_SPACE: result.category="insufficient-space"; break;
            default: result.category="unknown-inline-error"; result.instruction=0; break;
            }
        }
    }
    return result;
}
}
