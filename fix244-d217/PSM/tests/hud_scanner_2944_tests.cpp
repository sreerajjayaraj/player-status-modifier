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
    const std::uint8_t legacy_kModifierMutator[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
        0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B, 0xB1, 0xD8, 0x00, 0x00, 0x00,
        0x31, 0xDB, 0x4C, 0x89, 0xCE, 0x45, 0x0F, 0xB6, 0xE8, 0x44, 0x8B, 0x89, 0xE0, 0x00, 0x00, 0x00,
        0x49, 0x89, 0xCF, 0x89, 0xDF, 0x44, 0x89, 0xC8, 0x4C, 0x89, 0xF5, 0x4F, 0x8D, 0x24, 0x49, 0x49,
        0xC1, 0xE4, 0x04, 0x4D, 0x01, 0xF4, 0x4D, 0x39, 0xE6, 0x74, 0x46, 0x0F, 0x1F, 0x44, 0x00, 0x00,
        0x0F, 0xB7, 0x06, 0x66, 0x41, 0x39, 0x06, 0x75, 0x1B, 0x0F, 0xB7, 0x46, 0x10, 0x66, 0x41, 0x39,
        0x46, 0x10, 0x75, 0x10, 0x48, 0x8D, 0x54, 0x24, 0x60, 0x4C, 0x89, 0xF1, 0xE8, 0x00, 0x00, 0x00,
        0x00, 0x48, 0x03, 0x38, 0x49, 0x83, 0xC6, 0x30, 0x4D, 0x39, 0xE6, 0x75, 0xD3, 0x4D, 0x8B, 0xB7,
        0xD8, 0x00, 0x00, 0x00, 0x41, 0x8B, 0x87, 0xE0, 0x00, 0x00, 0x00, 0x4C, 0x89, 0xF5, 0x41, 0x89,
        0xC1, 0x45, 0x84, 0xED, 0x0F, 0x84, 0x37, 0x01, 0x00, 0x00, 0x41, 0x8B, 0x8F, 0xE4, 0x00, 0x00,
        0x00, 0x39, 0xC1, 0x0F, 0x87, 0xE8, 0x00, 0x00, 0x00, 0x41, 0xBD, 0x01, 0x00, 0x00, 0x00, 0x44,
        0x8D, 0xB4, 0x09, 0x01, 0x00, 0x00, 0x00, 0x41, 0x01, 0xCE, 0x41, 0xD1, 0xEE, 0x45, 0x39, 0xEE,
        0x45, 0x0F, 0x42, 0xF5, 0x44, 0x39, 0xF0, 0x44, 0x0F, 0x47, 0xF0, 0x41, 0x39, 0xCE, 0x0F, 0x86,
        0xBD, 0x00, 0x00, 0x00, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x43, 0x8D, 0x0C,
        0x76, 0x41, 0xBC, 0xFD, 0x01, 0x00, 0x00, 0xBA, 0x08, 0x00, 0x00, 0x00, 0xC1, 0xE1, 0x04, 0x4C,
        0x03, 0x20, 0x41, 0x38, 0x1C, 0x24, 0x74, 0x07, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x05, 0xE8,
        0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0xC5, 0x48, 0x85, 0xC0, 0x75, 0x19, 0xE8, 0x00, 0x00, 0x00,
        0x00, 0x45, 0x31, 0xC9, 0x45, 0x31, 0xC0, 0x44, 0x89, 0xEA, 0xB9, 0x02, 0x00, 0x00, 0xA0, 0xFF,
        0x15, 0x00, 0x00, 0x00, 0x00, 0x89, 0xDA, 0x41, 0x39, 0x9F, 0xE0, 0x00, 0x00, 0x00, 0x76, 0x32,
        0x89, 0xD0, 0xFF, 0xC2, 0x48, 0x8D, 0x0C, 0x40, 0x49, 0x8B, 0x87, 0xD8, 0x00, 0x00, 0x00, 0x48,
        0x01, 0xC9, 0xC5, 0xFC, 0x10, 0x04, 0xC8, 0xC5, 0xFC, 0x11, 0x44, 0xCD, 0x00, 0xC5, 0xF8, 0x10,
        0x4C, 0xC8, 0x20, 0xC5, 0xF8, 0x11, 0x4C, 0xCD, 0x20, 0x41, 0x3B, 0x97, 0xE0, 0x00, 0x00, 0x00,
        0x72, 0xCE, 0x49, 0x8B, 0x8F, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x15, 0xC5, 0xF8,
        0x77, 0x41, 0x38, 0x1C, 0x24, 0x74, 0x07, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x05, 0xE8, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x89, 0xAF, 0xD8, 0x00, 0x00, 0x00, 0x45, 0x89, 0xB7, 0xE4, 0x00, 0x00,
        0x00, 0x41, 0x8B, 0x87, 0xE0, 0x00, 0x00, 0x00, 0xC5, 0xFC, 0x10, 0x06, 0x48, 0x8D, 0x0C, 0x40,
        0x48, 0x01, 0xC9, 0xC5, 0xFC, 0x11, 0x44, 0xCD, 0x00, 0xC5, 0xF8, 0x10, 0x4E, 0x20, 0xC5, 0xF8,
        0x11, 0x4C, 0xCD, 0x20, 0x29, 0x2D, 0x00, 0x00, 0x00, 0x00, 0xBD, 0x01, 0x00, 0xB4, 0x74, 0x03,
        0x2D, 0x00, 0x00, 0x00, 0x00, 0x41, 0x01, 0xAF, 0xE0, 0x00, 0x00, 0x00, 0xE9, 0x7E, 0x00, 0x00,
        0x00, 0x89, 0xD9, 0x45, 0x85, 0xC9, 0x0F, 0x84, 0x73, 0x00, 0x00, 0x00, 0x0F, 0xB7, 0x16, 0x66,
        0x90, 0x89, 0xC8, 0x4C, 0x8D, 0x04, 0x40, 0x4D, 0x01, 0xC0, 0x66, 0x43, 0x39, 0x14, 0xC6, 0x75,
        0x17, 0x48, 0x8B, 0x46, 0x08, 0x4B, 0x39, 0x44, 0xC6, 0x08, 0x75, 0x0C, 0x0F, 0xB7, 0x46, 0x10,
        0x66, 0x43, 0x39, 0x44, 0xC6, 0x10, 0x74, 0x09, 0xFF, 0xC1, 0x44, 0x39, 0xC9, 0x72, 0xD2, 0xEB,
        0x3E, 0x41, 0x8D, 0x41, 0xFF, 0x39, 0xC1, 0x73, 0x2F, 0x41, 0x8B, 0x87, 0xE0, 0x00, 0x00, 0x00,
        0x49, 0x8B, 0x97, 0xD8, 0x00, 0x00, 0x00, 0xFF, 0xC8, 0x48, 0x8D, 0x0C, 0x40, 0x48, 0x01, 0xC9,
        0xC5, 0xFC, 0x10, 0x04, 0xCA, 0xC4, 0xA1, 0x7C, 0x11, 0x04, 0xC2, 0xC5, 0xF8, 0x10, 0x4C, 0xCA,
        0x20, 0xC4, 0xA1, 0x78, 0x11, 0x4C, 0xC2, 0x20, 0x41, 0xFF, 0x8F, 0xE0, 0x00, 0x00, 0x00, 0xB8,
        0xFF, 0xFF, 0x00, 0x00, 0x66, 0x39, 0x46, 0x10, 0x74, 0x41, 0x49, 0x8B, 0x8F, 0xD8, 0x00, 0x00,
        0x00, 0x41, 0x8B, 0x87, 0xE0, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x14, 0x40, 0x48, 0xC1, 0xE2, 0x04,
        0x48, 0x01, 0xCA, 0x48, 0x39, 0xD1, 0x74, 0x23, 0x0F, 0xB7, 0x01, 0x66, 0x39, 0x06, 0x75, 0x12,
        0x0F, 0xB7, 0x41, 0x10, 0x66, 0x39, 0x46, 0x10, 0x75, 0x08, 0x48, 0x8B, 0x46, 0x18, 0x48, 0x89,
        0x41, 0x18, 0x48, 0x83, 0xC1, 0x30, 0x48, 0x39, 0xD1, 0x75, 0xDD, 0x49, 0x8B, 0xAF, 0xD8, 0x00,
        0x00, 0x00, 0x41, 0x8B, 0x87, 0xE0, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0x34, 0x40, 0x49, 0xC1, 0xE6,
        0x04, 0x49, 0x01, 0xEE, 0x4C, 0x39, 0xF5, 0x74, 0x36, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00,
        0x0F, 0xB7, 0x06, 0x66, 0x39, 0x45, 0x00, 0x75, 0x1D, 0x0F, 0xB7, 0x46, 0x10, 0x66, 0x39, 0x45,
        0x10, 0x75, 0x13, 0x48, 0x8D, 0x54, 0x24, 0x60, 0x48, 0x89, 0xE9, 0xC5, 0xF8, 0x77, 0xE8, 0x00,
        0x00, 0x00, 0x00, 0x48, 0x03, 0x18, 0x48, 0x83, 0xC5, 0x30, 0x4C, 0x39, 0xF5, 0x75, 0xD1, 0x48,
        0x8B, 0x44, 0x24, 0x68, 0x48, 0x29, 0xFB, 0x48, 0x89, 0x18, 0xC5, 0xF8, 0x77, 0x48, 0x8B, 0x5C,
        0x24, 0x70, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5F, 0x5E,
        0x5D, 0xC3,
    };
    const std::uint8_t legacy_kContribution[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55, 0x48, 0x89, 0xE5, 0x48, 0x83,
        0xEC, 0x50, 0x48, 0x89, 0xD3, 0x48, 0x8B, 0x51, 0x18, 0x48, 0x85, 0xD2, 0x74, 0x76, 0x31, 0xFF,
        0x48, 0x89, 0x7D, 0xD0, 0x89, 0x7D, 0xD8, 0x48, 0x8D, 0x45, 0xE0, 0x48, 0x89, 0x45, 0x10, 0xB8,
        0x40, 0x42, 0x0F, 0x00, 0x48, 0x29, 0xD0, 0x48, 0x89, 0x45, 0xE0, 0x89, 0x7D, 0xE8, 0x48, 0x8D,
        0x45, 0xF0, 0x48, 0x89, 0x45, 0x20, 0x48, 0x8B, 0x41, 0x08, 0x48, 0x89, 0x45, 0xF0, 0x89, 0x7D,
        0xF8, 0x4C, 0x8D, 0x45, 0xE0, 0x48, 0x8D, 0x55, 0xF0, 0x41, 0xB9, 0x02, 0x00, 0x00, 0x00, 0x48,
        0x8D, 0x4D, 0xD0, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xB8, 0x49, 0xF7, 0xD6, 0x4C, 0x2B, 0x05, 0x00,
        0x00, 0x00, 0x00, 0x39, 0x45, 0xD8, 0x74, 0x16, 0x48, 0x89, 0x3B, 0x48, 0x89, 0xD8, 0x48, 0x8B,
        0x5C, 0x24, 0x68, 0x48, 0x8B, 0x7C, 0x24, 0x78, 0x48, 0x83, 0xC4, 0x50, 0x5D, 0xC3, 0x48, 0x8B,
        0x45, 0xD0, 0xEB, 0x48, 0x48, 0x8B, 0x41, 0x28, 0x48, 0x85, 0xC0, 0x74, 0x3B, 0x31, 0xFF, 0x48,
        0x89, 0x7D, 0xD0, 0x89, 0x7D, 0xD8, 0x48, 0x8D, 0x55, 0xF0, 0x48, 0x89, 0x55, 0x10, 0x48, 0x05,
        0x40, 0x42, 0x0F, 0x00, 0x48, 0x89, 0x45, 0xF0, 0x89, 0x7D, 0xF8, 0x48, 0x8D, 0x45, 0xE0, 0x48,
        0x89, 0x45, 0x20, 0x48, 0x8B, 0x41, 0x08, 0x48, 0x89, 0x45, 0xE0, 0x89, 0x7D, 0xE8, 0x4C, 0x8D,
        0x45, 0xF0, 0x48, 0x8D, 0x55, 0xE0, 0xEB, 0x81, 0x48, 0x8B, 0x41, 0x08, 0x48, 0x89, 0x03, 0x48,
        0x89, 0xD8, 0x48, 0x8B, 0x5C, 0x24, 0x68, 0x48, 0x8B, 0x7C, 0x24, 0x78, 0x48, 0x83, 0xC4, 0x50,
        0x5D, 0xC3,
    };
