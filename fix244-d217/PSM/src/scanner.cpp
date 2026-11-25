#include "runtime/game_image.h"
#include <atomic>
#include "scanner.h"
#include "item_3446.h"
#include "native_3446.h"
#include "native_menu_reader.h"

#include "logger.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#ifdef PSM_SCANNER_TESTING
static HMODULE g_scanner_test_module = nullptr;
void SetScannerTestModule(void* module) { g_scanner_test_module = static_cast<HMODULE>(module); }
#endif

namespace {



HMODULE GetScannerGameModule() {
#ifdef PSM_SCANNER_TESTING
    return g_scanner_test_module;
#else
    return psm::game::MainModule();
#endif
}

struct PatternDefinition {
    const char* name = nullptr;
    const char* text = nullptr;
    std::size_t hook_offset = 0;
};

struct SectionSpan {
    const std::uint8_t* begin = nullptr;
    std::size_t size = 0;
    std::string section_name;
    bool executable = false;
};

struct PatternByte {
    std::uint8_t value = 0;
    bool wildcard = false;
};

struct MatchResult {
    uintptr_t address = 0;
    std::string section_name;
};

enum class ScanStatus {
    NotFound,
    Unique,
    Ambiguous,
};

struct ScanOutcome {
    uintptr_t address = 0;
    ScanStatus status = ScanStatus::NotFound;
};

constexpr uintptr_t kMinimumPointerAddress = 0x10000;

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

bool IsSharedResourceDeltaFunction(const uintptr_t address) {
    if (address < kMinimumPointerAddress) {
        return false;
    }

    static constexpr std::array<std::uint8_t, 4> kSaveNonvolatile = {
        0x48, 0x89, 0x5C, 0x24,
    };
    static constexpr std::array<std::uint8_t, 6> kFrameSetup = {
        0x55, 0x56, 0x57, 0x48, 0x83, 0xEC,
    };
    static constexpr std::array<std::uint8_t, 5> kCaptureDelta = {
        0x31, 0xC0, 0x4C, 0x89, 0xC3,
    };
    static constexpr std::array<std::uint8_t, 6> kCaptureArguments = {
        0x48, 0x89, 0xD5, 0x48, 0x89, 0xCE,
    };

    __try {
        return std::memcmp(reinterpret_cast<const void*>(address),
                           kSaveNonvolatile.data(),
                           kSaveNonvolatile.size()) == 0 &&
               std::memcmp(reinterpret_cast<const void*>(address + 5),
                           kFrameSetup.data(),
                           kFrameSetup.size()) == 0 &&
               std::memcmp(reinterpret_cast<const void*>(address + 12),
                           kCaptureDelta.data(),
                           kCaptureDelta.size()) == 0 &&
               std::memcmp(reinterpret_cast<const void*>(address + 22),
                           kCaptureArguments.data(),
                           kCaptureArguments.size()) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t ResolveRelativeBranch(const uintptr_t instruction, const std::uint8_t opcode) {
    if (instruction < kMinimumPointerAddress) {
        return 0;
    }

    __try {
        if (*reinterpret_cast<const std::uint8_t*>(instruction) != opcode) {
            return 0;
        }
        std::int32_t displacement = 0;
        std::memcpy(&displacement,
                    reinterpret_cast<const void*>(instruction + 1),
                    sizeof(displacement));
        return ResolveRelativeBranchTarget(instruction, displacement);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

uintptr_t ResolveSharedResourceDeltaFromCall(const uintptr_t callsite) {
    uintptr_t target = ResolveRelativeBranch(callsite, 0xE8);
    if (target == 0) {
        return 0;
    }

    for (int hop = 0; hop < 3 && !IsSharedResourceDeltaFunction(target); ++hop) {
        const uintptr_t next = ResolveRelativeBranch(target, 0xE9);
        if (next == 0 || next == target) {
            return 0;
        }
        target = next;
    }
    return IsSharedResourceDeltaFunction(target) ? target : 0;
}

template <std::size_t N>
uintptr_t ResolveExpectedRvaCandidates(const char* const name,
                                       const uintptr_t* const rvas,
                                       const std::size_t rva_count,
                                       const std::array<std::uint8_t, N>& expected) {
    if (rvas == nullptr || rva_count == 0) {
        return 0;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(GetScannerGameModule());
    if (base == 0) {
        Log("scanner: %s unavailable; base module missing", name);
        return 0;
    }

    for (std::size_t i = 0; i < rva_count; ++i) {
        const uintptr_t target = base + rvas[i];
        if (ExpectBytes(target, expected)) {
            Log("scanner: %s found at known RVA 0x%08llX",
                name,
                static_cast<unsigned long long>(rvas[i]));
            return target;
        }
    }

    Log("scanner: %s known RVA candidates mismatched expected bytes", name);
    return 0;
}

SectionSpan EnumerateMainModuleImageSpan() {
    const auto module = reinterpret_cast<const std::uint8_t*>(GetScannerGameModule());
    if (module == nullptr) {
        Log("scanner: CrimsonDesert.exe module handle unavailable");
        return {};
    }

    const auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        Log("scanner: invalid DOS header");
        return {};
    }

    const auto nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        Log("scanner: invalid NT header");
        return {};
    }

    const auto image_size = static_cast<std::size_t>(nt_headers->OptionalHeader.SizeOfImage);
    if (image_size == 0) {
        Log("scanner: main-module image size is zero");
        return {};
    }

    return {
        module,
        image_size,
        "[image]",
        true,
    };
}

std::vector<SectionSpan> EnumerateMainModuleSections(const bool text_only) {
    std::vector<SectionSpan> sections;

    const auto module = reinterpret_cast<const std::uint8_t*>(GetScannerGameModule());
    if (module == nullptr) {
        Log("scanner: CrimsonDesert.exe module handle unavailable");
        return sections;
    }

    const auto dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        Log("scanner: invalid DOS header");
        return sections;
    }

    const auto nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        Log("scanner: invalid NT header");
        return sections;
    }

    const auto first_section = IMAGE_FIRST_SECTION(nt_headers);
    for (unsigned i = 0; i < nt_headers->FileHeader.NumberOfSections; ++i) {
        const auto& section = first_section[i];

        char name_buffer[9]{};
        std::memcpy(name_buffer, section.Name, 8);
        const std::string section_name(name_buffer);

        if (text_only && section_name != ".text") {
            continue;
        }

        if (section.Misc.VirtualSize == 0) {
            continue;
        }

        const bool executable = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        if (!text_only && !executable) {
            continue;
        }

        sections.push_back({
            module + section.VirtualAddress,
            static_cast<std::size_t>(section.Misc.VirtualSize),
            section_name,
            executable,
        });
    }

    return sections;
}

std::vector<PatternByte> ParsePattern(const char* text) {
    std::vector<PatternByte> bytes;
    std::string token;

    for (const char* cursor = text; *cursor != '\0';) {
        while (*cursor == ' ') {
            ++cursor;
        }

        if (*cursor == '\0') {
            break;
        }

        const char* token_start = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            ++cursor;
        }

        token.assign(token_start, cursor);
        if (token == "?" || token == "??" || token == "*") {
            bytes.push_back({0, true});
        } else {
            bytes.push_back({static_cast<std::uint8_t>(std::stoul(token, nullptr, 16)), false});
        }
    }

    return bytes;
}

std::vector<MatchResult> ScanPattern(const std::vector<SectionSpan>& spans,
                                     const std::vector<PatternByte>& pattern,
                                     const std::size_t max_results) {
    std::vector<MatchResult> results;
    if (pattern.empty()) {
        return results;
    }

    for (const auto& span : spans) {
        if (span.begin == nullptr || span.size < pattern.size()) {
            continue;
        }

        const auto end = span.size - pattern.size();
        for (std::size_t offset = 0; offset <= end; ++offset) {
            bool matched = true;
            for (std::size_t index = 0; index < pattern.size(); ++index) {
                if (!pattern[index].wildcard && span.begin[offset + index] != pattern[index].value) {
                    matched = false;
                    break;
                }
            }

            if (!matched) {
                continue;
            }

            results.push_back({
                reinterpret_cast<uintptr_t>(span.begin + offset),
                span.section_name,
            });

            if (results.size() >= max_results) {
                return results;
            }
        }
    }

    return results;
}

ScanOutcome ScanInSections(const PatternDefinition* definitions,
                          const std::size_t definition_count) {
    bool saw_ambiguous = false;

    for (int pass = 0; pass < 2; ++pass) {
        const bool text_only = pass == 0;
        const auto sections = EnumerateMainModuleSections(text_only);
        if (sections.empty()) {
            continue;
        }

        for (std::size_t definition_index = 0; definition_index < definition_count; ++definition_index) {
            const auto& definition = definitions[definition_index];
            const auto pattern = ParsePattern(definition.text);
            const auto matches = ScanPattern(sections, pattern, 16);

            if (matches.size() == 1) {
                return {
                    matches.front().address + definition.hook_offset,
                    ScanStatus::Unique,
                };
            }

            if (matches.size() > 1) {
                saw_ambiguous = true;
                Log("scanner: pattern %s ambiguous in %s (%llu matches), trying next variant",
                    definition.name != nullptr ? definition.name : "<unnamed>",
                    text_only ? ".text" : "executable-sections",
                    static_cast<unsigned long long>(matches.size()));
            }
        }
    }

    const auto image_span = EnumerateMainModuleImageSpan();
    if (image_span.begin == nullptr || image_span.size == 0) {
        return saw_ambiguous ? ScanOutcome{0, ScanStatus::Ambiguous} : ScanOutcome{};
    }

    const std::vector<SectionSpan> image_spans{image_span};
    for (std::size_t definition_index = 0; definition_index < definition_count; ++definition_index) {
        const auto& definition = definitions[definition_index];
        const auto pattern = ParsePattern(definition.text);
        const auto matches = ScanPattern(image_spans, pattern, 16);

        if (matches.size() == 1) {
            return {
                matches.front().address + definition.hook_offset,
                ScanStatus::Unique,
            };
        }

        if (matches.size() > 1) {
            saw_ambiguous = true;
            Log("scanner: pattern %s ambiguous in image (%llu matches), trying next variant",
                definition.name != nullptr ? definition.name : "<unnamed>",
                static_cast<unsigned long long>(matches.size()));
        }
    }

    return saw_ambiguous ? ScanOutcome{0, ScanStatus::Ambiguous} : ScanOutcome{};
}

}  // namespace

