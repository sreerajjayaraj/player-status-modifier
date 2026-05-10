#include "scanner.h"

#include "logger.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

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

SectionSpan EnumerateMainModuleImageSpan() {
    const auto module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (module == nullptr) {
        Log("scanner: GetModuleHandleW(nullptr) failed");
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

    const auto module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (module == nullptr) {
        Log("scanner: GetModuleHandleW(nullptr) failed");
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
        return {rdx_outcome.address, PlayerPointerMarkerRegister::Rdx};
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
        return {rsi_outcome.address, PlayerPointerMarkerRegister::Rsi};
    }

    if (rdx_outcome.status == ScanStatus::Ambiguous || rsi_outcome.status == ScanStatus::Ambiguous) {
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
    // v1.05.01: Hardcoded RVA (scanner was failing despite pattern existing)
    // Pattern "41 0F 11 45 00 48 8B BB F8 00 00 00" verified at memory RVA 0x0381859C
    // (this corresponds to file offset 0x0381799C; PE section maps file+0xC00 -> RVA)
    constexpr uintptr_t kPositionHeightRVA_1_05_01 = 0x0381859C;
    
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uintptr_t target = base + kPositionHeightRVA_1_05_01;
    
    // Verify expected bytes at hook location: 41 0F 11 45 00 (movups [r13+00],xmm0)
    static constexpr std::array<std::uint8_t, 5> kExpectedBytes = {
        0x41, 0x0F, 0x11, 0x45, 0x00
    };
    
    if (ExpectBytes(target, kExpectedBytes)) {
        Log("scanner: position-height found at hardcoded RVA 0x%08X (v1.05.01)", kPositionHeightRVA_1_05_01);
        return target;
    }
    
    Log("scanner: position-height RVA mismatch - expected bytes not found");
    return 0;
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
    // v1.05.01: Hardcoded RVA
    // Pattern "41 89 1C 24 E9" verified at memory RVA 0x01C9CDB9
    // (file offset 0x01C9C1B9; PE section maps file+0xC00 -> RVA)
    constexpr uintptr_t kDragonVillageRVA_1_05_01 = 0x01C9CDB9;
    
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uintptr_t target = base + kDragonVillageRVA_1_05_01;
    
    // Verify expected bytes: 41 89 1C 24 (mov [r12],ebx)
    static constexpr std::array<std::uint8_t, 4> kExpectedBytes = {
        0x41, 0x89, 0x1C, 0x24
    };
    
    if (ExpectBytes(target, kExpectedBytes)) {
        Log("scanner: dragon-village-summon found at hardcoded RVA 0x%08X (v1.05.01)", kDragonVillageRVA_1_05_01);
        return target;
    }
    
    Log("scanner: dragon-village-summon RVA mismatch - expected bytes not found");
    return 0;
}

uintptr_t ScanForDragonFlyingRestrictWrite() {
    // v1.05.01: Hardcoded RVA
    // Pattern "41 88 47 04 41 89 3F" verified at memory RVA 0x01E21E4B
    // (file offset 0x01E2124B; PE section maps file+0xC00 -> RVA)
    constexpr uintptr_t kDragonFlyingRVA_1_05_01 = 0x01E21E4B;
    
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uintptr_t target = base + kDragonFlyingRVA_1_05_01;
    
    // Verify expected bytes: 41 88 47 04 41 89 3F (mov [r15+4],al; mov [r15],edi)
    static constexpr std::array<std::uint8_t, 7> kExpectedBytes = {
        0x41, 0x88, 0x47, 0x04, 0x41, 0x89, 0x3F
    };
    
    if (ExpectBytes(target, kExpectedBytes)) {
        Log("scanner: dragon-flying-restrict found at hardcoded RVA 0x%08X (v1.05.01)", kDragonFlyingRVA_1_05_01);
        return target;
    }
    
    Log("scanner: dragon-flying-restrict RVA mismatch - expected bytes not found");
    return 0;
}

uintptr_t ScanForDragonRoofRestrictTest() {
    // v1.05.01: Hardcoded RVA
    // Pattern "85 DB 74 59 48 8B 05" verified at memory RVA 0x002E2F0B
    // (file offset 0x002E230B; PE section maps file+0xC00 -> RVA)
    // Note: The "test ebx,ebx" instruction starts here; the surrounding context
    // begins one byte earlier with "04 85 DB ..." but the hook lands on the test.
    constexpr uintptr_t kDragonRoofRVA_1_05_01 = 0x002E2F0B;
    
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uintptr_t target = base + kDragonRoofRVA_1_05_01;
    
    // Verify expected bytes: 85 DB 74 59 (test ebx,ebx; je +0x59)
    static constexpr std::array<std::uint8_t, 4> kExpectedBytes = {
        0x85, 0xDB, 0x74, 0x59
    };
    
    if (ExpectBytes(target, kExpectedBytes)) {
        Log("scanner: dragon-roof-restrict found at hardcoded RVA 0x%08X (v1.05.01)", kDragonRoofRVA_1_05_01);
        return target;
    }
    
    Log("scanner: dragon-roof-restrict RVA mismatch - expected bytes not found");
    return 0;
}

uintptr_t ScanForItemGainAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {"primary", "49 01 4C 38 10", 0},
    };

    // Known item-loss opcode kept for future work:
    //   49 29 4C 07 10    sub [r15+rax+10], rcx
    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: item-gain found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: item-gain found 0 matches");
        return 0;
    }

    return outcome.address;
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
    // v1.05.01: AIFunction_VaryFriendly virtual dispatch callsite after the
    // game has moved the friendly data record into r9.
    constexpr uintptr_t kAffinityVaryFriendlyRVA_1_05_01 = 0x0F995548;

    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uintptr_t target = base + kAffinityVaryFriendlyRVA_1_05_01;

    static constexpr std::array<std::uint8_t, 7> kExpectedBytes = {
        0x41, 0xFF, 0xD2, 0x90, 0x48, 0x8D, 0x1D,
    };

    if (ExpectBytes(target, kExpectedBytes)) {
        Log("scanner: affinity-vary-friendly callsite found at hardcoded RVA 0x%08X (v1.05.01)",
            kAffinityVaryFriendlyRVA_1_05_01);
        return target;
    }

    Log("scanner: affinity-vary-friendly callsite RVA mismatch - expected bytes not found");
    return 0;
}

