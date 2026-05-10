#include "hooks/hooks_internal.h"

#include "config.h"
#include "logger.h"
#include "runtime/affinity_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

std::atomic<std::uint32_t> g_affinity_current_samples{0};
std::atomic<std::uint32_t> g_affinity_hit_samples{0};
std::atomic<std::uint32_t> g_affinity_item_event_samples{0};
std::atomic<std::uint32_t> g_affinity_pet_diag_samples{0};

SafetyHookMid g_affinity_item_give_probe_hook{};
SafetyHookMid g_affinity_item_take_probe_hook{};

constexpr std::size_t kFriendlyDataCopySize = 0x40;
constexpr uintptr_t kFriendlyVaryDeltaOffset = 0x20;

struct alignas(16) FriendlyDataScratch {
    std::array<std::uint8_t, kFriendlyDataCopySize> bytes{};
};

template <std::size_t N>
bool ExpectBytes(const uintptr_t address, const std::array<std::uint8_t, N>& bytes) {
    if (address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        return std::memcmp(reinterpret_cast<const void*>(address), bytes.data(), bytes.size()) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool TryReadValue(const uintptr_t address, T* const value) {
    if (address < kMinimumPointerAddress || value == nullptr) {
        return false;
    }

    __try {
        *value = *reinterpret_cast<const T*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryStoreAffinityValue(const uintptr_t address, const int64_t value) {
    if (address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *reinterpret_cast<int64_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryReadAscii(const uintptr_t address, char* const buffer, const std::size_t size) {
    if (address < kMinimumPointerAddress || buffer == nullptr || size == 0) {
        return false;
    }

    __try {
        std::size_t i = 0;
        for (; i + 1 < size; ++i) {
            const char value = *reinterpret_cast<const char*>(address + i);
            if (value == '\0') {
                break;
            }
            if (static_cast<unsigned char>(value) < 0x20 || static_cast<unsigned char>(value) > 0x7E) {
                buffer[i] = '.';
            } else {
                buffer[i] = value;
            }
        }
        buffer[i] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        buffer[0] = '\0';
        return false;
    }
}

const char* PrintableText(const char* const text) {
    return text != nullptr && text[0] != '\0' ? text : "<unreadable>";
}

void LogAffinityItemEventProbe(const char* const path_name, SafetyHookContext& ctx) {
    const auto config = GetConfig();
    const std::uint32_t sample_limit = config.affinity.gift_diagnostics ? 256U : 32U;
    if (!ShouldLogSample(g_affinity_item_event_samples, sample_limit)) {
        return;
    }

    uintptr_t event_stack_ptr = 0;
    uintptr_t event_trampoline_stack_ptr = 0;
    int32_t rdx_i32 = 0;
    int32_t r8_i32 = 0;
    int32_t r9_i32 = 0;
    uint64_t rdx_q00 = 0;
    uint64_t rdx_q08 = 0;
    uint64_t rdx_q10 = 0;
    uint64_t r8_q00 = 0;
    uint64_t r9_q00 = 0;
    int32_t rbp_0324 = 0;
    int32_t rbp_0328 = 0;
    int32_t rbp_0ea8 = 0;
    int32_t rbp_0eac = 0;
    int32_t rbp_0eb0 = 0;
    int32_t rbp_0eb4 = 0;
    uintptr_t rsp_m20 = 0;
    uintptr_t rsp_m18 = 0;
    uintptr_t rsp_m10 = 0;
    uintptr_t rsp_m08 = 0;
    uintptr_t rsp_p00 = 0;
    uintptr_t rsp_p08 = 0;
    uintptr_t rsp_p10 = 0;
    uintptr_t rsp_p18 = 0;
    uintptr_t rsp_p20 = 0;
    uintptr_t rsp_p28 = 0;
    uintptr_t rsp_p30 = 0;
    uintptr_t rsp_p38 = 0;
    uintptr_t rsp_p40 = 0;
    uintptr_t rsp_p48 = 0;
    uintptr_t rsp_p50 = 0;
    uintptr_t rsp_p58 = 0;
    uintptr_t rsp_p60 = 0;
    char event_r14_name[80]{};
    char event_stack_name[80]{};
    char event_trampoline_stack_name[80]{};

    TryReadValue(ctx.rsp + 0x20, &event_stack_ptr);
    TryReadValue(ctx.trampoline_rsp + 0x20, &event_trampoline_stack_ptr);
    TryReadValue(ctx.rdx, &rdx_i32);
    TryReadValue(ctx.r8, &r8_i32);
    TryReadValue(ctx.r9, &r9_i32);
    TryReadValue(ctx.rdx + 0x00, &rdx_q00);
    TryReadValue(ctx.rdx + 0x08, &rdx_q08);
    TryReadValue(ctx.rdx + 0x10, &rdx_q10);
    TryReadValue(ctx.r8 + 0x00, &r8_q00);
    TryReadValue(ctx.r9 + 0x00, &r9_q00);
    TryReadValue(ctx.rbp + 0x0324, &rbp_0324);
    TryReadValue(ctx.rbp + 0x0328, &rbp_0328);
    TryReadValue(ctx.rbp + 0x0EA8, &rbp_0ea8);
    TryReadValue(ctx.rbp + 0x0EAC, &rbp_0eac);
    TryReadValue(ctx.rbp + 0x0EB0, &rbp_0eb0);
    TryReadValue(ctx.rbp + 0x0EB4, &rbp_0eb4);
    TryReadValue(ctx.rsp - 0x20, &rsp_m20);
    TryReadValue(ctx.rsp - 0x18, &rsp_m18);
    TryReadValue(ctx.rsp - 0x10, &rsp_m10);
    TryReadValue(ctx.rsp - 0x08, &rsp_m08);
    TryReadValue(ctx.rsp + 0x00, &rsp_p00);
    TryReadValue(ctx.rsp + 0x08, &rsp_p08);
    TryReadValue(ctx.rsp + 0x10, &rsp_p10);
    TryReadValue(ctx.rsp + 0x18, &rsp_p18);
    TryReadValue(ctx.rsp + 0x20, &rsp_p20);
    TryReadValue(ctx.rsp + 0x28, &rsp_p28);
    TryReadValue(ctx.rsp + 0x30, &rsp_p30);
    TryReadValue(ctx.rsp + 0x38, &rsp_p38);
    TryReadValue(ctx.rsp + 0x40, &rsp_p40);
    TryReadValue(ctx.rsp + 0x48, &rsp_p48);
    TryReadValue(ctx.rsp + 0x50, &rsp_p50);
    TryReadValue(ctx.rsp + 0x58, &rsp_p58);
    TryReadValue(ctx.rsp + 0x60, &rsp_p60);
    TryReadAscii(ctx.r14, event_r14_name, sizeof(event_r14_name));
    TryReadAscii(event_stack_ptr, event_stack_name, sizeof(event_stack_name));
    TryReadAscii(event_trampoline_stack_ptr, event_trampoline_stack_name, sizeof(event_trampoline_stack_name));

    Log("hooks: affinity-item-event %s regs rcx=0x%p rbx=0x%p rbp=0x%p rsp=0x%p tramp_rsp=0x%p rdx=0x%p r8=0x%p r9=0x%p r14=0x%p/%s rsi=0x%p rdi=0x%p rip=0x%p",
        path_name,
        reinterpret_cast<void*>(ctx.rcx),
        reinterpret_cast<void*>(ctx.rbx),
        reinterpret_cast<void*>(ctx.rbp),
        reinterpret_cast<void*>(ctx.rsp),
        reinterpret_cast<void*>(ctx.trampoline_rsp),
        reinterpret_cast<void*>(ctx.rdx),
        reinterpret_cast<void*>(ctx.r8),
        reinterpret_cast<void*>(ctx.r9),
        reinterpret_cast<void*>(ctx.r14),
        PrintableText(event_r14_name),
        reinterpret_cast<void*>(ctx.rsi),
        reinterpret_cast<void*>(ctx.rdi),
        reinterpret_cast<void*>(ctx.rip));

    Log("hooks: affinity-item-event %s args stack20=0x%p/%s tramp20=0x%p/%s rdx_i32=%d r8_i32=%d r9_i32=%d rdx_q00=%llu rdx_q08=%llu rdx_q10=%llu r8_q00=%llu r9_q00=%llu rbp324=%d rbp328=%d rbpea8=%d rbpeac=%d rbpeb0=%d rbpeb4=%d",
        path_name,
        reinterpret_cast<void*>(event_stack_ptr),
        PrintableText(event_stack_name),
        reinterpret_cast<void*>(event_trampoline_stack_ptr),
        PrintableText(event_trampoline_stack_name),
        static_cast<int>(rdx_i32),
        static_cast<int>(r8_i32),
        static_cast<int>(r9_i32),
        static_cast<unsigned long long>(rdx_q00),
        static_cast<unsigned long long>(rdx_q08),
        static_cast<unsigned long long>(rdx_q10),
        static_cast<unsigned long long>(r8_q00),
        static_cast<unsigned long long>(r9_q00),
        static_cast<int>(rbp_0324),
        static_cast<int>(rbp_0328),
        static_cast<int>(rbp_0ea8),
        static_cast<int>(rbp_0eac),
        static_cast<int>(rbp_0eb0),
        static_cast<int>(rbp_0eb4));

    Log("hooks: affinity-item-event %s stack rsp_m20=0x%p rsp_m18=0x%p rsp_m10=0x%p rsp_m08=0x%p rsp_p00=0x%p rsp_p08=0x%p rsp_p10=0x%p rsp_p18=0x%p rsp_p20=0x%p rsp_p28=0x%p rsp_p30=0x%p rsp_p38=0x%p rsp_p40=0x%p rsp_p48=0x%p rsp_p50=0x%p rsp_p58=0x%p rsp_p60=0x%p",
        path_name,
        reinterpret_cast<void*>(rsp_m20),
        reinterpret_cast<void*>(rsp_m18),
        reinterpret_cast<void*>(rsp_m10),
        reinterpret_cast<void*>(rsp_m08),
        reinterpret_cast<void*>(rsp_p00),
        reinterpret_cast<void*>(rsp_p08),
        reinterpret_cast<void*>(rsp_p10),
        reinterpret_cast<void*>(rsp_p18),
        reinterpret_cast<void*>(rsp_p20),
        reinterpret_cast<void*>(rsp_p28),
        reinterpret_cast<void*>(rsp_p30),
        reinterpret_cast<void*>(rsp_p38),
        reinterpret_cast<void*>(rsp_p40),
        reinterpret_cast<void*>(rsp_p48),
        reinterpret_cast<void*>(rsp_p50),
        reinterpret_cast<void*>(rsp_p58),
        reinterpret_cast<void*>(rsp_p60));
}

void LogAffinityPetDiagnostic(const char* const path_name, SafetyHookContext& ctx) {
    const auto config = GetConfig();
    if (!config.affinity.pet_diagnostics) {
        return;
    }

    if (!ShouldLogSample(g_affinity_pet_diag_samples, 256)) {
        return;
    }

    uintptr_t rsp20 = 0;
    uintptr_t tramp20 = 0;
    uintptr_t rsp60 = 0;
    uintptr_t tramp60 = 0;
    TryReadValue(ctx.rsp + 0x20, &rsp20);
    TryReadValue(ctx.trampoline_rsp + 0x20, &tramp20);
    TryReadValue(ctx.rsp + 0x60, &rsp60);
    TryReadValue(ctx.trampoline_rsp + 0x60, &tramp60);

    int32_t r9_d00 = 0;
    int32_t r9_d04 = 0;
    int32_t r9_d08 = 0;
    int32_t r9_d0c = 0;
    int32_t r9_d10 = 0;
    int32_t r9_d14 = 0;
    int32_t r9_d18 = 0;
    int32_t r9_d1c = 0;
    int64_t r9_q20 = 0;
    const bool r9_ok = TryReadValue(ctx.r9 + 0x00, &r9_d00);
    TryReadValue(ctx.r9 + 0x04, &r9_d04);
    TryReadValue(ctx.r9 + 0x08, &r9_d08);
    TryReadValue(ctx.r9 + 0x0C, &r9_d0c);
    TryReadValue(ctx.r9 + 0x10, &r9_d10);
    TryReadValue(ctx.r9 + 0x14, &r9_d14);
    TryReadValue(ctx.r9 + 0x18, &r9_d18);
    TryReadValue(ctx.r9 + 0x1C, &r9_d1c);
    TryReadValue(ctx.r9 + 0x20, &r9_q20);

    int32_t r13_38c = 0;
    int32_t r13_390 = 0;
    int32_t r13_394 = 0;
    int32_t r13_398 = 0;
    int32_t r13_39c = 0;
    int32_t r13_3a0 = 0;
    const bool r13_ok = TryReadValue(ctx.r13 + 0x38C, &r13_38c);
    TryReadValue(ctx.r13 + 0x390, &r13_390);
    TryReadValue(ctx.r13 + 0x394, &r13_394);
    TryReadValue(ctx.r13 + 0x398, &r13_398);
    TryReadValue(ctx.r13 + 0x39C, &r13_39c);
    TryReadValue(ctx.r13 + 0x3A0, &r13_3a0);

    Log("hooks: affinity-petdiag-%s regs rcx=0x%p rdx=0x%p r8=0x%p r9=0x%p r13=0x%p r14=0x%p rsp=0x%p tramp_rsp=0x%p rsp20=0x%p tramp20=0x%p rsp60=0x%p tramp60=0x%p rip=0x%p",
        path_name,
        reinterpret_cast<void*>(ctx.rcx),
        reinterpret_cast<void*>(ctx.rdx),
        reinterpret_cast<void*>(ctx.r8),
        reinterpret_cast<void*>(ctx.r9),
        reinterpret_cast<void*>(ctx.r13),
        reinterpret_cast<void*>(ctx.r14),
        reinterpret_cast<void*>(ctx.rsp),
        reinterpret_cast<void*>(ctx.trampoline_rsp),
        reinterpret_cast<void*>(rsp20),
        reinterpret_cast<void*>(tramp20),
        reinterpret_cast<void*>(rsp60),
        reinterpret_cast<void*>(tramp60),
        reinterpret_cast<void*>(ctx.rip));

    Log("hooks: affinity-petdiag-%s data r9_ok=%d r9_d00=%d r9_d04=%d r9_d08=%d r9_d0c=%d r9_d10=%d r9_d14=%d r9_d18=%d r9_d1c=%d r9_q20=%lld r13_ok=%d r13_38c=%d r13_390=%d r13_394=%d r13_398=%d r13_39c=%d r13_3a0=%d",
        path_name,
        r9_ok ? 1 : 0,
        static_cast<int>(r9_d00),
        static_cast<int>(r9_d04),
        static_cast<int>(r9_d08),
        static_cast<int>(r9_d0c),
        static_cast<int>(r9_d10),
        static_cast<int>(r9_d14),
        static_cast<int>(r9_d18),
        static_cast<int>(r9_d1c),
        static_cast<long long>(r9_q20),
        r13_ok ? 1 : 0,
        static_cast<int>(r13_38c),
        static_cast<int>(r13_390),
        static_cast<int>(r13_394),
        static_cast<int>(r13_398),
        static_cast<int>(r13_39c),
        static_cast<int>(r13_3a0));
}

void LogFriendlyHit(const char* const path_name, const uintptr_t data, const int64_t delta) {
    if (!ShouldLogSample(g_affinity_hit_samples, 128)) {
        return;
    }

    std::uint64_t q08 = 0;
    std::uint64_t q10 = 0;
    std::uint64_t q18 = 0;
    std::uint64_t q28 = 0;
    std::uint32_t d08 = 0;
    std::uint32_t d0c = 0;
    std::uint32_t d10 = 0;
    std::uint32_t d14 = 0;
    std::uint32_t d18 = 0;
    std::uint32_t d1c = 0;
    std::uint8_t b08 = 0;
    TryReadValue(data + 0x08, &q08);
    TryReadValue(data + 0x10, &q10);
    TryReadValue(data + 0x18, &q18);
    TryReadValue(data + 0x28, &q28);
    TryReadValue(data + 0x08, &d08);
    TryReadValue(data + 0x0C, &d0c);
    TryReadValue(data + 0x10, &d10);
    TryReadValue(data + 0x14, &d14);
    TryReadValue(data + 0x18, &d18);
    TryReadValue(data + 0x1C, &d1c);
    TryReadValue(data + 0x08, &b08);

    Log("hooks: affinity-%s friendly hit data=0x%p delta_q20=%lld q08=%llu q10=%llu q18=%llu q28=%llu d08=%u d0c=%u d10=%u d14=%u d18=%u d1c=%u b08=%u runtime=%d player_ready=%d",
        path_name,
        reinterpret_cast<void*>(data),
        delta,
        static_cast<unsigned long long>(q08),
        static_cast<unsigned long long>(q10),
        static_cast<unsigned long long>(q18),
        static_cast<unsigned long long>(q28),
        static_cast<unsigned>(d08),
        static_cast<unsigned>(d0c),
        static_cast<unsigned>(d10),
        static_cast<unsigned>(d14),
        static_cast<unsigned>(d18),
        static_cast<unsigned>(d1c),
        static_cast<unsigned>(b08),
        g_runtime_enabled.load(std::memory_order_acquire) ? 1 : 0,
        IsPlayerRuntimeReady() ? 1 : 0);
}

bool TryPrepareScaledFriendlyDataCopy(const char* const path_name,
                                      const uintptr_t data,
                                      uintptr_t* const scaled_data) {
    if (scaled_data == nullptr) {
        return false;
    }

    if (data < kMinimumPointerAddress) {
        if (ShouldLogSample(g_affinity_probe_samples, 24)) {
            Log("hooks: affinity-%s skipped invalid friendly data=0x%p",
                path_name,
                reinterpret_cast<void*>(data));
        }
        return false;
    }

    int64_t original_delta = 0;
    if (!TryReadValue(data + kFriendlyVaryDeltaOffset, &original_delta)) {
        if (ShouldLogSample(g_affinity_hit_samples, 24)) {
            Log("hooks: affinity-%s failed to read friendly delta data=0x%p",
                path_name,
                reinterpret_cast<void*>(data));
        }
        return false;
    }
    LogFriendlyHit(path_name, data, original_delta);

    int64_t scaled_delta = original_delta;
    if (!TryScaleFriendlyVaryDelta(path_name, data, original_delta, &scaled_delta)) {
        return false;
    }

    thread_local std::array<FriendlyDataScratch, 8> scratch_records{};
    thread_local std::size_t scratch_index = 0;
    auto& scratch = scratch_records[scratch_index++ % scratch_records.size()];

    __try {
        std::memcpy(scratch.bytes.data(), reinterpret_cast<const void*>(data), scratch.bytes.size());
        *reinterpret_cast<int64_t*>(scratch.bytes.data() + kFriendlyVaryDeltaOffset) = scaled_delta;
        *scaled_data = reinterpret_cast<uintptr_t>(scratch.bytes.data());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (ShouldLogSample(g_affinity_probe_samples, 96)) {
        std::uint32_t d08 = 0;
        std::uint32_t d0c = 0;
        std::uint32_t d10 = 0;
        std::uint32_t d14 = 0;
        std::uint32_t d18 = 0;
        std::uint32_t d1c = 0;
        std::uint8_t b08 = 0;
        TryReadValue(data + 0x08, &d08);
        TryReadValue(data + 0x0C, &d0c);
        TryReadValue(data + 0x10, &d10);
        TryReadValue(data + 0x14, &d14);
        TryReadValue(data + 0x18, &d18);
        TryReadValue(data + 0x1C, &d1c);
        TryReadValue(data + 0x08, &b08);

        Log("hooks: affinity-%s scaled friendly copy data=0x%p copy=0x%p delta=%lld final=%lld d08=%u d0c=%u d10=%u d14=%u d18=%u d1c=%u b08=%u",
            path_name,
            reinterpret_cast<void*>(data),
            reinterpret_cast<void*>(*scaled_data),
            original_delta,
            scaled_delta,
            static_cast<unsigned>(d08),
            static_cast<unsigned>(d0c),
            static_cast<unsigned>(d10),
            static_cast<unsigned>(d14),
            static_cast<unsigned>(d18),
            static_cast<unsigned>(d1c),
            static_cast<unsigned>(b08));
    }

    return true;
}

void AffinityItemGiveEventProbeCallback(SafetyHookContext& ctx) {
    __try {
        LogAffinityItemEventProbe("give", ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_probe_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity item-give event probe hook", GetExceptionCode());
        }
    }
}

void AffinityItemTakeEventProbeCallback(SafetyHookContext& ctx) {
    __try {
        LogAffinityItemEventProbe("take", ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_probe_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity item-take event probe hook", GetExceptionCode());
        }
    }
}

void AffinityVaryScaleCallback(SafetyHookContext& ctx) {
    __try {
        uintptr_t scaled_data = 0;
        if (TryPrepareScaledFriendlyDataCopy("vary", ctx.r9, &scaled_data)) {
            ctx.r9 = scaled_data;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_probe_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity-vary scaler hook", GetExceptionCode());
        }
    }
}

void AffinityVaryWithLogoutScaleCallback(SafetyHookContext& ctx) {
    __try {
        uintptr_t scaled_data = 0;
        if (TryPrepareScaledFriendlyDataCopy("vary-logout-r9", ctx.r9, &scaled_data)) {
            ctx.r9 = scaled_data;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_probe_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity-vary-logout scaler hook", GetExceptionCode());
        }
    }
}

void AffinityPetDiagnosticRelocCallback(SafetyHookContext& ctx) {
    __try {
        LogAffinityPetDiagnostic("reloc", ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_probe_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity-petdiag-reloc hook", GetExceptionCode());
        }
    }
}

void AffinityPetDiagnosticRsrcCallback(SafetyHookContext& ctx) {
    __try {
        LogAffinityPetDiagnostic("rsrc", ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_probe_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity-petdiag-rsrc hook", GetExceptionCode());
        }
    }
}

void AffinityPrepareCallback(SafetyHookContext& ctx) {
    const uintptr_t record = ctx.rdi;
    if (record < kMinimumPointerAddress) {
        return;
    }

    __try {
        const uintptr_t value_address = record + 0x10;
        const int64_t old_value = static_cast<int64_t>(ctx.r12);
        int64_t new_value = *reinterpret_cast<const int64_t*>(value_address);
            if (!TryScaleAffinityGain(record, old_value, &new_value)) {
                return;
            }

            if (!TryStoreAffinityValue(value_address, new_value)) {
                return;
            }

            if (ShouldLogSample(g_affinity_samples, 24)) {
                Log("hooks: affinity-prepare scaled record=0x%p old=%lld final=%lld rip=0x%p",
                    reinterpret_cast<void*>(record),
                    old_value,
                    new_value,
                    reinterpret_cast<void*>(ctx.rip));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (!g_reported_affinity_exception.exchange(true, std::memory_order_acq_rel)) {
                Log("hooks: exception 0x%08lX inside affinity-prepare hook", GetExceptionCode());
            }
        }
}

void AffinityCurrentCallback(SafetyHookContext& ctx) {
    const uintptr_t record = ctx.rbx;
    if (record < kMinimumPointerAddress) {
        return;
    }

    __try {
        const int64_t old_value = *reinterpret_cast<const int64_t*>(record + 0x48);
        const int64_t pending_delta = *reinterpret_cast<const int64_t*>(record + 0x28);
        int64_t new_value = static_cast<int64_t>(ctx.rax);

        if (!TryScaleAffinityCurrentWrite(record, old_value, pending_delta, &new_value)) {
            return;
        }

        ctx.rax = static_cast<uintptr_t>(new_value);
        if (ShouldLogSample(g_affinity_current_samples, 24)) {
            Log("hooks: affinity-current scaled record=0x%p old=%lld delta=%lld final=%lld rip=0x%p",
                reinterpret_cast<void*>(record),
                old_value,
                pending_delta,
                new_value,
                reinterpret_cast<void*>(ctx.rip));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_current_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity-current hook", GetExceptionCode());
        }
    }
}

bool InstallAffinityMidHook(const uintptr_t target,
                            const char* const name,
                            SafetyHookMid* const storage,
                            void (*callback)(SafetyHookContext&)) {
    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), callback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create %s mid hook", name);
        return false;
    }

    *storage = std::move(*hook_result);
    Log("hooks: installed %s hook at 0x%p", name, reinterpret_cast<void*>(target));
    return true;
}

bool ResolveHardcodedTarget(const char* const name,
                            const uintptr_t rva,
                            const std::array<std::uint8_t, 5>& expected,
                            uintptr_t* const target) {
    if (target == nullptr) {
        return false;
    }

    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (base == 0) {
        Log("scanner: %s unavailable; base module missing", name);
        return false;
    }

    const uintptr_t candidate = base + rva;
    if (!ExpectBytes(candidate, expected)) {
        Log("scanner: %s callsite RVA mismatch - expected bytes not found rva=0x%08llX",
            name,
            static_cast<unsigned long long>(rva));
        return false;
    }

    Log("scanner: %s callsite found at hardcoded RVA 0x%08llX (v1.05.01)",
        name,
        static_cast<unsigned long long>(rva));
    *target = candidate;
    return true;
}

void InstallAffinityItemEventProbeHooks() {
    constexpr uintptr_t kGiveEventRva = 0x01377454;
    constexpr uintptr_t kTakeEventRva = 0x0137754B;
    constexpr std::array<std::uint8_t, 5> kGiveBytes = {0xE8, 0x17, 0xDD, 0x95, 0x00};
    constexpr std::array<std::uint8_t, 5> kTakeBytes = {0xE8, 0x20, 0xDC, 0x95, 0x00};

    uintptr_t give_target = 0;
    uintptr_t take_target = 0;
    const bool give_found = ResolveHardcodedTarget("affinity-item-give-event",
                                                   kGiveEventRva,
                                                   kGiveBytes,
                                                   &give_target);
    const bool take_found = ResolveHardcodedTarget("affinity-item-take-event",
                                                   kTakeEventRva,
                                                   kTakeBytes,
                                                   &take_target);

    if (give_found) {
        InstallAffinityMidHook(give_target,
                               "affinity-item-give-event-probe",
                               &g_affinity_item_give_probe_hook,
                               AffinityItemGiveEventProbeCallback);
    }

    if (take_found) {
        InstallAffinityMidHook(take_target,
                               "affinity-item-take-event-probe",
                               &g_affinity_item_take_probe_hook,
                               AffinityItemTakeEventProbeCallback);
    }
}

void InstallAffinityPetDiagnosticHooks() {
    const auto config = GetConfig();
    if (!config.affinity.pet_diagnostics) {
        return;
    }

    const uintptr_t reloc_target = ScanForAffinityPetDiagnosticReloc();
    if (reloc_target != 0) {
        if (!InstallAffinityMidHook(reloc_target,
                                    "affinity-petdiag-reloc",
                                    &g_affinity_pet_diag_reloc_hook,
                                    AffinityPetDiagnosticRelocCallback)) {
            Log("hooks: affinity-petdiag-reloc install failed; continuing without reloc pet diagnostics");
        }
    }

    const uintptr_t rsrc_target = ScanForAffinityPetDiagnosticRsrc();
    if (rsrc_target != 0) {
        if (!InstallAffinityMidHook(rsrc_target,
                                    "affinity-petdiag-rsrc",
                                    &g_affinity_pet_diag_rsrc_hook,
                                    AffinityPetDiagnosticRsrcCallback)) {
            Log("hooks: affinity-petdiag-rsrc install failed; continuing without rsrc pet diagnostics");
        }
    }
}

bool InstallAffinityFriendlyScalerHooks() {
    const uintptr_t vary_target = ScanForAffinityVaryFriendly();
    const uintptr_t vary_logout_target = ScanForAffinityVaryFriendlyWithLogout();

    if (vary_target == 0 || vary_logout_target == 0) {
        InstallAffinityPetDiagnosticHooks();
        if (g_affinity_pet_diag_reloc_hook || g_affinity_pet_diag_rsrc_hook) {
            Log("hooks: affinity friendly hook pair unavailable; continuing with pet diagnostics only");
            return true;
        }
        Log("hooks: affinity friendly hook pair unavailable; continuing without affinity scaling");
        return false;
    }

    if (!InstallAffinityMidHook(vary_target, "affinity-vary-friendly", &g_affinity_vary_hook, AffinityVaryScaleCallback)) {
        RemoveAffinityHooks();
        return false;
    }

    if (!InstallAffinityMidHook(vary_logout_target,
                                "affinity-vary-logout-friendly",
                                &g_affinity_vary_logout_hook,
                                AffinityVaryWithLogoutScaleCallback)) {
        RemoveAffinityHooks();
        return false;
    }

    InstallAffinityPetDiagnosticHooks();
    InstallAffinityItemEventProbeHooks();

    Log("hooks: affinity friendly scaler hooks installed with copy-on-write data");
    return true;
}

}  // namespace

bool InstallAffinityHooks() {
    static constexpr std::array<std::uint8_t, 8> kPrepareBytes = {
        0x48, 0x8B, 0x47, 0x10, 0x48, 0x89, 0x45, 0x18,
    };
    static constexpr std::array<std::uint8_t, 20> kCurrentBytes = {
        0x48, 0x89, 0x43, 0x48, 0x41, 0x8B, 0xCD, 0x83, 0xE9, 0x01,
        0x74, 0x0A, 0x83, 0xF9, 0x01, 0x75, 0x09, 0x88, 0x4B, 0x3F,
    };

    RemoveAffinityHooks();

    // The old affinity implementation is a two-site patch.  Installing only
    // one side is unsafe on Crimson Desert 1.05.01 and caused the post-fix-3
    // load crashes.  Validate both exact legacy sites first, then install both
    // or neither.  Do not accept the 1.05.01 generic container block as an
    // affinity-current hook.
    const uintptr_t prepare_target = ScanForAffinityGainPrepare();
    const uintptr_t current_target = ScanForAffinityCurrentWrite();

    if (prepare_target == 0 || current_target == 0) {
        Log("hooks: affinity hook pair unavailable; continuing without affinity scaling");
        return InstallAffinityFriendlyScalerHooks();
    }

    if (!ExpectBytes(prepare_target, kPrepareBytes)) {
        Log("hooks: affinity-prepare target did not match expected bytes target=0x%p",
            reinterpret_cast<void*>(prepare_target));
        Log("hooks: affinity hook pair rejected; continuing without affinity scaling");
        return InstallAffinityFriendlyScalerHooks();
    }

    if (!ExpectBytes(current_target, kCurrentBytes)) {
        Log("hooks: affinity-current target did not match expected bytes target=0x%p",
            reinterpret_cast<void*>(current_target));
        Log("hooks: affinity hook pair rejected; continuing without affinity scaling");
        return InstallAffinityFriendlyScalerHooks();
    }

    if (!InstallAffinityMidHook(prepare_target, "affinity-prepare", &g_affinity_hook, AffinityPrepareCallback)) {
        RemoveAffinityHooks();
        return false;
    }

    if (!InstallAffinityMidHook(current_target, "affinity-current", &g_affinity_current_hook, AffinityCurrentCallback)) {
        RemoveAffinityHooks();
        return false;
    }

    InstallAffinityPetDiagnosticHooks();
    InstallAffinityItemEventProbeHooks();

    return true;
}

void RemoveAffinityHooks() {
    if (g_affinity_hook) {
        g_affinity_hook.reset();
        Log("hooks: removed affinity-prepare hook");
    }

    if (g_affinity_current_hook) {
        g_affinity_current_hook.reset();
        Log("hooks: removed affinity-current hook");
    }

    if (g_affinity_vary_hook) {
        g_affinity_vary_hook.reset();
        Log("hooks: removed affinity-vary-friendly hook");
    }

    if (g_affinity_vary_logout_hook) {
        g_affinity_vary_logout_hook.reset();
        Log("hooks: removed affinity-vary-logout-friendly hook");
    }

    if (g_affinity_pet_diag_reloc_hook) {
        g_affinity_pet_diag_reloc_hook.reset();
        Log("hooks: removed affinity-petdiag-reloc hook");
    }

    if (g_affinity_pet_diag_rsrc_hook) {
        g_affinity_pet_diag_rsrc_hook.reset();
        Log("hooks: removed affinity-petdiag-rsrc hook");
    }

    if (g_affinity_item_give_probe_hook) {
        g_affinity_item_give_probe_hook.reset();
        Log("hooks: removed affinity-item-give-event-probe hook");
    }

    if (g_affinity_item_take_probe_hook) {
        g_affinity_item_take_probe_hook.reset();
        Log("hooks: removed affinity-item-take-event-probe hook");
    }
}
