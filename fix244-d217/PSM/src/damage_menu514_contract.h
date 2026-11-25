#pragma once

#include "item_capture_compat.h"
#include "oxygen_3732_profiles.h"
#include <atomic>

namespace psm::damage_menu514 {

inline constexpr size_t kNativeEntry = 0x194D0F0;
inline constexpr size_t kNativeSite = 0x194D122;
inline constexpr size_t kMenuReturn = 0xDE7A0;
inline constexpr unsigned char kNativePatch[] = {
    0x48,0x8B,0x11,0x41,0xB8,0xEB,0x01,0x00,0x00,
};
inline constexpr size_t kPatchSize = sizeof(kNativePatch);
inline constexpr unsigned char kNativeStatDispatch[] = {
    0x48,0x8B,0x0D,0x26,0x84,0x8F,0x05,0x41,0x80,0x3C,0x10,0x00,0x48,0x0F,0x45,0x0D,
    0x21,0x84,0x8F,0x05,0x48,0x8B,0x09,0x48,0x8B,0x51,0x08,0x48,0x8B,0x4A,0x10,
    0x66,0x3B,0x59,0x78,0x75,0x66,
};

// Discover the common post-TLS instructions in executable code, then prove the
// dispatcher owner and argument moves around the candidate.
inline constexpr unsigned char kNativeWindow3732[] = {
    0x48,0x8B,0x11,0x41,0xB8,0xEB,0x01,0x00,0x00,
    0x48,0x8B,0x0D,0x26,0xC7,0x8F,0x05,0x41,0x80,0x3C,0x10,0x00,0x48,0x0F,0x45,0x0D,
    0x21,0xC7,0x8F,0x05,0x48,0x8B,0x09,0x48,0x8B,0x51,0x08,0x48,0x8B,0x4A,0x10,
    0x66,0x3B,0x59,0x78,0x75,0x66,
};
inline constexpr v3446::Profile kNativeMid3732{
    kNativeWindow3732,sizeof(kNativeWindow3732),nullptr,0
};

inline constexpr unsigned char kNativePrologue[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x70,0x49,0x8B,0xC1,0x49,0x8B,0xE8,0x0F,0xB7,0xDA,0x48,0x8B,0xF1,0x4D,0x85,0xC9,0x0F,0x84,0x19,0x01,0x00,0x00
};
inline constexpr unsigned char kNativeUnwind[] = {
    0x11,0x14,0x08,0x00,0x14,0x64,0x12,0x00,0x14,0x54,0x11,0x00,0x14,0x34,0x10,0x00,0x14,0xD2,0x10,0x70,0xF8,0x74,0xC2,0x04
};
inline constexpr unsigned char kMenuPrologue[] = {
    0x40,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x90,0x0B,0x00,0x00,
};
inline constexpr unsigned char kMenuUnwind[] = {
    0x19,0x25,0x09,0x00,0x13,0x01,0x72,0x01,0x0C,0xF0,0x0A,0xE0,0x08,0xD0,0x06,0xC0,
    0x04,0x70,0x03,0x60,0x02,0x30,0x00,0x00,
};
inline constexpr unsigned char kMenuForward[] = {
    0x48,0x8B,0x8C,0x24,0xF0,0x0B,0x00,0x00,0x48,0x89,0x4C,0x24,0x20,0x4D,0x8B,0xCC,
    0x4C,0x8B,0x84,0x24,0xB8,0x00,0x00,0x00,0x41,0x0F,0xB7,0xD6,0x48,0x8B,0xCE,
    0x48,0x8B,0x05,0x2E,0x4B,0x43,0x00,0xFF,0x15,0x50,0x61,0x1E,0x00,
};
inline constexpr unsigned char kMenuDamageEnabledRead[] = {0x0F,0xB6,0x05,0x1A,0xB7,0x42,0x00};
inline constexpr unsigned char kMenuMultiplierRead[] = {0x4C,0x8B,0x05,0x95,0x7A,0x3D,0x00};
inline constexpr size_t kMenuHandler = 0xDDF20;
inline constexpr size_t kMenuOriginalSlot = 0x5132C8;
inline constexpr size_t kMenuTargetSlot = 0x5132D0;
inline constexpr size_t kMenuDamageEnabled = 0x5135FA;
inline constexpr size_t kMenuMultiplier = 0x4BF9A0;
inline constexpr unsigned char kMenu516Forward[] = {
    0x48,0x8B,0x8C,0x24,0xF0,0x0B,0x00,0x00,0x48,0x89,0x4C,0x24,0x20,0x4D,0x8B,0xCC,
    0x4C,0x8B,0x84,0x24,0xB8,0x00,0x00,0x00,0x41,0x0F,0xB7,0xD6,0x48,0x8B,0xCE,
    0x48,0x8B,0x05,0xCE,0xD6,0x44,0x00,0xFF,0x15,0x20,0xC5,0x1E,0x00,
};
inline constexpr unsigned char kMenu516EnabledRead[] = {0x0F,0xB6,0x05,0x5A,0x42,0x44,0x00};
inline constexpr unsigned char kMenu516MultiplierRead[] = {0x4C,0x8B,0x05,0xA5,0x01,0x3E,0x00};

inline constexpr unsigned char kMenu60Forward[] = {
    0x48,0x8B,0x8C,0x24,0xF0,0x0B,0x00,0x00,0x48,0x89,0x4C,0x24,0x20,0x4D,0x8B,0xCF,
    0x4C,0x8B,0x84,0x24,0xB8,0x00,0x00,0x00,0x41,0x0F,0xB7,0xD5,0x48,0x8B,0xCE,
    0x48,0x8B,0x05,0xF8,0xAE,0x5A,0x00,0xFF,0x15,0x52,0x34,0x2D,0x00,
};
inline constexpr unsigned char kMenu60EnabledRead[] = {0x0F,0xB6,0x05,0x7D,0xFF,0x59,0x00};
inline constexpr unsigned char kMenu60PercentageRead[] = {0x8B,0x05,0x7A,0x5A,0x53,0x00};

inline constexpr unsigned char kMenu605Forward[] = {
    0x48,0x8B,0x8C,0x24,0xF0,0x0B,0x00,0x00,0x48,0x89,0x4C,0x24,0x20,0x4D,0x8B,0xCF,
    0x4C,0x8B,0x84,0x24,0xB8,0x00,0x00,0x00,0x0F,0xB7,0xD6,0x49,0x8B,0xCE,
    0x48,0x8B,0x05,0xFD,0x8E,0x69,0x00,0xFF,0x15,0x3F,0xB3,0x2E,0x00,
};
inline constexpr unsigned char kMenu605EnabledRead[] = {0x0F,0xB6,0x05,0x7D,0xE0,0x68,0x00};
inline constexpr unsigned char kMenu605PercentageRead[] = {0x8B,0x05,0x2A,0x3A,0x62,0x00};

struct MenuContract {
    RUNTIME_FUNCTION handler;
    size_t return_rva;
    size_t original_slot;
    size_t target_slot;
    size_t enabled_slot;
    size_t multiplier_slot;
    bool percentage = false;
};
inline constexpr MenuContract kMenu514{
    {kMenuHandler,0xDEAF8,0x489698},kMenuReturn,kMenuOriginalSlot,kMenuTargetSlot,
    kMenuDamageEnabled,kMenuMultiplier,
};
// Same native frame, with the 5.1.6 handler, state and option locations.
inline constexpr MenuContract kMenu516{
    {0xE6B50,0xE7728,0x49A39C},0xE73D0,0x534A98,0x534AA0,0x534DCA,0x4D0D40,
};

inline constexpr MenuContract kMenu60{
    {0xF4C40,0xF5877,0x5F09E0},0xF5516,0x6A0408,0x6A0410,0x6A0774,0x6362C0,true,
};

inline constexpr MenuContract kMenu605{
    {0xF7DE0,0xF89B7,0x6DFFC8},0xF8661,0x791558,0x791560,0x7918C4,0x7272C0,true,
};

struct Binding {
    uintptr_t code_begin = 0;
    uintptr_t code_end = 0;
    uintptr_t menu = 0;
    const MenuContract* contract = &kMenu514;
    std::size_t native_site = 0;
    bool operator==(const Binding&) const = default;
};

inline std::atomic<const Binding*> g_verified_menu{nullptr};

struct Frame {
    uintptr_t caller = 0;
    uintptr_t source = 0;
    bool menu = false;
};

inline bool UsesCurrentDispatcher(const v3446::Image& image) {
    // The typed-slot observer is installed before the damage hook. Match only
    // its untouched prefix and prove the slot function's complete owner.
    constexpr v3446::Profile slot_prefix{
        oxygen::layout3732::kSlotCode,0x7B,nullptr,0
    };
    const auto slot = image.Find(slot_prefix,false);
    RUNTIME_FUNCTION owner{};
    return slot && image.UniqueRecord(slot,owner) && owner.BeginAddress == slot &&
           owner.EndAddress == slot + sizeof(oxygen::layout3732::kSlotCode);
}
inline bool IsCurrentGame(const v3446::Image& image) {
    return v3446::HasCurrentResourceLayout(image) || UsesCurrentDispatcher(image);
}

template<size_t N>
inline bool CodeEquals(const v3446::Image& image, size_t at, const unsigned char (&bytes)[N]) {
    return image.Exec(at,N) && !std::memcmp(image.data + at,bytes,N);
}

template<size_t N>
inline bool OwnerEquals(const v3446::Image& image, size_t at, const RUNTIME_FUNCTION& expected,
                        const unsigned char (&unwind)[N]) {
    RUNTIME_FUNCTION actual{};
    return image.UniqueRecord(at,actual) && actual.BeginAddress == expected.BeginAddress &&
           actual.EndAddress == expected.EndAddress && actual.UnwindData == expected.UnwindData &&
           image.Range(actual.UnwindData,N) && !std::memcmp(image.data + actual.UnwindData,unwind,N);
}

inline const MenuContract* ResolveMenu(const v3446::Image& image) {
    if (image.nt.OptionalHeader.CheckSum) return nullptr;

    const MenuContract* profile = nullptr;
    if (image.nt.FileHeader.TimeDateStamp == 0x7CCDF8C0 && image.size == 0x990000 &&
        CodeEquals(image,kMenuReturn - sizeof(kMenuForward),kMenuForward) &&
        CodeEquals(image,0xE7ED9,kMenuDamageEnabledRead) && CodeEquals(image,0xE7F04,kMenuMultiplierRead)) {
        profile = &kMenu514;
    } else if (image.nt.FileHeader.TimeDateStamp == 0x847F423D && image.size == 0xA1E000 &&
        CodeEquals(image,kMenu516.return_rva - sizeof(kMenu516Forward),kMenu516Forward) &&
        CodeEquals(image,0xF0B69,kMenu516EnabledRead) && CodeEquals(image,0xF0B94,kMenu516MultiplierRead)) {
        profile = &kMenu516;
    } else if (image.nt.FileHeader.TimeDateStamp == 0x1927CF17 && image.size == 0xC01000 &&
        CodeEquals(image,kMenu60.return_rva - sizeof(kMenu60Forward),kMenu60Forward) &&
        CodeEquals(image,0x1007F0,kMenu60EnabledRead) &&
        CodeEquals(image,0x100840,kMenu60PercentageRead)) {
        profile = &kMenu60;
    } else if (CodeEquals(image,kMenu605.return_rva - sizeof(kMenu605Forward),kMenu605Forward) &&
        CodeEquals(image,0x103840,kMenu605EnabledRead) &&
        CodeEquals(image,0x103890,kMenu605PercentageRead)) {
        profile = &kMenu605;
    }
    if (!profile ||
        !OwnerEquals(image,profile->return_rva,profile->handler,kMenuUnwind) ||
        !CodeEquals(image,profile->handler.BeginAddress,kMenuPrologue) ||
        !capture_compat::Context::DataSection(image,profile->original_slot,8,true) ||
        !capture_compat::Context::DataSection(image,profile->target_slot,8,true) ||
        !capture_compat::Context::DataSection(image,profile->enabled_slot,1,true) ||
        !capture_compat::Context::DataSection(image,profile->multiplier_slot,profile->percentage ? 4 : 8,true)) return nullptr;
    return profile;
}

inline bool EntryOwnedByMenu(const v3446::Image& game, const v3446::Image& menu, const MenuContract& profile,
                             const capture_compat::Reader& reader = {},
                             std::size_t native_entry = kNativeEntry) {
    const auto entry = reinterpret_cast<uintptr_t>(game.data) + native_entry;
    const auto handler = reinterpret_cast<uintptr_t>(menu.data) + profile.handler.BeginAddress;
    // MinHook's 5-byte displaced entry, jump-back and handler relay. Take
    // two observations so incomplete publication cannot authorize the hook.
    uintptr_t first_bridge = 0;
    for (unsigned sample = 0; sample < 2; ++sample) {
        uintptr_t target = 0, bridge = 0;
        std::array<unsigned char,5> branch{};
        std::array<unsigned char,33> code{};
        int32_t delta = 0;
        if (!menu.Read(profile.target_slot,target) || target != entry ||
            !menu.Read(profile.original_slot,bridge) || !reader.Exec(bridge,code.size()) ||
            !reader.Read(bridge,code.data(),code.size()) || !reader.Read(entry,branch.data(),branch.size()) ||
            branch[0] != 0xE9 || std::memcmp(code.data(),kNativePrologue,5) ||
            !capture_compat::AbsoluteJump(code.data()+5,entry+5) ||
            !capture_compat::AbsoluteJump(code.data()+19,handler)) return false;
        std::memcpy(&delta,branch.data()+1,sizeof(delta));
        if (static_cast<int64_t>(entry+5)+delta != static_cast<int64_t>(bridge+19) ||
            (sample && bridge != first_bridge)) return false;
        first_bridge = bridge;
    }
    return true;
}

inline Binding Resolve(const v3446::Image& game, const v3446::Image* menu,
                       const capture_compat::Reader& reader = {}) {
    // The first witness is also used by Menu and Tailwind. Keep our write
    // between Tailwind's TLS instruction and the Menu body witness.
    const auto* profile = menu ? ResolveMenu(*menu) : nullptr;
    const bool legacy = v3446::HasCurrentResourceLayout(game);
    if (!legacy && !UsesCurrentDispatcher(game)) return {};
    const auto site = legacy ? kNativeSite : game.Find(kNativeMid3732,false);
    if (site < 0x32) return {};
    const auto entry = site - 0x32;
    if (legacy) {
        if (!OwnerEquals(game,site,{kNativeEntry,0x194D248,0x67BBAA0},kNativeUnwind) ||
            !CodeEquals(game,site+kPatchSize,kNativeStatDispatch)) return {};
    } else {
        RUNTIME_FUNCTION owner{};
        std::uint32_t handler = 0;
        if (!game.UniqueRecord(site,owner) || owner.BeginAddress != entry ||
            owner.EndAddress != entry + 0x158 || !game.Range(owner.UnwindData,sizeof(kNativeUnwind)) ||
            std::memcmp(game.data + owner.UnwindData,kNativeUnwind,20) ||
            !game.Read(owner.UnwindData + 20,handler) || !game.Exec(handler)) return {};
    }
    if (!game.Exec(entry,sizeof(kNativePrologue)) ||
        std::memcmp(game.data + entry+5,kNativePrologue+5,sizeof(kNativePrologue)-5) ||
        !CodeEquals(game,site,kNativePatch)) return {};
    if (std::memcmp(game.data+entry,kNativePrologue,5) &&
        (!profile || !EntryOwnedByMenu(game,*menu,*profile,reader,entry))) return {};
    const auto provider = profile ? reinterpret_cast<uintptr_t>(menu->data) : 0;

    for (unsigned i = 0; i < game.nt.FileHeader.NumberOfSections; ++i) {
        const auto& section = game.sections[i];
        const auto length = (std::max)(section.Misc.VirtualSize,section.SizeOfRawData);
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && site >= section.VirtualAddress &&
            site - section.VirtualAddress < length && game.Range(section.VirtualAddress,length)) {
            const auto start = reinterpret_cast<uintptr_t>(game.data) + section.VirtualAddress;
            return {start,start+length,provider,profile,site};
        }
    }
    return {};
}