PlayerPointerCaptureTarget ScanForPlayerPointerCapture() {
    const auto module=GetScannerGameModule();
    psm::v3446::Image current{};
    if(current.Init(module,psm::v3446::ModuleSize(module)) && psm::v3446::HasCurrentResourceLayout(current)) {
        const auto capture=current.Find(psm::v3446::kPlayerCapture3663);
        return {capture && psm::v3446::ExactUnwind(current,capture,psm::v3446::kCaptureUnwind3663)
                    ? reinterpret_cast<uintptr_t>(module)+capture+0x11 : 0,
                PlayerPointerCaptureKind::CurrentPlayerCaptureBaseRax};
    }
    // 1.0.0.2079: this function receives the live local-character capture
    // object in RAX at the hook site.  Unlike the legacy marker sites below,
    // it runs on character switches and resolves both actor and status marker.
    static constexpr PatternDefinition kCurrentCapturePatterns[] = {
        {
            "current-player-capture-1.0.0.2079",
            "0F B6 ?? ?? 8B ?? ?? 48 8B 58 40 48 8B 43 08 ?? 8D ?? 08 33 ?? ?? 85 ?? ?? 0F 44",
            7,
        },
    };

    const auto current_capture_outcome = ScanInSections(
        kCurrentCapturePatterns,
        sizeof(kCurrentCapturePatterns) / sizeof(kCurrentCapturePatterns[0]));
    if (current_capture_outcome.status == ScanStatus::Unique) {
        return {
            current_capture_outcome.address,
            PlayerPointerCaptureKind::CurrentPlayerCaptureBaseRax,
        };
    }

    static constexpr PatternDefinition kRdxPatterns[] = {
        {
            "rax-rdx-1.05.01-exe",
            "48 8B 40 68 48 8B 50 20 4C 8D 45 D7 0F B7 52 30 E8 59 2D AE 01 C5 FA 10 45 D7 C5 F8 2F C6 76 6C",
            8,
        },
        {
            "rax-rdx-1.05.01-relaxed",
            "48 8B 40 68 48 8B 50 20 4C 8D 45 D7 0F B7 52 30 E8 ?? ?? ?? ?? C5 FA 10 45 D7",
            8,
        },
    };

    const auto rdx_outcome = ScanInSections(kRdxPatterns, sizeof(kRdxPatterns) / sizeof(kRdxPatterns[0]));
    if (rdx_outcome.status == ScanStatus::Unique) {
        return {rdx_outcome.address, PlayerPointerCaptureKind::LegacyMarkerRdx};
    }

    static constexpr PatternDefinition kRsiPatterns[] = {
        {
            "rcx-based",
            "48 8B 41 68 48 8B 70 20 48 81 C6 B0 03 00 00 85 D2 75 0D 48",
            8,
        },
        {
            "rax-fallback-2",
            "48 8B 40 68 48 8B 70 20 48 85 F6 75 02 EB 73 0F B7 5D 77 48 8B 46 08 48 8D",
            8,
        },
        {
            "rax-fallback-3",
            "48 8B 40 68 48 8B 70 20 4C 8B 66 78 4D 3B 34 24 74 15 49 8B CE E8",
            8,
        },
        {
            "rax-primary",
            "48 8B 40 68 48 8B 70 20 0F B7 7D 77 44 8B 75 BB 4C 8B 7E 08 48 8B 86 88 00",
            8,
        },
    };

    const auto rsi_outcome = ScanInSections(kRsiPatterns, sizeof(kRsiPatterns) / sizeof(kRsiPatterns[0]));
    if (rsi_outcome.status == ScanStatus::Unique) {
        return {rsi_outcome.address, PlayerPointerCaptureKind::LegacyMarkerRsi};
    }

    if (current_capture_outcome.status == ScanStatus::Ambiguous ||
        rdx_outcome.status == ScanStatus::Ambiguous ||
        rsi_outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: player-pointer found multiple matches, install failed");
        return {};
    }

    Log("scanner: player-pointer found 0 matches");
    return {};
}

MountPointerCaptureTarget ScanForMountPointerCapture() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary-1.05.01-exe",
            "80 BF 94 00 00 00 00 0F 85 F1 02 00 00 4C 8B 4F 68 48 8B CF E8 A5 84 E7 FF C5 F8 28 DE 84",
            20,
        },
        {
            "primary-1.05.01-relaxed",
            "80 BF 94 00 00 00 00 0F 85 ?? ?? ?? ?? 4C 8B 4F 68 48 8B CF E8 ?? ?? ?? ?? C5 F8 28 DE 84",
            20,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: mount-pointer found multiple matches, install failed");
        return {};
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: mount-pointer found 0 matches");
        return {};
    }

    return {outcome.address};
}

uintptr_t ScanForPositionHeightAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "position-height-v1001235",
            "41 0F 11 45 00 48 8B BB F8 00 00 00 48 63 83 00 01 00 00 48 8D 34 C7",
            0,
        },
        {
            "position-height-relaxed",
            "41 0F 11 45 00 48 8B BB F8 00 00 00",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Unique) {
        return outcome.address;
    }

    static constexpr std::array<std::uint8_t, 5> kExpectedBytes = {
        0x41, 0x0F, 0x11, 0x45, 0x0
    };
    static constexpr uintptr_t kKnownRvas[] = {
        0x0384782C,  // CrimsonDesert.exe 1.0.0.1235
        0x0381859C,  // CrimsonDesert.exe 1.05.01
    };
    return ResolveExpectedRvaCandidates("position-height", kKnownRvas, std::size(kKnownRvas), kExpectedBytes);
}

uintptr_t ScanForDamageBattleAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9",
            0,
        },
        {
            "fallback",
            "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9 0F 84 ?? ?? ?? ??",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: damage-battle found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: damage-battle found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForDragonVillageSummonJump() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "village-v1001235-context",
            "C7 44 24 50 06 00 00 00 89 5C 24 48 4C 8D 0D ?? ?? ?? ?? 4C 89 4C 24 40 44 89 6C 24 38 4C 89 4C 24 30 4C 89 4C 24 28 4C 89 4C 24 20 B1 01 E8 ?? ?? ?? ?? 41 89 1C 24 E9 ?? ?? ?? ?? 41 C6 87 C0 00 00 00 01",
            51,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Unique) {
        return outcome.address;
    }

    static constexpr std::array<std::uint8_t, 4> kExpectedBytes = {
        0x41, 0x89, 0x1C, 0x24
    };
    static constexpr uintptr_t kKnownRvas[] = {
        0x01CC04A9,  // CrimsonDesert.exe 1.0.0.1235
        0x01C9CDB9,  // CrimsonDesert.exe 1.05.01
    };
    return ResolveExpectedRvaCandidates("dragon-village-summon", kKnownRvas, std::size(kKnownRvas), kExpectedBytes);
}

