#include "hud_scanner.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
int checks = 0;
int failures = 0;
void Check(bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL: %s\n", name); }
}
bool Closed(const psm::hud::MountedResourceScanResult& r) {
    return !r && !r.mount_actor_capture && !r.query_after_lookup && !r.root_update &&
        !r.ride_refresh && !r.gauge_update && !r.current_query && !r.entry_lookup &&
        !r.current_evaluator;
}
std::vector<std::uint8_t> MapFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(input)), {});
    if (file.size() < sizeof(IMAGE_DOS_HEADER)) return {};
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, file.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        static_cast<size_t>(dos.e_lfanew) > file.size() ||
        sizeof(IMAGE_NT_HEADERS64) > file.size() - static_cast<size_t>(dos.e_lfanew)) return {};
    IMAGE_NT_HEADERS64 nt{};
    std::memcpy(&nt, file.data() + dos.e_lfanew, sizeof(nt));
    const auto& opt = nt.OptionalHeader;
    if (nt.Signature != IMAGE_NT_SIGNATURE || opt.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        !opt.SizeOfImage || opt.SizeOfImage > 512U * 1024U * 1024U ||
        opt.SizeOfHeaders > file.size() || opt.SizeOfHeaders > opt.SizeOfImage) return {};
    std::vector<std::uint8_t> image(opt.SizeOfImage);
    std::memcpy(image.data(), file.data(), opt.SizeOfHeaders);
    const size_t table = static_cast<size_t>(dos.e_lfanew) + 24 + nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        const size_t at = table + i * sizeof(IMAGE_SECTION_HEADER);
        if (at > file.size() || sizeof(IMAGE_SECTION_HEADER) > file.size() - at) return {};
        IMAGE_SECTION_HEADER section{};
        std::memcpy(&section, file.data() + at, sizeof(section));
        if (section.PointerToRawData > file.size() ||
            section.SizeOfRawData > file.size() - section.PointerToRawData ||
            section.VirtualAddress > image.size() ||
            section.SizeOfRawData > image.size() - section.VirtualAddress) return {};
        std::memcpy(image.data() + section.VirtualAddress,
                    file.data() + section.PointerToRawData, section.SizeOfRawData);
    }
    return image;
}
}  // namespace

