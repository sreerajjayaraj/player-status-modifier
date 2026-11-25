#pragma once

#include "item_capture_compat.h"
#include "native_menu_reader_profiles.h"
#include "native_menu_reader_516_profiles.h"

namespace psm::menu_reader {
namespace detail {
inline constexpr std::size_t kReaderRva = 0x18EDD20;
struct CurveLayout {
    std::size_t broker_record;
    std::size_t original_slot;
    std::size_t active_record;
    std::size_t active_slot;
    std::size_t callback;
    std::size_t observer;
};

inline constexpr CurveLayout kMenu514Curve{
    0x512010,0x5132A8,0x513430,0x513440,profile::kReaderCallbackRva,profile::kReaderObserverRva,
};
inline constexpr CurveLayout kMenu516Curve{
    0x5337E0,0x534A78,0x534C00,0x534C10,profile516::kReaderCallbackRva,profile516::kReaderObserverRva,
};

inline bool ExactFunction(const v3446::Image& image, std::size_t rva, const v3446::Profile& function) {
    return image.Exec(rva, function.size) && image.Fragments(rva, function) &&
        !std::memcmp(image.data + rva, function.code, function.size);
}

inline bool OwnsCurveReader(const v3446::Image& provider, uintptr_t entry,
                           const CurveLayout& layout, const capture_compat::Reader& reader) {
    using capture_compat::Context;
    if (!Context::DataSection(provider, layout.broker_record, 0x70, true) ||
        !Context::DataSection(provider, layout.original_slot, sizeof(uintptr_t), true) ||
        !Context::DataSection(provider, layout.active_record, sizeof(uintptr_t), true) ||
        !Context::DataSection(provider, layout.active_slot, sizeof(uintptr_t), true)) return false;

    const auto base = reinterpret_cast<uintptr_t>(provider.data);
    uintptr_t activeRecord = 0, activeSlot = 0;
    if (!reader.Read(base + layout.active_record, &activeRecord, sizeof(activeRecord)) ||
        activeRecord != base + layout.broker_record ||
        !reader.Read(base + layout.active_slot, &activeSlot, sizeof(activeSlot)) ||
        activeSlot != base + layout.original_slot) return false;

    // The owned broker publishes mode 1 only after enabling its MinHook.
    // Mode 2 is a subscriber, whose separate original and lifetime are unknown.
    std::array<unsigned char, 0x54> observed{}, expected{};
    const auto store = [&](std::size_t offset, auto value) {
        std::memcpy(expected.data() + offset, &value, sizeof(value));
    };
    store(0, uint64_t{0x314B524243524C44});
    store(8, uint32_t{1});
    store(0x0C, uint32_t{0x70});
    store(0x10, uint64_t{0x3432343345565255});
    store(0x18, entry);
    store(0x20, base + layout.callback);
    store(0x28, base);
    store(0x30, base + layout.observer);
    std::memcpy(expected.data() + 0x38, v3446::kReader.code, 21);
    store(0x50, uint32_t{1});
    return reader.Read(activeRecord, observed.data(), observed.size()) && observed == expected;
}

inline const CurveLayout* ResolveLayout(const v3446::Image& provider) {
    if (provider.nt.OptionalHeader.CheckSum != 0) return nullptr;
    if (provider.nt.FileHeader.TimeDateStamp == 0x7CCDF8C0 && provider.size == 0x990000) {
        using namespace profile;
        if (ExactFunction(provider,kReaderInstallerRva,kReaderInstaller) &&
            ExactFunction(provider,kCurveBrokerRva,kCurveBroker) &&
            ExactFunction(provider,kReaderCallbackRva,kReaderCallback) &&
            ExactFunction(provider,kReaderObserverRva,kReaderObserver)) return &kMenu514Curve;
    } else if (provider.nt.FileHeader.TimeDateStamp == 0x847F423D && provider.size == 0xA1E000) {
        using namespace profile516;
        if (ExactFunction(provider,kReaderInstallerRva,kReaderInstaller) &&
            ExactFunction(provider,kCurveBrokerRva,kCurveBroker) &&
            ExactFunction(provider,kReaderCallbackRva,kReaderCallback) &&
            ExactFunction(provider,kReaderObserverRva,kReaderObserver) &&
            capture_compat::Context::DataExport(provider,"DesertLinkResourceCurveBrokerV1") ==
                kMenu516Curve.broker_record) return &kMenu516Curve;
    }
    return nullptr;
}

inline std::size_t FindOwned(const v3446::Image& game, const void* module,
                             const capture_compat::Reader& reader) {
    if (!module || !game.Exec(kReaderRva, v3446::kReader.size)) return 0;
    v3446::Image provider{};
    if (!provider.Init(module,v3446::ModuleSize(module))) return 0;
    const auto* layout = ResolveLayout(provider);
    if (!layout) return 0;

    const auto base = reinterpret_cast<uintptr_t>(module);
    const auto entry = reinterpret_cast<uintptr_t>(game.data) + kReaderRva;
    const capture_compat::Capture capture{base + layout->callback, base + layout->original_slot, 5};
    capture_compat::Snapshot first{}, second{};
    if (!OwnsCurveReader(provider, entry, *layout, reader) ||
        !capture_compat::Observe(game, v3446::kReader, kReaderRva, capture, first, reader) ||
        !OwnsCurveReader(provider, entry, *layout, reader) ||
        !capture_compat::Observe(game, v3446::kReader, kReaderRva, capture, second, reader) ||
        !(first == second)) return 0;
    return kReaderRva;
}

inline std::size_t FindLiveOwned(const v3446::Image& game, HMODULE module) {
    __try { return FindOwned(game, module, {}); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
}

#ifdef PSM_SCANNER_TESTING
inline const void* g_testProvider = nullptr;
inline capture_compat::Reader g_testReader{};
inline void SetTestProvider(const void* provider, const capture_compat::Reader& reader) {
    g_testProvider = provider;
    g_testReader = reader;
}
#endif

inline std::size_t Find(const v3446::Image& game) {
    if (const auto native = game.Find(v3446::kReader)) return native;
#ifdef PSM_SCANNER_TESTING
    return detail::FindOwned(game, g_testProvider, g_testReader);
#else
    HMODULE provider = nullptr;
    if (!GetModuleHandleExW(0, L"DesertLinkModMenu.asi", &provider)) return 0;
    const auto native = detail::FindLiveOwned(game, provider);
    FreeLibrary(provider);
    return native;
#endif
}
}