uintptr_t ScanForDragonFlyingRestrictWrite() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "flying-result-write",
            "41 88 47 04 41 89 3F 48 8B 5C 24 ?? 48 8B 6C 24 ?? 48 8B B4 24 ?? ?? ?? ?? 48 83 C4 40 41 5F",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Unique) {
        return outcome.address;
    }

    static constexpr std::array<std::uint8_t, 7> kExpectedBytes = {
        0x41, 0x88, 0x47, 0x04, 0x41, 0x89, 0x3F
    };
    static constexpr uintptr_t kKnownRvas[] = {
        0x01E4746B,  // CrimsonDesert.exe 1.0.0.1235
        0x01E21E4B,  // CrimsonDesert.exe 1.05.01
    };
    return ResolveExpectedRvaCandidates("dragon-flying-restrict", kKnownRvas, std::size(kKnownRvas), kExpectedBytes);
}

uintptr_t ScanForDragonRoofRestrictTest() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "roof-test-context",
            "48 8D 3D ?? ?? ?? ?? 85 DB 74 59 48 8B 05 ?? ?? ?? ?? 44 8B C3 48 8D 54 24 50",
            7,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Unique) {
        return outcome.address;
    }

    static constexpr std::array<std::uint8_t, 4> kExpectedBytes = {
        0x85, 0xDB, 0x74, 0x59
    };
    static constexpr uintptr_t kKnownRvas[] = {
        0x002E42CB,  // CrimsonDesert.exe 1.0.0.1235
        0x002E2F0B,  // CrimsonDesert.exe 1.05.01
    };
    return ResolveExpectedRvaCandidates("dragon-roof-restrict", kKnownRvas, std::size(kKnownRvas), kExpectedBytes);
}

bool ValidateItemNativeEntriesBeforeEnable(const ItemGainCaptureTarget& target) {
    psm::v3446::Image game;
    const auto module = GetScannerGameModule();
    if (!game.Init(module, psm::v3446::ModuleSize(module))) return false;
    const bool current = target.weighted_offset == psm::items3732::kCurrentWeightedOffset;
    if (!current && !psm::v3446::HasCurrentResourceLayout(game)) return true;
    HMODULE menu = nullptr;
    GetModuleHandleExW(0, L"DesertLinkModMenu.asi", &menu);
    using namespace psm::item_insert_3663;
    const bool stable = current ? psm::items3732::ValidateBeforeEnable(game, menu, target) :
        (ObserveStable(game, menu, Lane::Insert) && ObserveStable(game, menu, Lane::OwnerLookup));
    if (menu) FreeLibrary(menu);
    return stable;
}