// Standalone target needs hud_scanner.cpp only; it never loads or executes the PE.
int main(int argc, char** argv) {
    if (argc != 2) { std::puts("usage: mount-scanner-tests <1.0.0.2760 executable>"); return 2; }
    auto image = MapFile(argv[1]);
    if (image.empty()) { std::puts("invalid input PE"); return 2; }
    const auto base = reinterpret_cast<std::uintptr_t>(image.data());
    const auto scan = [&] { return psm::hud::ScanMountedResourceContracts(image.data(), image.size()); };
    const auto good = scan();
    Check(static_cast<bool>(good), "current PE has independent mounted resource contracts");
    if (!good) {
        std::printf("baseline rejected status=%d stage=%d\n", int(good.status), int(good.stage));
        return 1;
    }
    Check(good.mount_actor_capture == base + 0xFAA357, "capture precedes wrapper release: RSI root, R15 player, R13 mount");
    Check(good.query_after_lookup == base + 0x1717C87, "query capture: R14 owner, BX selector, RAX native entry");
    Check(good.root_update == base + 0xFAA150 && good.ride_refresh == base + 0xFAE6B0,
          "native root and active player ride refresh");
    Check(good.gauge_update == base + 0xDE0330 && good.current_query == base + 0x1717B20 &&
          good.entry_lookup == base + 0x1718140 && good.current_evaluator == base + 0x171E4C0,
          "gauge query, canonical slot lookup and evaluator are linked");
    Check(Closed(psm::hud::ScanMountedResourceContracts(nullptr, 0)), "null image");
    Check(Closed(psm::hud::ScanMountedResourceContracts(image.data(), 32)), "truncated headers");
    Check(Closed(psm::hud::ScanMountedResourceContracts(image.data(), image.size() - 1)), "truncated mapping");

    struct Mutation { size_t rva; const char* name; };
    const Mutation mutations[] = {
        {0xFAA17E, "root register must remain RSI"},
        {0xFAA1A6, "root player lookup link"}, {0xFAA1B1, "player wrapper validity slot"},
        {0xFAA1B6, "player capture stack slot"}, {0xFAA1CF, "R13 starts empty"},
        {0xFAA329, "player missing path cannot enter mount capture"},
        {0xFAA331, "root ride identity member +360"}, {0xFAA344, "actor manager lookup link"},
        {0xFAA34B, "mount wrapper valid-byte guard"}, {0xFAA34F, "mount invalid branch"},
        {0xFAA356, "R13 mount stack slot"}, {0xFAA361, "mount actor identity +60"},
        {0xFAA3E5, "gauge actor identity +60"}, {0xFAA3FB, "root mount gauge +1B8"},
        {0xFAA400, "direct root gauge update link"},
        {0xFAAA73, "horse visibility R8 must be R13"}, {0xFAAA76, "horse visibility RDX must be R15"},
        {0xFAAA7B, "horse visibility call link"}, {0xFAF100, "horse actor saved in RSI"},
        {0xFAF12A, "horse null guard"}, {0xFAF150, "gauge actor argument is RSI"},
        {0xFAF154, "horse gauge field +1B8"}, {0xFAF159, "horse refresh links same gauge"},
        {0xFA8D4E, "native horse selector token reference"},
        {0xFA8D55, "selector binds root +1B8"}, {0xFA8D7F, "bound gauge type check"},
        {0x1655E9, "selector registration string reference"},
        {0x55C39D8, "native selector name is exact"},
        {0xFAE6CE, "ride writer uses same active player lookup"},
        {0xFAE6D9, "failed player lookup skips stale cache write"},
        {0xFAE709, "ride chain actor +68"}, {0xFAE70D, "ride component +48"},
        {0xFAE716, "ride state +18"}, {0xFAE71E, "ridden actor ID +10"},
        {0xFAE721, "ride writer root +360"},
        {0x5DD501, "player wrapper manager resolver link"},
        {0x83D106, "player manager +58"}, {0x83D1D2, "equip actor +D8"},
        {0x8373E8, "actor ID zero guard"}, {0x8374DD, "manager native ID map call"},
        {0x83751B, "manager wrapper actor field"}, {0x83751F, "manager wrapper valid field"},
        {0xDE0442, "gauge actor components +68"}, {0xDE0446, "gauge status component +20"},
        {0xDE0455, "gauge native owner +18"}, {0xDE0457, "gauge canonical query call"},
        {0x1717B3D, "query owner saved in R14"}, {0x1717B49, "query selector saved in BX"},
        {0x1717C6F, "native selector validity branch"}, {0x1717C81, "lookup RCX owner"},
        {0x1717C83, "canonical lookup call target"}, {0x1717C87, "observer instruction boundary"},
        {0x1717C94, "evaluator consumes returned entry"}, {0x1717C96, "evaluator link"},
        {0x1718154, "lookup selector is DX"}, {0x1718169, "owner profile key +5B8"},
        {0x1718185, "native selector metadata resolver"}, {0x171818B, "native selector metadata +14"},
        {0x171818F, "profile slot map +58"}, {0x1718195, "invalid slot sentinel"},
        {0x17181B2, "entry stride multiplier nine"}, {0x17181B6, "entry stride shift four"},
        {0x17181BA, "owner entry array +58"}, {0x171E4DB, "native evaluator entry register"},
    };
    for (const auto& mutation : mutations) {
        image[mutation.rva] ^= 1;
        Check(Closed(scan()), mutation.name);
        image[mutation.rva] ^= 1;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    const auto saved_directory = directory;
    directory.Size = 0;
    Check(Closed(scan()), "missing pdata has no known-RVA fallback");
    directory = saved_directory;
    --directory.Size;
    Check(Closed(scan()), "partial pdata record");
    directory = saved_directory;
    auto* entries = reinterpret_cast<RUNTIME_FUNCTION*>(image.data() + directory.VirtualAddress);
    const size_t count = directory.Size / sizeof(RUNTIME_FUNCTION);
    size_t root_index = count;
    for (size_t i = 0; i < count; ++i) if (entries[i].BeginAddress == 0xFAA150) root_index = i;
    Check(root_index < count, "root has pdata extent");
    if (root_index < count) {
        const auto root_entry = entries[root_index];
        entries[root_index].EndAddress = 0xFAA357;
        Check(Closed(scan()), "capture outside truncated root extent");
        entries[root_index] = root_entry;
        const auto saved_last = entries[count - 1];
        entries[count - 1] = root_entry;
        Check(Closed(scan()), "overlapping duplicate pdata ownership");
        entries[count - 1] = saved_last;

        for (const DWORD interior : {0xFAA357U, 0x1717C87U}) {
            entries[count - 1] = root_entry;
            entries[count - 1].BeginAddress = interior;
            entries[count - 1].EndAddress = interior + 7;
            Check(Closed(scan()), "interior pdata ownership overlapping observation site is rejected");
            entries[count - 1] = saved_last;
        }

        const size_t duplicate = 0xFAF400;
        const size_t prefix_size = 0x21E;
        const std::vector<std::uint8_t> saved(image.begin() + duplicate, image.begin() + duplicate + prefix_size);
        std::memcpy(image.data() + duplicate, image.data() + 0xFAA150, prefix_size);
        entries[count - 1] = root_entry;
        entries[count - 1].BeginAddress = static_cast<DWORD>(duplicate);
        entries[count - 1].EndAddress = static_cast<DWORD>(duplicate + 0xC8D);
        const auto ambiguous = scan();
        Check(Closed(ambiguous) && ambiguous.status == psm::hud::ScanStatus::Ambiguous,
              "duplicate root anchor fails closed before trusting its links");
        entries[count - 1] = saved_last;
        std::memcpy(image.data() + duplicate, saved.data(), saved.size());
    }
    auto* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (0xFAA150 >= sections[i].VirtualAddress &&
            0xFAA150 - sections[i].VirtualAddress < sections[i].Misc.VirtualSize) {
            const auto flags = sections[i].Characteristics;
            sections[i].Characteristics &= ~IMAGE_SCN_MEM_EXECUTE;
            Check(Closed(scan()), "non-executable contract bytes");
            sections[i].Characteristics = flags;
            break;
        }
    }
    Check(static_cast<bool>(scan()), "all fixtures restored");
    std::printf("Mounted scanner: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