uintptr_t ScanForAffinityVaryFriendlyWithLogout() {
    // v1.05.01: AIFunction_VaryFriendlyWithLogout virtual dispatch callsite
    // after the game has moved the friendly data record into r8.
    constexpr uintptr_t kAffinityVaryFriendlyWithLogoutRVA_1_05_01 = 0x01319B8B;

    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uintptr_t target = base + kAffinityVaryFriendlyWithLogoutRVA_1_05_01;

    static constexpr std::array<std::uint8_t, 7> kExpectedBytes = {
        0x41, 0xFF, 0xD2, 0x90, 0x48, 0x8D, 0x1D,
    };

    if (ExpectBytes(target, kExpectedBytes)) {
        Log("scanner: affinity-vary-friendly-with-logout callsite found at hardcoded RVA 0x%08X (v1.05.01)",
            kAffinityVaryFriendlyWithLogoutRVA_1_05_01);
        return target;
    }

    Log("scanner: affinity-vary-friendly-with-logout callsite RVA mismatch - expected bytes not found");
    return 0;
}

uintptr_t ScanForAffinityPetDiagnosticReloc() {
    // v1.05.01 candidate callsite near pet/animal friendly actions. This is
    // diagnostic-only; do not write through this path until logs prove the
    // active delta field.
    constexpr uintptr_t kAffinityPetDiagnosticRelocRVA_1_05_01 = 0x012FBD75;

    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uintptr_t target = base + kAffinityPetDiagnosticRelocRVA_1_05_01;

    static constexpr std::array<std::uint8_t, 7> kExpectedBytes = {
        0x41, 0xFF, 0xD2, 0x90, 0x48, 0x8D, 0x1D,
    };

    if (ExpectBytes(target, kExpectedBytes)) {
        Log("scanner: affinity-petdiag-reloc callsite found at hardcoded RVA 0x%08X (v1.05.01)",
            kAffinityPetDiagnosticRelocRVA_1_05_01);
        return target;
    }

    Log("scanner: affinity-petdiag-reloc callsite RVA mismatch - expected bytes not found");
    return 0;
}

uintptr_t ScanForAffinityPetDiagnosticRsrc() {
    // v1.05.01 candidate callsite near petting/taming friendly actions. This
    // reads r9/r13 context only in the ASI diagnostic callback.
    constexpr uintptr_t kAffinityPetDiagnosticRsrcRVA_1_05_01 = 0x0C7EBCBE;

    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uintptr_t target = base + kAffinityPetDiagnosticRsrcRVA_1_05_01;

    static constexpr std::array<std::uint8_t, 7> kExpectedBytes = {
        0x41, 0xFF, 0xD2, 0x90, 0x48, 0x8D, 0x1D,
    };

    if (ExpectBytes(target, kExpectedBytes)) {
        Log("scanner: affinity-petdiag-rsrc callsite found at hardcoded RVA 0x%08X (v1.05.01)",
            kAffinityPetDiagnosticRsrcRVA_1_05_01);
        return target;
    }

    Log("scanner: affinity-petdiag-rsrc callsite RVA mismatch - expected bytes not found");
    return 0;
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
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "48 89 ?? 48 89 ?? E8 ?? ?? ?? ?? 84 C0 75 ?? 48 8B 5C 24 ?? 48 8B 74 24 ?? 48 83 C4 ?? 5F C3",
            6,
        },
        {
            "fallback",
            "49 89 D8 48 89 FA 48 89 C1 48 89 C6 E8 ?? ?? ?? ?? 84 C0 75 ?? 48 8B 5C 24 30 48 8B 74 24 40 48 83 C4 20 5F C3",
            12,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: spirit-delta found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: spirit-delta found 0 matches");
        return 0;
    }

    return outcome.address;
}

uintptr_t ScanForStatsAccess() {
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

uintptr_t ScanForStatWriteAccess() {
    static constexpr PatternDefinition kPatterns[] = {
        {
            "primary",
            "48 2B 47 18 48 39 5F 18 48 0F 4F C2 48 89 47 20 48 FF 47 48 48 89 5F 08 48 8B 5C 24 48 48 89 77 38 66 89 6F 50",
            20,
        },
        {
            "fallback",
            "48 FF 47 48 48 89 5F 08 48 8B 5C 24 48 48 89 77 38",
            4,
        },
    };

    const auto outcome = ScanInSections(kPatterns, sizeof(kPatterns) / sizeof(kPatterns[0]));
    if (outcome.status == ScanStatus::Ambiguous) {
        Log("scanner: stat-write found multiple matches, install failed");
        return 0;
    }

    if (outcome.status != ScanStatus::Unique) {
        Log("scanner: stat-write found 0 matches");
        return 0;
    }

    return outcome.address;
}