ItemGainCaptureTarget ScanForItemGainAccess() {
    struct ObserverReference {
        HMODULE module=nullptr;
        explicit ObserverReference(const wchar_t* name){GetModuleHandleExW(0,name,&module);}
        ~ObserverReference(){if(module)FreeLibrary(module);}
    } observer(L"DesertLinkModMenu.asi"),bulk_observer(L"BulkSellDonate.asi");
    unsigned capture_mask=0;psm::items3446::ResolveTrace primary_trace{};
    if (const auto current=psm::items3446::ResolveDetailed(GetScannerGameModule(),observer.module,&capture_mask,&primary_trace,bulk_observer.module); current.address) {
        const char* insert_route = primary_trace.insert_route == psm::item_insert_3663::Route::Menu
            ? "menu-capacity-capture" : "native";
        Log("scanner: %s exact receipt/source/SQL/metadata group validated observer-capture-mask=%u owner-route=%u bulk-supported=%d insert-route=%s; native entry calls and foreign hooks preserved",
            primary_trace.profile3732 ? "item3732" : "item3663", capture_mask,primary_trace.owner_route,primary_trace.bulk_supported?1:0,insert_route);
        return current;
    }
    // Explain the PRIMARY strict group failure before any legacy fallback.
    // This is diagnostic evidence only and never authorizes an unknown hook.
    static std::atomic<unsigned> primary_failure_messages{0};
    if(primary_failure_messages.fetch_add(1,std::memory_order_relaxed)<4){
        Log("scanner: primary %s group rejected stage=%u missing-mask=%06X observer-present=%d observer-supported=%d observer-stamp=%08X observer-image=%X bulk-present=%d bulk-supported=%d owner-route=%u call-rva=0x%llX expected-rva=0x%llX observed-rva=0x%llX",primary_trace.profile3732?"item3732":"item3663",unsigned(primary_trace.stage),primary_trace.missing,primary_trace.observer_present?1:0,primary_trace.observer_supported?1:0,primary_trace.observer_stamp,primary_trace.observer_size,primary_trace.bulk_present?1:0,primary_trace.bulk_supported?1:0,primary_trace.owner_route,static_cast<unsigned long long>(primary_trace.failed_call),static_cast<unsigned long long>(primary_trace.expected_target),static_cast<unsigned long long>(primary_trace.observed_target));
        for(size_t i=0;i<primary_trace.profiles.size();++i)if(primary_trace.missing&(uint32_t{1}<<i))Log("scanner: primary required item profile missing name=%s",psm::items3446::kResolveNames[i]);
    }
    if (primary_trace.profile3732) return {};
    psm::v3446::Image current_image;
    const auto game = GetScannerGameModule();
    if (current_image.Init(game, psm::v3446::ModuleSize(game)) && psm::v3446::HasCurrentResourceLayout(current_image))
        return {};
    // Follow ItemPushInventory's destination call. A qword-add search also
    // matches unrelated reward aggregators and misses the native new-stack path.
    const auto sections = EnumerateMainModuleSections(false);
    const auto calls = ScanPattern(sections, ParsePattern(
        "88 44 24 20 44 0F B7 CF 4D 8B C6 48 8D 55 84 48 8B CB E8 ?? ?? ?? ?? 8B 5D 84 E9"), 2);
    if (calls.size() != 1) {
        Log("scanner: item-receipt caller missing or ambiguous; no aggregation fallback");
        return {};
    }
    const auto in_code = [&sections](uintptr_t address, size_t size) {
        for (const auto& section : sections) {
            const auto begin = reinterpret_cast<uintptr_t>(section.begin);
            if (section.executable && address >= begin && size <= section.size &&
                address - begin <= section.size - size) return true;
        }
        return false;
    };
    const auto resolve = [&in_code](uintptr_t call) {
        if (!in_code(call, 5)) return uintptr_t{0};
        auto address = ResolveRelativeBranch(call, 0xE8);
        for (int i = 0; i < 3 && in_code(address, 5); ++i) {
            const auto next = ResolveRelativeBranch(address, 0xE9);
            if (!next) return address;
            address = next;
        }
        return uintptr_t{0};
    };
    const auto call = calls.front().address + 18;
    const auto module = reinterpret_cast<const uint8_t*>(GetScannerGameModule());
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    const auto& exceptions = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exceptions.VirtualAddress > nt->OptionalHeader.SizeOfImage ||
        exceptions.Size > nt->OptionalHeader.SizeOfImage - exceptions.VirtualAddress ||
        exceptions.Size % sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY) != 0) return {};
    const auto entries = reinterpret_cast<const IMAGE_RUNTIME_FUNCTION_ENTRY*>(module + exceptions.VirtualAddress);
    uintptr_t push = 0;
    const auto call_rva = call - reinterpret_cast<uintptr_t>(module);
    for (size_t i = 0; i < exceptions.Size / sizeof(*entries); ++i) {
        if (call_rva >= entries[i].BeginAddress && call_rva < entries[i].EndAddress) {
            if (push) return {};
            push = reinterpret_cast<uintptr_t>(module) + entries[i].BeginAddress;
        }
    }
    if (!in_code(push, 0x26) || !ExpectBytes(push, std::array<uint8_t, 17>{
        0x48,0x89,0x54,0x24,0x10,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57})) return {};
    const auto receiver = resolve(call);
    const auto within = [](uintptr_t start, size_t size, const char* pattern) {
        const std::vector<SectionSpan> spans{{reinterpret_cast<const uint8_t*>(start), size, "receipt-body", true}};
        const auto matches = ScanPattern(spans, ParsePattern(pattern), 2);
        return matches.size() == 1 ? matches.front().address : uintptr_t{0};
    };
    const bool receiver2944 = in_code(receiver, 0x285) && ExpectBytes(receiver, std::array<uint8_t, 31>{
        0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x57,0x48,0x83,0xEC,0x28,
        0x45,0x0F,0xBF,0xE1,0x4C,0x89,0xC5,0x49,0x89,0xD7,0x48,0x89,0xCE,
        0x66,0x44,0x39,0x61,0x08});
    const bool receiver2760 = in_code(receiver, 0x240) && ExpectBytes(receiver, std::array<uint8_t, 29>{
        0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x28,
        0x45,0x0F,0xBF,0xF9,0x4C,0x89,0xC5,0x49,0x89,0xD6,0x48,0x89,0xCF,0x66,0x44,0x39});
    if (!receiver2944 && !receiver2760) {
        Log("scanner: item-receipt destination ABI rejected");
        return {};
    }
    const size_t receiver_size = receiver2944 ? 0x285 : 0x240;
    const auto merge = within(receiver, receiver_size, receiver2944 ?
        "4C 8B 45 10 48 8D 54 24 78 44 89 E1 48 69 C9 C8 00 00 00 48 03 0E E8 ?? ?? ?? ??" :
        "4C 8B 45 10 48 8D 54 24 78 44 89 F9 48 69 C9 C8 00 00 00 48 03 0F E8 ?? ?? ?? ??");
    const auto copy = within(receiver, receiver_size, receiver2944 ?
        "48 89 EA E8 ?? ?? ?? ?? 48 8B 3E 4C 8B 65 18" :
        "48 89 EA E8 ?? ?? ?? ?? 4C 8B 3F 4C 8B 65 18");
    const auto quantity = merge ? resolve(merge + 22) : 0;
    const auto assignment = copy ? resolve(copy + 3) : 0;
    const auto metadata_call = within(receiver, receiver_size, receiver2944 ?
        "49 8D 4D 08 4C 01 F1 48 89 44 24 60 E8 ?? ?? ?? ?? 44 02 05 ?? ?? ?? ?? 80 B8 F0 03 00 00 00" :
        "49 8D 4C 24 08 48 89 44 24 60 48 01 F1 E8 ?? ?? ?? ?? 80 B8 F0 03 00 00 00");
    const auto item_info = metadata_call ? resolve(metadata_call + (receiver2944 ? 12 : 13)) : 0;
    const auto occupancy_call = within(receiver, receiver_size, receiver2944 ?
        "48 8D 4C 24 78 66 89 5C 24 78 E8 ?? ?? ?? ?? 0F B6 94 24 80 00 00 00 48 89 C1 E8 ?? ?? ?? ??" :
        "0F B7 CB 48 8B 55 10 E8 ?? ?? ?? ??");
    const auto occupancy = occupancy_call ? resolve(occupancy_call + (receiver2944 ? 26 : 7)) : 0;
    // 2944 calls the same metadata lookup before a capacity helper. For the
    // stack_size > 1 items accepted by our existing policy, it returns info+18
    // unchanged. The new nonstackable-item special path remains excluded.
    const bool stack_contract = receiver2944 ?
        (in_code(occupancy, 0x6C) && resolve(occupancy_call + 10) == item_info &&
         ExpectBytes(occupancy, std::array<uint8_t, 13>{0x4C,0x8B,0x49,0x18,0x49,0x89,0xCA,
            0x49,0x83,0xF9,0x01,0x7F,0x5B}) &&
         ExpectBytes(occupancy + 0x68, std::array<uint8_t,4>{0x4C,0x89,0xC8,0xC3})) :
        (in_code(occupancy, 0xA6) && [&]() {
            const auto read = within(occupancy, 0xA6,
                "E8 ?? ?? ?? ?? 49 89 C2 4C 8B 48 18 49 83 F9 01");
            return read && resolve(read) == item_info;
        }());
    if (!in_code(quantity, 0xD4) || !in_code(assignment, 0x50) ||
        !in_code(item_info, 0x30) || !stack_contract ||
        !within(item_info, 0x30, "0F B7 39 48 8B 1D ?? ?? ?? ?? 3B 7B 08") ||
        !within(quantity, 0xD4, "4C 01 C2 48 89 53 10 4C 89 D8 41 C7 03 00 00 00 00") ||
        !within(assignment, 0x50, "48 8B 43 10 49 89 46 10 48 8B 43 18 49 89 46 18")) {
        Log("scanner: item-receipt merge/new-stack/metadata group rejected");
        return {};
    }
    // A second native transfer owns push at +0 and pop at +0x80, without a
    // MoveItemBehavior parent. Accept only its source-success gated push call.
    const auto transfers = ScanPattern(sections, ParsePattern(
        "48 8D 8F 80 00 00 00 48 8D 95 ?? ?? ?? ?? E8 ?? ?? ?? ?? "
        "8B 18 85 DB 75 1D 39 5F 60 74 15 48 8D 95 ?? ?? ?? ?? "
        "48 8B CF E8 ?? ?? ?? ?? 8B 18 85 DB"), 16);
    if (transfers.size() == 16) return {};
    uintptr_t transfer = 0;
    uintptr_t transfer_begin = 0;
    size_t transfer_size = 0;
    for (const auto& candidate : transfers) {
        const auto rva = candidate.address - reinterpret_cast<uintptr_t>(module);
        for (size_t i = 0; i < exceptions.Size / sizeof(*entries); ++i) {
            if (rva >= entries[i].BeginAddress && rva + 49 <= entries[i].EndAddress) {
                const auto begin = reinterpret_cast<uintptr_t>(module) + entries[i].BeginAddress;
                const auto size = entries[i].EndAddress - entries[i].BeginAddress;
                if (in_code(begin, size) && size &&
                    within(begin, size, "49 3B DD 0F 95 C0 66 83 C0 37 66 89 45 A0")) {
                    if (transfer) {
                        Log("scanner: standalone item transfer ambiguous");
                        return {};
                    }
                    transfer = candidate.address;
                    transfer_begin = begin;
                    transfer_size = size;
                }
            }
        }
    }
    const auto pop = transfer ? resolve(transfer + 14) : 0;
    const auto table_read = in_code(pop, 0x120) ? within(pop, 0x120,
        "4C 8B 15 ?? ?? ?? ?? 41 83 7A 6C 00 74 59 41 8B 4A 68 "
        "44 0F B7 8B DA 00 00 00") : 0;
    if (!transfer || resolve(transfer + 40) != push || !table_read ||
        !within(pop, 0x120, "48 8B 49 10 E8 ?? ?? ?? ?? 48 8B E8 48 85 C0") ||
        !within(pop, 0x120, "48 8B 5F 18 8B 4F 20 48 69 F1 E0 00 00 00 48 03 F3") ||
        !within(pop, 0x120, "49 C1 E3 08 4D 03 5A 78 41 8B 3B 85 FF") ||
        !within(pop, 0x120, "49 8B 8A 80 00 00 00 41 8B 54 C3 0C 4C 8B 04 D1 41 0F B7 48 04") ||
        !within(pop, 0x120, "49 83 C0 06 74 A3 45 0F B7 00") ||
        !within(pop, 0x120, "48 8B 43 10 48 8D 54 24 50 44 0F B7 8B D8 00 00 00 48 8B CD "
            "48 89 44 24 20 E8 ?? ?? ?? ?? 8B 00 85 C0 75 39")) {
        Log("scanner: standalone item transfer call/layout/mode contract rejected");
        return {};
    }
    int32_t table_displacement = 0;
    std::memcpy(&table_displacement, reinterpret_cast<const void*>(table_read + 3), sizeof(table_displacement));
    const auto table_global = table_read + 7 + table_displacement;
    const auto module_base = reinterpret_cast<uintptr_t>(module);
    if (table_global < module_base || table_global - module_base > nt->OptionalHeader.SizeOfImage - sizeof(uintptr_t)) return {};

    // Resolve preparation inside the same standalone transaction, before source
    // E0 receipts exist. The callback reads pending pop requests at this point.
    const auto plan_context = within(transfer_begin, transfer_size,
        "48 8B 9D A0 00 00 00 48 8D 95 30 01 00 00 48 8B CB E8 ?? ?? ?? ?? "
        "8B 38 85 FF 0F 85 ?? ?? ?? ?? 48 8D 8B 80 00 00 00 "
        "48 8D 95 34 01 00 00 E8 ?? ?? ?? ?? 8B 38 85 FF");
    const auto source_context = within(transfer_begin, transfer_size,
        "48 8D 8B 80 00 00 00 C6 44 24 50 01 44 88 64 24 48 C6 44 24 40 01 "
        "33 C0 48 89 44 24 38 66 44 89 7C 24 30 C6 44 24 28 03 48 89 74 24 20 "
        "4C 8D 8D 80 01 00 00 4C 8B 85 80 05 00 00 48 8D 95 2C 01 00 00 E8 ?? ?? ?? ??");
    const auto plan = plan_context ? resolve(plan_context + 17) : 0;
    const auto source_init = source_context ? resolve(source_context + 66) : 0;
    const auto source_plan = plan_context ? resolve(plan_context + 46) : 0;
    if (!plan_context || !source_context || source_context >= plan_context || plan_context >= transfer ||
        transfer < transfer_begin + 7 || !ExpectBytes(transfer - 7, std::array<uint8_t, 7>{
            0x48,0x8B,0xBD,0xA0,0x0,0x0,0x0}) ||
        !in_code(plan, 0x126) || !in_code(source_init, 0x118) || !in_code(source_plan, 0x26A)) return {};
    if (!ExpectBytes(source_init, std::array<uint8_t, 15>{
            0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18}) ||
        !ExpectBytes(source_plan, std::array<uint8_t, 15>{
            0x48,0x89,0x5C,0x24,0x20,0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4C,0x24,0x08})) return {};
    const auto sql_gate = within(transfer_begin, transfer_size,
        "48 8D 95 38 01 00 00 48 8B 8D A0 00 00 00 E8 ?? ?? ?? ?? "
        "8B 38 85 FF 0F 85 ?? ?? ?? ??");
    if (!sql_gate || sql_gate <= plan_context || sql_gate + 29 >= transfer || !resolve(sql_gate + 14)) return {};
    int32_t sql_failure_displacement = 0;
    std::memcpy(&sql_failure_displacement, reinterpret_cast<const void*>(sql_gate + 25), 4);
    const auto sql_failure = sql_gate + 29 + sql_failure_displacement;
    if (sql_failure <= transfer + 49 || sql_failure >= transfer_begin + transfer_size) return {};

    // Exact frame/control flow guarantees [ctx.rsp+0x78] is the native return,
    // RDI is push, R14 is &push->receipts, and R15 is its already-resolved owner.
    if (within(plan, 0x126,
        "48 89 5C 24 10 48 89 6C 24 18 56 57 41 54 41 56 41 57 48 83 EC 50 "
        "83 79 50 00 48 8B F2 48 8B F9 0F 84 DE 00 00 00 48 8B 49 10 E8 ?? ?? ?? ?? "
        "4C 8B F8 48 85 C0 75 0D 8B 05 ?? ?? ?? ?? 89 06 E9 C6 00 00 00 "
        "80 7F 7A 00 0F B7 47 68 41 0F 94 C4 66 83 E8 07 66 83 F8 3D 77 13 "
        "48 B9 81 00 00 01 00 00 00 20 48 0F A3 C1 73 03 41 B4 01 "
        "48 8B 5F 48 4C 8D 77 58 8B 47 50 48 8D 0C 40 48 8D 2C CB 48 3B DD 74 5B "
        "66 66 66 0F 1F 84 00 00 00 00 00 48 8B 4B 08 4C 8D 44 24 40 0F B7 03 "
        "48 8D 94 24 80 00 00 00 66 89 44 24 38 4D 8B CE 0F B6 47 7A "
        "48 89 4C 24 40 8B 4B 10 89 4C 24 48 49 8B CF 44 88 64 24 30 88 44 24 20 "
        "E8 ?? ?? ?? ?? 8B 00 85 C0 0F 85 66 FF FF FF 48 83 C3 18 48 3B DD 75 B0 "
        "83 7F 60 00 74 1E 48 8B 4F 28 48 8D 94 24 80 00 00 00 4D 8B C6 "
        "E8 ?? ?? ?? ?? 8B 00 85 C0 0F 85 39 FF FF FF C7 06 00 00 00 00 "
        "4C 8D 5C 24 50 48 8B C6 49 8B 5B 38 49 8B 6B 40 49 8B E3 41 5F 41 5E 41 5C 5F 5E C3") != plan) {
        Log("scanner: item plan frame/register contract rejected");
        return {};
    }
    const auto push_owner = within(push, 0x60, "48 8B 49 10 E8 ?? ?? ?? ?? 48 89 45 98 48 85 C0");
    const auto owner = resolve(plan + 0x2A);
    const auto source_owner = within(source_plan, 0x70, "48 8B 49 10 E8 ?? ?? ?? ?? 48 8B C8 48 85 C0");
    const auto debt_owner = within(push, 0x70, "49 8B 4D 10 E8 ?? ?? ?? ?? 48 8B F0 48 89 45 88");
    if (!in_code(owner, 0x7F) || !ExpectBytes(owner + 0x41, std::array<uint8_t, 1>{static_cast<uint8_t>(receiver2944 ? 0x5F : 0x5E)}) || !push_owner || !source_owner ||
        !debt_owner || resolve(debt_owner + 4) != owner ||
        resolve(push_owner + 4) != owner || resolve(source_owner + 4) != owner ||
        within(owner, 0x7F,
            "40 53 48 83 EC 20 48 8B 41 68 48 8B D9 48 8B 48 20 0F B7 41 30 "
            "48 8D 4C 24 30 66 89 44 24 30 E8 ?? ?? ?? ?? 48 8D 88 BE 00 00 00 "
            "B8 FF FF 00 00 66 39 01 75 04 B0 03 EB 09 E8 ?? ?? ?? ?? 0F B6 40 ?? "
            "0F B6 C8 84 C0 74 2E 83 F9 01 75 18 48 8B 9B A0 00 00 00 48 85 DB 74 1D "
            "48 8B 9B D0 00 00 00 48 85 DB 74 11 48 8B 43 68 48 8B 80 B8 00 00 00 "
            "48 83 C4 20 5B C3 33 C0 48 83 C4 20 5B C3") != owner) return {};

    // SQL row builder consumes R8's E0 vector and copies the delivered qword,
    // rather than rereading the original C8 request or the live destination.
    const auto rows = resolve(plan + 0xF5);
    if (!in_code(rows, 0x277) || within(rows, 0x30,
        "49 89 E3 4D 89 4B 20 49 89 53 10 53 56 57 41 57 48 83 EC 58 "
        "41 8B 40 08 31 F6 49 89 6B 08 49 89 D7 4D 89 63 D8 4D 89 C4") != rows ||
        !within(rows, 0x277, "49 8B 34 24 4C 01 FE 48 8B 86 D0 00 00 00 0F B7 8E C8 00 00 00") ||
        !within(rows, 0x277, "48 8B 8B E0 00 00 00 48 8B 46 10 49 89 04 0E "
            "48 8B 83 D8 00 00 00 49 C7 04 06 08 00 00 00") ||
        !within(rows, 0x277, "49 81 C7 E0 00 00 00 49 83 C5 04") ||
        !within(rows, 0x277, "49 83 C6 08 8B 44 24 20 39 C5 0F 82")) return {};

    const auto pending_metadata = within(source_init, 0x118,
        "48 8B CF C5 F8 77 E8 ?? ?? ?? ?? 80 B8 65 03 00 00 00");
    const auto source_read = within(source_plan, 0xA0,
        "48 8B 43 50 48 89 45 F0 8B 43 58 89 45 F8 4C 8D 6B 18 "
        "0F B6 43 62 88 44 24 30 C6 44 24 28 01 4C 89 6C 24 20 "
        "4C 8D 4D F0 4C 8B 43 10 48 8D 55 50 E8 ?? ?? ?? ??");
    const auto source_group = source_read ? resolve(source_read + 48) : 0;
    if (!pending_metadata || resolve(pending_metadata + 6) != item_info ||
        !within(source_init, 0x118, "4C 89 41 10 49 8B 39 41 8B 41 08 48 8D 0C 80 4C 8D 34 CF") ||
        !within(source_init, 0x118, "8B 45 58 C5 FC 10 07 48 8D 0C 80 48 8B 45 50 "
            "C5 FC 11 04 C8 C5 FB 10 4F 20 C5 FB 11 4C C8 20 FF 45 58 48 83 C7 28") ||
        !in_code(source_group, 0x409) ||
        !within(source_group, 0x409, "4D 8B D0 4C 8B E2 48 8B D1 33 F6") ||
        !within(source_group, 0x409, "49 8B 39 41 8B 41 08 48 8D 0C 80 4C 8D 2C CF")) return {};
    const auto source_consumer_call = within(source_group, 0x409,
        "4C 8D 4B 08 40 88 74 24 30 44 88 74 24 28 4C 89 7C 24 20 4D 8B C2 "
        "48 8D 95 A8 00 00 00 48 8B C8 C5 F8 77 E8 ?? ?? ?? ??");
    const auto source_consumer = source_consumer_call ? resolve(source_consumer_call + 35) : 0;
    if (!within(source_group, 0x409, "0F B7 5F 10 66 89 5C 24 48") ||
        !within(source_group, 0x409, "44 0F B7 03 48 8B 4A 18 8B 42 20 48 8D 14 C1 "
            "48 3B CA 74 13 48 8B 01 66 44 39 40 10 74 4B 48 83 C1 08 48 3B CA 75 ED") ||
        !in_code(source_consumer, 0x692) ||
        !ExpectBytes(source_consumer, std::array<uint8_t, 15>{
            0x48,0x89,0x5C,0x24,0x08,0x4C,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10}) ||
        !within(source_consumer, 0x692, "49 8B F0 48 8B FA 4C 8B E1 4D 8B 31 "
            "41 8B 41 08 4C 8D 0C 80 4B 8D 04 CE 48 89 44 24 50") ||
        !within(source_consumer, 0x692, "4D 8B 7E 18 4C 89 7C 24 30 4D 85 FF 0F 8E ?? ?? ?? ?? "
            "41 0F BF 46 12 66 44 3B E8") ||
        !within(source_consumer, 0x692, "41 0F B7 06 66 3B C7 74 09 66 3B C8 0F 85") ||
        !within(source_consumer, 0x692, "49 8B 46 18 48 89 44 24 70") ||
        !within(source_consumer, 0x692, "49 83 C6 28 45 33 D2 4C 3B 74 24 50")) return {};

    // Use the native push's canonical mapper and container list, not E0+DA:
    // destination planning does not propagate the source metadata-key tail.
    if (!in_code(push, 0x622)) return {};
    const auto map_call = within(push, 0x622,
        "41 0F B7 96 C8 00 00 00 48 8D 4D 70 E8 ?? ?? ?? ?? "
        "41 0F B7 BE D8 00 00 00 4C 8B 7D 98 49 8B 4F 18 41 8B 47 20 "
        "48 8D 14 C1 48 3B CA 74 1D 0F B7 45 70 0F 1F 00 "
        "48 8B 19 66 39 43 10 0F 84 ?? ?? ?? ?? 48 83 C1 08 48 3B CA 75 EA");
    const auto mapper = map_call ? resolve(map_call + 12) : 0;
    if (!in_code(mapper, 0x584) || within(mapper, 0x23,
        "4C 8B C1 66 83 FA 19 0F 87 6B 05 00 00 48 8D 0D ?? ?? ?? ?? "
        "0F B7 C2 8B 84 81 ?? ?? ?? ?? 48 03 C1 FF E0") != mapper ||
        !ExpectBytes(mapper + 0x578, std::array<uint8_t, 10>{
            0x66,0x41,0xC7,0x0,0xFF,0xFF,0x49,0x8B,0xC0,0xC3})) return {};
    int32_t mapper_base_displacement = 0;
    uint32_t mapper_table_rva = 0;
    std::memcpy(&mapper_base_displacement, reinterpret_cast<const void*>(mapper + 0x10), 4);
    std::memcpy(&mapper_table_rva, reinterpret_cast<const void*>(mapper + 0x1A), 4);
    if (mapper + 0x14 + mapper_base_displacement != module_base ||
        mapper_table_rva > nt->OptionalHeader.SizeOfImage ||
        26 * sizeof(uint32_t) > nt->OptionalHeader.SizeOfImage - mapper_table_rva) return {};
    static constexpr int kRouteCases[] = {0,1,-1,2,3,-1,-1,5,-1,4,0,6,7,8,9,10,12,13,14,15,16,17,18,19,11,20};
    static constexpr uint32_t kContainerFields[] = {
        0xA2,0xA6,0xA0,0xA4,0xA8,0xB2,0xB0,0xAA,0xAC,0xB4,0xB6,0xB8,0xBA,0xC6,0xC8,0xBC,0xBE,0xC0,0xC2,0xC4,0xAE};
    uintptr_t mapper_globals[2]{};
    for (size_t i = 0; i < std::size(kContainerFields); ++i) {
        const auto branch = mapper + 0x23 + i * 0x41;
        if (within(branch, 0x41,
            "65 48 8B 04 25 58 00 00 00 48 8B 0D ?? ?? ?? ?? 48 8B 10 B8 EC 01 00 00 "
            "80 3C 10 00 48 0F 45 0D ?? ?? ?? ?? 48 8B 01 48 8B 48 08 "
            "48 8B 81 40 02 00 00 0F B7 88 ?? ?? ?? ?? 49 8B C0 66 41 89 08 C3") != branch) return {};
        uint32_t field = 0;
        std::memcpy(&field, reinterpret_cast<const void*>(branch + 0x35), 4);
        if (field != kContainerFields[i]) return {};
        for (size_t j = 0; j < 2; ++j) {
            const size_t displacement_offset = j ? 0x20 : 0xC;
            int32_t displacement = 0;
            std::memcpy(&displacement, reinterpret_cast<const void*>(branch + displacement_offset), 4);
            const auto global = branch + displacement_offset + 4 + displacement;
            if (global < module_base || global - module_base > nt->OptionalHeader.SizeOfImage - sizeof(uintptr_t) ||
                (i && global != mapper_globals[j])) return {};
            mapper_globals[j] = global;
        }
    }
    for (size_t i = 0; i < std::size(kRouteCases); ++i) {
        uint32_t target_rva = 0;
        std::memcpy(&target_rva, module + mapper_table_rva + i * sizeof(target_rva), sizeof(target_rva));
        const auto expected = mapper + (kRouteCases[i] < 0 ? 0x578 : 0x23 + kRouteCases[i] * 0x41);
        if (module_base + target_rva != expected) return {};
    }
    const auto debt = within(push, 0x622,
        "8B 9E E0 00 00 00 83 EB 01 0F 88 ?? ?? ?? ?? 48 63 FB 48 C1 E7 04 0F 1F 44 00 00 "
        "48 8B B6 D8 00 00 00 49 8D 4E 08 E8 ?? ?? ?? ?? 8B 14 3E 3B 10 74 12 "
        "48 83 EF 10 83 EB 01 48 8B 75 88 79 DC E9 ?? ?? ?? ?? "
        "48 8B 4C 3E 08 4D 8B 46 10 89 55 A0 4C 3B C1");
    if (!debt || resolve(debt + 38) != item_info) return {};
    const auto plan_site = plan + 0xE6;
    const auto plan_caller_return = plan_context + 22;
    Log("scanner: item plan validated site=0x%p caller=0x%p mapper=0x%p",
        reinterpret_cast<void*>(plan_site), reinterpret_cast<void*>(plan_caller_return), reinterpret_cast<void*>(mapper));
    Log("scanner: item-receipt validated caller=0x%p receiver=0x%p merge=0x%p copy=0x%p standalone-return=0x%p",
        reinterpret_cast<void*>(call), reinterpret_cast<void*>(receiver),
        reinterpret_cast<void*>(quantity), reinterpret_cast<void*>(assignment), reinterpret_cast<void*>(transfer + 45));
    return {receiver, ItemGainCaptureKind::NativeItemReceipt, call + 5, item_info, push, transfer + 45, table_global,
        plan_site, plan_caller_return, mapper};
}