// Synthetic contracts preserve required links and boundaries, so the only
// rejection reason in mismatched cases is the legacy/current pairing rule.
void CheckModifierProfileFixtures(std::vector<std::uint8_t>& image) {
    const size_t mutator = 0xCAE7E60, contribution = 0xCAF7F90;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    auto* functions = reinterpret_cast<RUNTIME_FUNCTION*>(image.data() + directory.VirtualAddress);
    RUNTIME_FUNCTION *mutator_entry = nullptr, *contribution_entry = nullptr;
    for (size_t i = 0; i < directory.Size / sizeof(RUNTIME_FUNCTION); ++i) {
        if (functions[i].BeginAddress == mutator) mutator_entry = &functions[i];
        if (functions[i].BeginAddress == contribution) contribution_entry = &functions[i];
    }
    Check(mutator_entry && contribution_entry, "profile fixture native boundaries exist");
    if (!mutator_entry || !contribution_entry) return;
    const auto mutator_end = mutator_entry->EndAddress;
    const auto contribution_end = contribution_entry->EndAddress;
    const std::vector<std::uint8_t> saved_mutator(image.begin() + mutator,
                                               image.begin() + mutator + sizeof(legacy_kModifierMutator));
    const std::vector<std::uint8_t> saved_contribution(image.begin() + contribution,
                                                    image.begin() + contribution + sizeof(legacy_kContribution));
    const auto scan = [&] { return psm::hud::ValidateHudModifierListContract(image.data(), image.size()); };
    const auto rel32 = [&](size_t call, size_t target) {
        const auto displacement = static_cast<std::int32_t>(target - (call + 5));
        std::memcpy(image.data() + call + 1, &displacement, sizeof(displacement));
    };
    const auto restore = [&] {
        std::memcpy(image.data() + mutator, saved_mutator.data(), saved_mutator.size());
        std::memcpy(image.data() + contribution, saved_contribution.data(), saved_contribution.size());
        mutator_entry->EndAddress = mutator_end;
        contribution_entry->EndAddress = contribution_end;
    };
    const auto install_legacy_mutator = [&] {
        std::memcpy(image.data() + mutator, legacy_kModifierMutator, sizeof(legacy_kModifierMutator));
        mutator_entry->EndAddress = static_cast<DWORD>(mutator + sizeof(legacy_kModifierMutator));
        rel32(mutator + 0x6C, 0x17B4640);
        rel32(mutator + 0x2DE, 0x17B4640);
    };
    const auto install_legacy_contribution = [&] {
        std::memcpy(image.data() + contribution, legacy_kContribution, sizeof(legacy_kContribution));
        contribution_entry->EndAddress = static_cast<DWORD>(contribution + sizeof(legacy_kContribution));
        rel32(contribution + 0x63, 0x910E30);
    };
    install_legacy_contribution();
    Check(!scan(), "current mutator with sole legacy contribution rejected");
    restore();
    install_legacy_mutator();
    Check(!scan(), "sole legacy mutator with current contribution rejected");
    install_legacy_contribution();
    Check(scan(), "synthetic matching legacy pair validates native links and boundaries");
    restore();
    Check(scan(), "matching current pair restored after profile fixtures");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) { std::puts("usage: hud-scanner-tests <1.0.0.2944 executable> [--profiles-only]"); return 2; }
    auto image = MapFile(argv[1]);
    if (image.empty()) { std::puts("invalid input PE"); return 2; }
    if (argc == 3 && std::strcmp(argv[2], "--profiles-only") == 0) {
        CheckModifierProfileFixtures(image);
        std::printf("hud-2944 profiles: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    SetScannerTestModule(image.data());
    const auto base = reinterpret_cast<std::uintptr_t>(image.data());
    const auto native_cost = ScanForNativeResourceCostAccess();
    Check(native_cost == base + 0xCAF4CFB, "independent native cost anchor");
    const auto modifier_scan = [&] { return psm::hud::ValidateHudModifierListContract(image.data(), image.size()); };
    Check(modifier_scan(), "2944 sustained mutation and contribution chain is valid");
    if (failures) { std::printf("modifier baseline failed before primary checks\n"); return 1; }
    const auto scan = [&] { return psm::hud::ScanContracts(image.data(), image.size(), native_cost); };
    const auto good = scan();
    Check(static_cast<bool>(good), "current PE has three linked contracts");
    Check(good.visibility_after_call == base + 0x1038EBC, "visibility site is after native boolean call");
    Check(good.gauge_before_current_load == base + 0xE68F89, "gauge site precedes R10 cache load");
    Check(good.cost_after_commit == base + 0xCAF4D53, "cost site follows actual commit return");
    Check(good.gauge_update == base + 0xE68490 && good.commit_target == base + 0xCAF70F0,
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
        {0, "bad DOS signature"}, {0x1038DB2, "actor null guard"},
        {0x1038DC3, "dead-state member"}, {0x1038DCE, "mount R8 guard"},
        {0x1038DD6, "mount state member"}, {0x1038DFF, "controller gauge +1A8"},
        {0x1038E04, "refresh link must name discovered gauge function"},
        {0x1038E17, "RBX context replacement contract"}, {0x1038E5F, "action descriptor +25"},
        {0x1038EB1, "native deficit branch"}, {0x1038EB8, "native boolean call target"},
        {0x1038EBD, "post-call AL test"}, {0x1038EBF, "true AL branches to timer reset"},
        {0x1038EE5, "hide-duration +308"}, {0x1038EED, "timer +30C"},
        {0x1038F17, "timer reset destination"},
        {0xE684F7, "gauge role byte +255"}, {0xE684FB, "role table accepts exactly indices 0..2"},
        {0xE68545, "native role table index displacement"},
        {0xE685A2, "actor status chain +68"}, {0xE685A6, "actor status chain +20"},
        {0xE685B5, "actor status owner +18"}, {0xE6865C, "role path joins final cache comparison"},
        {0xE68F51, "old-current stack slot"}, {0xE68F8C, "cache load +230"},
        {0xE68F98, "cached maximum +238"}, {0xE69015, "changed-value vtable callback +518"},
        {0xCAF4CFF, "evaluated pre-value stack +58"}, {0xCAF4D07, "RBX signed expense normalization"},
        {0xCAF4CDB, "cost RSI entry capture"}, {0xCAF4CF5, "getter output stack slot"},
        {0xCAF4CF7, "evaluated getter call target"},
        {0xCAF4D4F, "commit-call destination"}, {0x17B4230, "commit thunk opcode"},
        {0x17B4232, "commit thunk destination"}, {0xCAF710A, "writer entry ownership"},
        {0xCAF7137, "writer threshold must be zero"},
        {0xCAF713A, "writer bridge must target validated tail"},
        {0xCAF713F, "writer state byte +52"},
        {0xCAF7226, "writer stores post-value +08"}, {0xCAF4D53, "post-commit instruction boundary"},
    };
    for (const auto& mutation : mutations) {
        image[mutation.rva] ^= 1;
        Check(Closed(scan()), mutation.name);
        image[mutation.rva] ^= 1;
    }

    const size_t duplicate = 0x1039260;
    std::vector<std::uint8_t> saved(image.begin() + duplicate, image.begin() + duplicate + 0x1A7);
    std::memcpy(image.data() + duplicate, image.data() + 0x1038D90, saved.size());
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
        if (0x1038D90 >= sections[i].VirtualAddress &&
            0x1038D90 - sections[i].VirtualAddress < sections[i].Misc.VirtualSize) {
            const auto flags = sections[i].Characteristics;
            sections[i].Characteristics &= ~IMAGE_SCN_MEM_EXECUTE;
            Check(Closed(scan()), "nonexecutable visibility bytes rejected");
            sections[i].Characteristics = flags;
            break;
        }
    }
    Check(static_cast<bool>(scan()), "all mutation fixtures restored");
    Check(modifier_scan(), "primary mutations preserve sustained modifier contract");
    Check(!psm::hud::ValidateHudModifierListContract(nullptr, 0), "modifier contract rejects null image");
    const Mutation modifier_mutations[] = {
        {0xCAE7E7C, "modifier list pointer +D8"}, {0xCAE7E8C, "modifier list count +E0"},
        {0xCAE7EFD, "modifier capacity +E4"}, {0xCAE7ED7, "modifier stride +30"},
        {0xCAE7EB4, "modifier stat grouping"}, {0xCAE7EC1, "modifier selector grouping"},
        {0xCAE7EF6, "add/remove ordering branch"}, {0xCAE801D, "increment list count"},
        {0xCAE80A0, "decrement list count"}, {0xCAE80E6, "penalty propagation +18"},
        {0xCAE8144, "after-minus-before contribution"}, {0xCAE8149, "delta output destination"},
        {0x17AFE33, "first caller mutator link"}, {0x17AFE53, "first caller AB00 link"},
        {0x17AFFA2, "second caller mutator link"}, {0x17AFFC2, "second caller AB00 link"},
        {0x17B3560, "modifier trampoline"}, {0x17B33A5, "AB00 owner register"},
        {0x17B341A, "AB00 aggregation after observation"},
        {0x17B4640, "contribution trampoline"}, {0xCAE7ECD, "before contribution link"},
        {0xCAE812F, "after contribution link"}, {0xCAF7FA8, "contribution penalty +18"},
        {0xCAF7FD7, "contribution base +08"}, {0xCAF801D, "contribution boost +28"},
        {0xCAF7FC0, "million-unit penalty denominator"},
        {0xCAF7FF4, "contribution arithmetic helper link"},
        {0xCAF7FFB, "arithmetic status compared to zero"}, {0xCAF7FFD, "arithmetic failure branch"},
        {0xCAF805D, "boost returns to shared arithmetic"},
    };
    for (const auto& mutation : modifier_mutations) {
        image[mutation.rva] ^= 1;
        Check(!modifier_scan(), mutation.name);
        image[mutation.rva] ^= 1;
    }
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size = 0;
    Check(!modifier_scan(), "modifier linked function boundaries required");
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION] = directory;
    const auto duplicate_modifier = [&](size_t source, size_t length, const char* name) {
        const size_t destination = 0x103A000;
        std::vector<std::uint8_t> original(image.begin() + destination, image.begin() + destination + length);
        std::memcpy(image.data() + destination, image.data() + source, length);
        Check(!modifier_scan(), name);
        std::memcpy(image.data() + destination, original.data(), length);
    };
    duplicate_modifier(0xCAE7E60, 0x302, "duplicate current modifier rejects invalid links");
    duplicate_modifier(0xCAF7F90, 0xE8, "duplicate current contribution rejects invalid links");

    const auto mixed_modifier = [&](const auto& legacy, const char* name) {
        const size_t destination = 0x103A000;
        std::vector<std::uint8_t> original(image.begin() + destination,
                                          image.begin() + destination + sizeof(legacy));
        std::memcpy(image.data() + destination, legacy, sizeof(legacy));
        Check(!modifier_scan(), name);
        std::memcpy(image.data() + destination, original.data(), sizeof(legacy));
    };
    mixed_modifier(legacy_kModifierMutator, "mixed current and legacy modifier candidates rejected");
    mixed_modifier(legacy_kContribution, "mixed current and legacy contribution candidates rejected");
    CheckModifierProfileFixtures(image);
    Check(modifier_scan(), "all sustained modifier fixtures restored");
    // Duplicate current cost bytes must be rejected before linked validation.
    const size_t cost_duplicate = 0x1039400;
    std::vector<std::uint8_t> cost_saved(image.begin() + cost_duplicate,
                                        image.begin() + cost_duplicate + 0x67);
    std::memcpy(image.data() + cost_duplicate, image.data() + 0xCAF4CFB, 0x67);
    const auto duplicate_cost = scan();
    Check(Closed(duplicate_cost) && duplicate_cost.status == psm::hud::ScanStatus::Ambiguous,
          "duplicate current cost code fails closed even with invalid links");
    std::memcpy(image.data() + cost_duplicate, cost_saved.data(), cost_saved.size());
    // An old-version candidate must not mask ambiguity with the current form.
    const std::uint8_t old_cost[] = {0x48, 0x8B, 0x4C, 0x24, 0x58, 0x4C, 0x8D, 0x04, 0x19, 0x48, 0xC1, 0xEB, 0x3F, 0x84, 0xDB, 0x74, 0x1C, 0x48, 0x8B, 0x46, 0x28, 0x48, 0x39, 0xC1, 0x7F, 0x09, 0x48, 0x39, 0xF8, 0x0F, 0x84, 0x9A, 0x00, 0x00, 0x00, 0x49, 0x39, 0xC0, 0x7F, 0x25, 0x49, 0x89, 0xC0, 0xEB, 0x20, 0x4C, 0x8B, 0x4E, 0x20, 0x4C, 0x03, 0x4E, 0x18, 0x49, 0x39, 0xC9, 0x7F, 0x05, 0x49, 0x39, 0xF9, 0x74, 0x7E, 0x4D, 0x39, 0xC1, 0x8A, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x03, 0x4D, 0x89, 0xC8, 0x44, 0x0F, 0xB7, 0x4C, 0x24, 0x50, 0x48, 0x89, 0xEA, 0x48, 0x89, 0xF1, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0xF1, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0x78, 0x20, 0x00, 0x76, 0x41};
    std::vector<std::uint8_t> mixed_saved(image.begin() + cost_duplicate,
                                         image.begin() + cost_duplicate + sizeof(old_cost));
    std::memcpy(image.data() + cost_duplicate, old_cost, sizeof(old_cost));
    const auto mixed_cost = scan();
    Check(Closed(mixed_cost) && mixed_cost.status == psm::hud::ScanStatus::Ambiguous,
          "mixed supported cost versions fail closed before checking links");
    std::memcpy(image.data() + cost_duplicate, mixed_saved.data(), mixed_saved.size());
    Check(static_cast<bool>(scan()), "cost mutation restored");
    std::printf("hud-2944: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