inline bool DecodeFrame(const Binding& binding, uintptr_t rsp, Frame& frame) {
    frame = {};
    if (!rsp || rsp > UINTPTR_MAX - 0xC50 ||
        !capture_compat::SafeCopy(rsp+0x78,&frame.caller,sizeof(frame.caller)) ||
        !capture_compat::SafeCopy(rsp+0xA0,&frame.source,sizeof(frame.source))) return false;
    frame.menu = binding.menu && binding.contract && frame.caller == binding.menu+binding.contract->return_rva;
    if (frame.menu && !capture_compat::SafeCopy(rsp+0xC48,&frame.caller,sizeof(frame.caller))) return false;
    return frame.caller >= binding.code_begin && frame.caller < binding.code_end;
}

inline bool PreferMenuOutgoing(const Binding* binding) {
    if (!binding || !binding->menu || !binding->contract) return false;
    const auto menu = binding->menu;
    const auto& profile = *binding->contract;
    unsigned char enabled = 0, again = 0;
    if (!capture_compat::SafeCopy(menu+profile.enabled_slot,&enabled,sizeof(enabled)) || enabled > 1)
        return true;
    if (!enabled) return false;
    bool effective = false;
    if (profile.percentage) {
        int32_t percent = 0;
        if (!capture_compat::SafeCopy(menu+profile.multiplier_slot,&percent,sizeof(percent)) ||
            percent < 10 || percent > 10000) return true;
        effective = percent != 100;
    } else {
        int64_t multiplier = 0;
        if (!capture_compat::SafeCopy(menu+profile.multiplier_slot,&multiplier,sizeof(multiplier))) return true;
        effective = multiplier > 1;
    }
    return !capture_compat::SafeCopy(menu+profile.enabled_slot,&again,sizeof(again)) ||
           enabled != again || effective;
}

// A configured stat-write fallback owns its multiplier when the typed writer
// is installed. The dispatcher covers that channel when the fallback is off.
inline constexpr bool NativeDamageChannel(bool writer, bool stat_write_fallback) {
    return !writer || !stat_write_fallback;
}

} // namespace psm::damage_menu514