uintptr_t ScanForAffinityGainPrepare() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary-1.05.01-exe",
            "89 45 F0 48 8B 07 48 89 45 C8 8B 47 08 89 45 D0 48 8B 4F 10 48 89 4D D8 48 85 C9 74 2C 80",
            30,
        },
        {
            "primary-1.05.01-relaxed",
            "89 45 F0 48 8B 07 48 89 45 C8 8B 47 08 89 45 D0 48 8B 4F 10 48 89 4D D8 48 85 C9 74 ?? 80",
            30,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: affinity-gain-prepare found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: affinity-gain-prepare found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForAffinityCurrentWrite() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "08 48 8D 42 08 48 89 43 48 41 8B FC 48 8B 03 48 8B CB FF 50 10 85 FF 0F 85",
            5,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: affinity-current-write found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: affinity-current-write found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForAffinityVaryFriendly() {
    static constexpr std::array<std::uint8_t, 7> kExpectedBytes = {
        0x41, 0xFF, 0xD2, 0x90, 0x48, 0x8D, 0x1D,
    };
    static constexpr uintptr_t kKnownRvas[] = {
        0x0FAB2A84,  // CrimsonDesert.exe 1.0.0.1245
        0x0F48EFF4,  // CrimsonDesert.exe 1.0.0.1235
        0x0F995548,  // CrimsonDesert.exe 1.05.01
    };
    return ResolveExpectedRvaCandidates("affinity-vary-friendly callsite", kKnownRvas, std::size(kKnownRvas), kExpectedBytes);
}

uintptr_t ScanForAffinityVaryFriendlyWithLogout() {
    static constexpr std::array<std::uint8_t, 7> kExpectedBytes = {
        0x41, 0xFF, 0xD2, 0x90, 0x48, 0x8D, 0x1D,
    };
    static constexpr uintptr_t kKnownRvas[] = {
        0x0132DDFB,  // CrimsonDesert.exe 1.0.0.1235
        0x01319B8B,  // CrimsonDesert.exe 1.05.01
    };
    return ResolveExpectedRvaCandidates("affinity-vary-friendly-with-logout callsite",
                                        kKnownRvas,
                                        std::size(kKnownRvas),
                                        kExpectedBytes);
}

uintptr_t ScanForAffinityPetDiagnosticReloc() {
    static constexpr std::array<std::uint8_t, 7> kExpectedBytes = {
        0x41, 0xFF, 0xD2, 0x90, 0x48, 0x8D, 0x1D,
    };
    static constexpr uintptr_t kKnownRvas[] = {
        0x0130FFF5,  // CrimsonDesert.exe 1.0.0.1235
        0x012FBD75,  // CrimsonDesert.exe 1.05.01
    };
    return ResolveExpectedRvaCandidates("affinity-petdiag-reloc callsite",
                                        kKnownRvas,
                                        std::size(kKnownRvas),
                                        kExpectedBytes);
}

uintptr_t ScanForAffinityPetDiagnosticRsrc() {
    static constexpr std::array<std::uint8_t, 7> kExpectedBytes = {
        0x41, 0xFF, 0xD2, 0x90, 0x48, 0x8D, 0x1D,
    };
    static constexpr uintptr_t kKnownRvas[] = {
        0x0C98C977,  // CrimsonDesert.exe 1.0.0.1245
        0x0C563647,  // CrimsonDesert.exe 1.0.0.1235
        0x0C7EBCBE,  // CrimsonDesert.exe 1.05.01
    };
    return ResolveExpectedRvaCandidates("affinity-petdiag-rsrc callsite",
                                        kKnownRvas,
                                        std::size(kKnownRvas),
                                        kExpectedBytes);
}

uintptr_t ScanForDurabilityWriteAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary-1.05.01-exe",
            "66 89 7B 50 40 88 7B 52 40 88 7B 54 EB 03 48 89 FB 48 8B 05 BA BE 15 F6 48",
            0,
        },
        {
            "primary-1.05.01-relaxed",
            "66 89 7B 50 40 88 7B 52 40 88 7B 54 EB ?? 48 89 FB 48 8B 05 ?? ?? ?? ?? 48",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: durability-write found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: durability-write found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForDurabilityDeltaAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "C1 79 C5 C1 06 66 41 03 C0 66 89 45 EC C4 C1 79 C5 C1 07 66 41 03 C1 66 89",
            5,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: durability-delta found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: durability-delta found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForAbyssDurabilityDeltaAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "0F B7 73 02 48 8B CB 66 41 3B F5 42 8D 04 2E 66 0F 4D F8 66 89 7B 02 E8",
            11,
        },
        {
            "fallback",
            "66 41 3B F5 42 8D 04 2E 66 0F 4D F8 66 89 7B 02",
            4,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: abyss-durability-delta found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: abyss-durability-delta found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForStaminaAb00Access() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "0F B7 D7 49 8B CE E8 ?? ?? ?? ?? 48 8B F0 48 85 DB 74 ?? 33 C0 66 89 44 24 20 38 46 53",
            11,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: stamina-ab00 found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: stamina-ab00 found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForSpiritDeltaAccess() {
    // Hook the wrapper call while its explicit 16-bit resource selector is
    // still available at rsp+0x38. The callee is shared by Stamina, Spirit,
    // and unrelated resources, so hooking the callee loses the discriminator.
    static constexpr PatternDefinition kPatterns[] = {
        {
            "explicit-resource-delta-caller-primary",
            "48 89 ?? 48 89 ?? E8 ?? ?? ?? ?? 84 C0 75 ?? 48 8B 5C 24 ?? 48 8B 74 24 ?? 48 83 C4 ?? 5F C3",
            6,
        },
        {
            "explicit-resource-delta-caller-fallback",
            "49 89 D8 48 89 FA 48 89 C1 48 89 C6 E8 ?? ?? ?? ?? 84 C0 75 ?? 48 8B 5C 24 30 48 8B 74 24 40 48 83 C4 20 5F C3",
            12,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: explicit resource-delta caller found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: explicit resource-delta caller found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForSharedResourceDeltaAccess() {
    static constexpr PatternDefinition kFunctionPatterns[] = {
        {
            "shared-resource-delta-1.0.0.2435",
            "48 89 5C 24 ?? 55 56 57 48 83 EC ?? 31 C0 4C 89 C3 66 89 44 24 ?? 48 89 D5 48 89 CE",
            0,
        },
    };

    const auto function_outcome = ScanInSections(
        kFunctionPatterns,
        sizeof(kFunctionPatterns) / sizeof(kFunctionPatterns[0]));
    if (function_outcome.status == ScanStatus::Unique &&
        IsSharedResourceDeltaFunction(function_outcome.address)) {
        Log("scanner: shared-resource-delta found directly at 0x%p",
            reinterpret_cast<void*>(function_outcome.address));
        return function_outcome.address;
    }

    static constexpr PatternDefinition kCallerPatterns[] = {
        {
            "resource-delta-caller-primary",
            "48 89 ?? 48 89 ?? E8 ?? ?? ?? ?? 84 C0 75 ?? 48 8B 5C 24 ?? 48 8B 74 24 ?? 48 83 C4 ?? 5F C3",
            6,
        },
        {
            "resource-delta-caller-fallback",
            "49 89 D8 48 89 FA 48 89 C1 48 89 C6 E8 ?? ?? ?? ?? 84 C0 75 ?? 48 8B 5C 24 30 48 8B 74 24 40 48 83 C4 20 5F C3",
            12,
        },
    };

    const auto outcome = ScanInSections(
        kCallerPatterns,
        sizeof(kCallerPatterns) / sizeof(kCallerPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: shared-resource-delta caller found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: shared-resource-delta found 0 matches");
        return 0;
    }

    const uintptr_t target = ResolveSharedResourceDeltaFromCall(outcome.address);
    if (target == 0) {
        Log("scanner: shared-resource-delta caller resolved to an invalid function");
        return 0;
    }
    Log("scanner: shared-resource-delta resolved caller=0x%p target=0x%p",
        reinterpret_cast<void*>(outcome.address),
        reinterpret_cast<void*>(target));
    return target;
}

uintptr_t ScanForNativeResourceRateAccess() {
    // RBX is the exact entry, R11 the normalized signed rate. This site is
    // before elapsed-time multiplication, clamping and fractional carry.
    static constexpr PatternDefinition kPatterns[] = {
        {"native-rate-rbx-r11-2760",
         "C4 E1 FB 2A 43 10 C5 FB 5E D3 C5 F8 57 C0 C4 E1 FB 2A 05 ?? ?? ?? ?? C5 FB 5E CB C5 EB 5E D1 C5 EB 59 DB C4 61 FB 2C DB 4D 85 C0 74 ??",
         40},
    };
    const auto result = ScanInSections(kPatterns, std::size(kPatterns));
    if (result.status != ScanStatus::Unique) {
        Log("scanner: native resource rate contract unresolved or ambiguous");
        return 0;
    }
    return result.address;
}

uintptr_t ScanForNativeResourceCostAccess() {
    // The getter has already evaluated timed recovery. RSI is the entry and
    // RBX is the one-time signed delta, still unclamped and uncommitted.
    static constexpr PatternDefinition kPatterns[] = {
        {"native-cost-rsi-rbx-2760",
         "48 8B 4C 24 58 4C 8D 04 19 48 C1 EB 3F 84 DB 74 ?? 48 8B 46 28 48 39 C1 7F ?? 48 39 F8",
         0},
    };
    const auto result = ScanInSections(kPatterns, std::size(kPatterns));
    if (result.status != ScanStatus::Unique) {
        Log("scanner: native resource cost contract unresolved or ambiguous");
        return 0;
    }
    return result.address;
}

bool ValidateNativeResourceGroup(const uintptr_t rate_site, const uintptr_t cost_site) {
    const auto module = GetScannerGameModule();
    psm::v3446::Image current{};
    if (current.Init(module, psm::v3446::ModuleSize(module)) &&
        psm::v3446::HasCurrentResourceLayout(current)) {
        const auto base = reinterpret_cast<uintptr_t>(module);
        return rate_site >= base && cost_site >= base &&
            psm::v3446::ResourceGroup(current, rate_site-base, cost_site-base, psm::menu_reader::Find(current));
    }
    if (cost_site < kMinimumPointerAddress + 5 || rate_site < kMinimumPointerAddress) return false;
    uintptr_t reader = ResolveRelativeBranch(cost_site - 5, 0xE8);
    for (int hop = 0; reader && hop < 3; ++hop) {
        const uintptr_t next = ResolveRelativeBranch(reader, 0xE9);
        if (!next) break;
        if (next == reader) return false;
        reader = next;
    }
    // The cost's getter must be the same RBX-entry evaluator being patched.
    static constexpr std::array<std::uint8_t, 12> capture = {
        0x4D, 0x8B, 0xF1, 0x49, 0x8B, 0xF0,
        0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
    };
    const bool valid = reader && rate_site > reader && rate_site - reader < 0x200 &&
                       ExpectBytes(reader + 0x15, capture);
    Log("scanner: native resource group linked=%d reader=0x%p rate=0x%p cost=0x%p",
        valid ? 1 : 0, reinterpret_cast<void*>(reader),
        reinterpret_cast<void*>(rate_site), reinterpret_cast<void*>(cost_site));
    return valid;
}

uintptr_t ScanForSpiritValueWriteAccess() {
    const auto module=GetScannerGameModule();
    psm::v3446::Image current{};
    if(current.Init(module,psm::v3446::ModuleSize(module))&&
       psm::v3446::HasCurrentResourceLayout(current)){
        const auto capture=ScanForStatWriteAccess();
        return capture.address?capture.address-0x43:0;
    }
    static constexpr PatternDefinition kPatterns[] = {
        {
            "shared-resource-commit-1.0.0.2760",
            "66 44 89 4C 24 20 48 89 54 24 10 53 55 56 57 41 56 48 83 EC 40 4C 8D 71 18 48 89 CF 48 8B 49 20 4C 89 C3 49 03 0E 4C 89 F6 4C 39 C1",
            0,
        },
        {
            "shared-resource-commit-1.0.0.1980",
            "48 89 5C 24 ?? 55 56 57 48 83 EC ?? 48 8B 59 18 41 0F B7 E9 48 03 59 20 48 89 D6 48 89 CF 4C 39 C3",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: spirit-value-write found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: spirit-value-write found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForStatsAccess() {
    const auto module=GetScannerGameModule();
    psm::v3446::Image current{};
    if(current.Init(module,psm::v3446::ModuleSize(module)) && psm::v3446::HasCurrentResourceLayout(current)) {
        const auto slot=current.Find(psm::v3446::kSlot);
        return slot ? reinterpret_cast<uintptr_t>(module)+slot+0x7B : 0;
    }
    static constexpr PatternDefinition kPatterns[] = {
        {"primary", "48 8D ?? ?? 48 C1 E0 04 48 03 46 58 ?? 8B ?? 24", 12},
        {"fallback", "48 C1 E0 04 48 03 46 58", 8},
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: stats found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: stats found 0 matches");
        return 0;
    }

    return outcome.address;
}

StatWriteCaptureTarget ScanForStatWriteAccess() {
    const auto module = GetScannerGameModule();
    psm::v3446::Image current{};
    if (current.Init(module, psm::v3446::ModuleSize(module)) &&
        psm::v3446::HasCurrentResourceLayout(current)) {
        auto writer = current.Find(psm::v3446::kWriter);
        // Both native direct entry+18 address computations are checked;
        // terminal state+52 remains a direct field in the exact writer profile.
        if(!psm::v3446::WriterFields(current,writer))writer=0;
        // RDI=entry, RBX=clamped value, before threshold/death notifications.
        // Both low-clamp branches compute RSI=entry+18 in the reviewed body.
        return {writer ? reinterpret_cast<uintptr_t>(module)+writer+0x43 : 0,
                StatWriteValueRegister::Rbx, true};
    }
    static constexpr PatternDefinition kPatterns[] = {
        {
            "after-native-clamp-1.0.0.2760",
            "48 89 CB EB 10 48 8B 47 28 48 39 C3 7D 07 48 89 C3 48 8D 77 18",
            21,
        },
        {
            "pre-notify-1.0.0.1980",
            "4C 8D 4C 24 58 49 89 F0 48 8D 54 24 40 E8 ?? ?? ?? ?? 48 31 15 ?? ?? ?? ?? 48 83 38 00 7F ?? C6 47 52 01",
            13,
        },
        {
            "pre-notify-semantic",
            "48 83 79 30 00 74 ?? 80 79 52 00 75 ?? 48 85 DB 7E ?? 4C 8D 4C 24 58 49 89 F0 48 8D 54 24 40 E8",
            31,
        },
        {
            "pre-notify-1.0.0.1744",
            "4C 8D 4C 24 58 49 89 F0 48 8D 54 24 40 E8 ?? ?? ?? ?? 66 8B 15 ?? ?? ?? ?? 00 15 ?? ?? ?? ?? 48 83 38 00 7F ?? C6 47 52 01",
            13,
        },
        {
            "pre-notify-relaxed-current",
            "4C 8D 4C 24 58 49 89 F0 48 8D 54 24 40 E8 ?? ?? ?? ?? ?? ?? 15 ?? ?? ?? ?? ?? 15 ?? ?? ?? ?? 48 83 38 00 7F ?? C6 47 52 01",
            13,
        },
        {
            "pre-notify-1.0.0.1673",
            "4C 8D 4C 24 58 49 89 F0 48 8D 54 24 40 E8 ?? ?? ?? ?? 8B 15 ?? ?? ?? ?? 00 15 ?? ?? ?? ?? 48 83 38 00 7F ?? C6 47 52 01",
            13,
        },
        {
            "pre-notify-1.0.0.1573",
            "4C 8D 4C 24 58 49 89 F0 48 8D 54 24 40 E8 ?? ?? ?? ?? 66 8B 15 ?? ?? ?? ?? 30 15 ?? ?? ?? ?? 48 83 38 00 7F ?? C6 47 52 01",
            13,
        },
        {
            "pre-notify-primary",
            "E8 ?? ?? ?? ?? 80 25 ?? ?? ?? ?? F8 48 83 38 00 7F ?? C6 47 52 01 EB ?? 80 7F 52 00 74 ?? 48 3B 5F 30 7D ?? 4C 8D 4C 24 58 49 89 F0 48 8D 54 24 40 48 89 F9 E8 ?? ?? ?? ??",
            0,
        },
        {
            "pre-notify-fallback",
            "E8 ?? ?? ?? ?? 80 25 ?? ?? ?? ?? F8 48 83 38 00 7F ?? C6 47 52 01",
            0,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: stat-write found multiple matches, install failed");
        return {};
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: stat-write found 0 matches");
        return {};
    }

    Log("scanner: stat-write contract value-register=rbx");
    return {outcome.address, StatWriteValueRegister::Rbx};
}
