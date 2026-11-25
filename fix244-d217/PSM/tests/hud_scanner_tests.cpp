#include "hud_scanner.h"
#include "scanner.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

// Standalone target links scanner.cpp with PSM_SCANNER_TESTING, not logger.cpp.
void Log(const char*, ...) {}

namespace {
int failures = 0;
int checks = 0;
void Check(bool condition, const char* name) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL: %s\n", name); }
}
bool Closed(const psm::hud::ScanResult& r) {
    return !r && !r.visibility_after_call && !r.gauge_before_current_load &&
           !r.cost_after_commit && !r.gauge_update && !r.commit_target;
}
std::vector<std::uint8_t> MapFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(input)), {});
    if (file.size() < sizeof(IMAGE_DOS_HEADER)) return {};
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, file.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
        static_cast<size_t>(dos.e_lfanew) > file.size() - sizeof(IMAGE_NT_HEADERS64)) return {};
    IMAGE_NT_HEADERS64 nt{};
    std::memcpy(&nt, file.data() + dos.e_lfanew, sizeof(nt));
    const auto& opt = nt.OptionalHeader;
    if (nt.Signature != IMAGE_NT_SIGNATURE || opt.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        opt.SizeOfImage > 512U * 1024U * 1024U || opt.SizeOfHeaders > file.size() ||
        opt.SizeOfHeaders > opt.SizeOfImage) return {};
    std::vector<std::uint8_t> image(opt.SizeOfImage);
    std::memcpy(image.data(), file.data(), opt.SizeOfHeaders);
    const size_t table = static_cast<size_t>(dos.e_lfanew) + 24 + nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        const size_t at = table + i * sizeof(IMAGE_SECTION_HEADER);
        if (at > file.size() || sizeof(IMAGE_SECTION_HEADER) > file.size() - at) return {};
        IMAGE_SECTION_HEADER section{};
        std::memcpy(&section, file.data() + at, sizeof(section));
        if (section.PointerToRawData > file.size() || section.SizeOfRawData > file.size() - section.PointerToRawData ||
            section.VirtualAddress > image.size() || section.SizeOfRawData > image.size() - section.VirtualAddress) return {};
        std::memcpy(image.data() + section.VirtualAddress, file.data() + section.PointerToRawData, section.SizeOfRawData);
    }
    return image;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::puts("usage: hud-scanner-tests <1.0.0.2760 executable>"); return 2; }
    auto image = MapFile(argv[1]);
    if (image.empty()) { std::puts("invalid input PE"); return 2; }
    SetScannerTestModule(image.data());
    const auto base = reinterpret_cast<std::uintptr_t>(image.data());
    const auto native_cost = ScanForNativeResourceCostAccess();
    Check(native_cost == base + 0xC4E3EAB, "independent native cost anchor");
    const auto scan = [&] { return psm::hud::ScanContracts(image.data(), image.size(), native_cost); };
    const auto good = scan();
    Check(static_cast<bool>(good), "current PE has three linked contracts");
    Check(good.visibility_after_call == base + 0xFAF05C, "visibility site is after native boolean call");
    Check(good.gauge_before_current_load == base + 0xDE0E29, "gauge site precedes R10 cache load");
    Check(good.cost_after_commit == base + 0xC4E3F09, "cost site follows actual commit return");
    Check(good.gauge_update == base + 0xDE0330 && good.commit_target == base + 0xC4E6A80,
          "linked gauge function and resolved commit writer");
    if (!good) {
        std::printf("baseline rejected status=%d stage=%d\n", static_cast<int>(good.status), static_cast<int>(good.stage));
        return 1;
    }
    Check(Closed(psm::hud::ScanContracts(nullptr, 0, 0)), "null image rejected");
    Check(Closed(psm::hud::ScanContracts(image.data(), 32, native_cost)), "truncated headers rejected");
    Check(Closed(psm::hud::ScanContracts(image.data(), image.size() - 1, native_cost)), "truncated mapped image rejected");
    Check(Closed(psm::hud::ScanContracts(image.data(), image.size(), 0)), "missing cost anchor has no RVA fallback");
    Check(Closed(psm::hud::ScanContracts(image.data(), image.size(), native_cost + 1)), "misaligned cost anchor rejected");

    struct Mutation { size_t rva; const char* name; };
    const Mutation mutations[] = {
        {0, "bad DOS signature"}, {0xFAEF52, "actor null guard"},
        {0xFAEF63, "dead-state member"}, {0xFAEF6E, "mount R8 guard"},
        {0xFAEF76, "mount state member"}, {0xFAEF9F, "controller gauge +1A8"},
        {0xFAEFA4, "refresh link must name discovered gauge function"},
        {0xFAEFB7, "RBX context replacement contract"}, {0xFAEFFF, "action descriptor +25"},
        {0xFAF051, "native deficit branch"}, {0xFAF058, "native boolean call target"},
        {0xFAF05D, "post-call AL test"}, {0xFAF05F, "true AL branches to timer reset"},
        {0xFAF085, "hide-duration +308"}, {0xFAF08D, "timer +30C"},
        {0xFAF0B7, "timer reset destination"},
        {0xDE0397, "gauge role byte +255"}, {0xDE039B, "role table accepts exactly indices 0..2"},
        {0xDE03E5, "native role table index displacement"},
        {0xDE0442, "actor status chain +68"}, {0xDE0446, "actor status chain +20"},
        {0xDE0455, "actor status owner +18"}, {0xDE04FC, "role path joins final cache comparison"},
        {0xDE0DF1, "old-current stack slot"}, {0xDE0E2C, "cache load +230"},
        {0xDE0E38, "cached maximum +238"}, {0xDE0EB5, "changed-value vtable callback +518"},
        {0xC4E3EAF, "evaluated pre-value stack +58"}, {0xC4E3EB7, "RBX signed expense normalization"},
        {0xC4E3E8B, "cost RSI entry capture"}, {0xC4E3EA5, "getter output stack slot"},
        {0xC4E3EA7, "evaluated getter call target"},
        {0xC4E3F05, "commit-call destination"}, {0x171E630, "commit thunk opcode"},
        {0x171E632, "commit thunk destination"}, {0xC4E6A9A, "writer entry ownership"},
        {0xC4E6BB8, "writer stores post-value +08"}, {0xC4E3F09, "post-commit instruction boundary"},
    };
    for (const auto& mutation : mutations) {
        image[mutation.rva] ^= 1;
        Check(Closed(scan()), mutation.name);
        image[mutation.rva] ^= 1;
    }

    const size_t duplicate = 0xFAF400;
    std::vector<std::uint8_t> saved(image.begin() + duplicate, image.begin() + duplicate + 0x1A7);
    std::memcpy(image.data() + duplicate, image.data() + 0xFAEF30, saved.size());
    const auto ambiguous = scan();
    Check(Closed(ambiguous) && ambiguous.status == psm::hud::ScanStatus::Ambiguous,
          "duplicate visibility code fails closed even if links are invalid");
    std::memcpy(image.data() + duplicate, saved.data(), saved.size());

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size = 0;
    Check(Closed(scan()), "missing function boundaries rejected");
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION] = directory;
    auto* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (0xFAEF30 >= sections[i].VirtualAddress &&
            0xFAEF30 - sections[i].VirtualAddress < sections[i].Misc.VirtualSize) {
            const auto flags = sections[i].Characteristics;
            sections[i].Characteristics &= ~IMAGE_SCN_MEM_EXECUTE;
            Check(Closed(scan()), "nonexecutable visibility bytes rejected");
            sections[i].Characteristics = flags;
            break;
        }
    }
    Check(static_cast<bool>(scan()), "all mutation fixtures restored");
    Check(psm::hud::ValidateHudModifierListContract(image.data(), image.size()),
          "optional list mutation, AB00 ordering and contribution contracts");
    Check(!psm::hud::ValidateHudModifierListContract(nullptr, 0), "optional list rejects null image");
    const Mutation optional_mutations[] = {
        {0xC4DCF7C, "list pointer +D8"}, {0xC4DCF8C, "list count +E0"},
        {0xC4DCFFD, "list capacity +E4"}, {0xC4DCFD7, "list record stride +30"},
        {0xC4DCFB4, "group stat comparison"}, {0xC4DCFC1, "group selector +10"},
        {0xC4DD1C0, "removal preserves count contract"},
        {0x171A233, "first caller mutator link"}, {0x171A253, "first caller AB00 link"},
        {0x171A3A2, "second caller mutator link"}, {0x171A3C2, "second caller AB00 link"},
        {0x171D960, "mutation trampoline"}, {0x171D7A5, "AB00 R14 owner capture"},
        {0x171D81A, "AB00 aggregation follows observation site"},
        {0x171EA40, "contribution trampoline"}, {0xC4DCFCD, "first contribution call"},
        {0xC4DD23F, "second contribution call"}, {0xC4EB738, "contribution penalty +18"},
        {0xC4EB767, "contribution base +08"}, {0xC4EB7B7, "contribution boost +28"},
        {0xC4EB750, "million-unit penalty denominator"},
        {0xC4EB784, "contribution arithmetic call target"},
    };
    for (const auto& mutation : optional_mutations) {
        image[mutation.rva] ^= 1;
        Check(!psm::hud::ValidateHudModifierListContract(image.data(), image.size()), mutation.name);
        image[mutation.rva] ^= 1;
    }
    SetScannerTestModule(nullptr);
    std::printf("HUD scanner: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
