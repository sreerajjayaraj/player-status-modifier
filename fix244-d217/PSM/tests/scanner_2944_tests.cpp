#include <Windows.h>
#include "scanner.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

void Log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    std::puts("");
    va_end(args);
}

int main(int argc, char** argv) {
    if (argc != 2) { std::puts("usage: scanner-image-tests <1.0.0.2760 executable>"); return 2; }
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.size() < 4096) return 2;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > bytes.size()) return 2;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfHeaders > bytes.size()) return 2;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage,
                                                         MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!image) return 2;
    std::memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    auto* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sections[i];
        if (static_cast<size_t>(s.PointerToRawData) + s.SizeOfRawData > bytes.size() ||
            static_cast<size_t>(s.VirtualAddress) + s.SizeOfRawData > nt->OptionalHeader.SizeOfImage) return 2;
        std::memcpy(image + s.VirtualAddress, bytes.data() + s.PointerToRawData, s.SizeOfRawData);
    }
    SetScannerTestModule(image); // Data-only map. Never load or execute the game.
    const auto base = reinterpret_cast<uintptr_t>(image);
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name) {
        std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
        if (!condition) ++failures;
    };
    const auto item = ScanForItemGainAccess();
    check(item.address == base + 0xF8391E0, "2944 item receipt resolves the native destination");
    std::printf("item address=%llX push=%llX plan=%llX caller=%llX mapper=%llX\n",
        item.address ? item.address-base:0, item.push_function ? item.push_function-base:0,
        item.plan_site ? item.plan_site-base:0, item.plan_caller_return ? item.plan_caller_return-base:0,
        item.container_mapper ? item.container_mapper-base:0);

    check(item.push_function == base + 0x27AECC0 && item.plan_site == base + 0x27AE796 &&
          item.plan_caller_return == base + 0x2B53ED0 && item.standalone_caller_return == base + 0x2B53FB1 &&
          item.container_mapper == base + 0x1AB0930, "2944 storage planning and committed transfer stay linked");
    const struct { size_t rva; const char* label; } mutations[] = {
        {0x27AF160, "missing destination call fails closed"},
        {0xF8391F0, "receipt slot register drift rejected"},
        {0xF8391F6, "receipt result pointer capture drift rejected"},
        {0xF839368, "receipt C8 destination stride drift rejected"},
        {0xF83936E, "receipt destination stride width drift rejected"},
        {0xF839372, "native merge relation drift rejected"},
        {0xF8393E6, "new stack copy relation drift rejected"},
        {0xF8392FF, "metadata must read the selected slot"},
        {0xF839307, "metadata lookup relationship drift rejected"},
        {0xF83942A, "native stack helper relationship drift rejected"},
        {0xF86B693, "native stack size field drift rejected"},
        {0xF86B69A, "native stackability comparison drift rejected"},
        {0xF86B69C, "stackable items must bypass special capacity path"},
        {0xF86B6FA, "stackable capacity return register drift rejected"},
        {0x212A131, "native owner type field drift rejected"},
        {0x27AE6C5, "planning frame size drift rejected"},
        {0x27AE6CF, "planning push capture drift rejected"},
        {0x27AE721, "planning receipt descriptor drift rejected"},
        {0x27AE794, "nonempty receipt gate drift rejected"},
        {0x27AE7A5, "SQL row builder call drift rejected"},
        {0x1AB0932, "container mapper output-pointer ABI drift rejected"},
        {0x2B53EE9, "pending source preparation relation drift rejected"},
        {0x2B53F92, "source removal call drift rejected"},
        {0x2B53F9B, "source success gate drift rejected"},
        {0x2B53FAD, "committed destination push relation drift rejected"}
    };
    for (const auto& mutation : mutations) {
        const auto old = image[mutation.rva];
        image[mutation.rva] ^= 1;
        const auto rejected = ScanForItemGainAccess();
        check(!rejected.address && !rejected.plan_site && !rejected.container_mapper, mutation.label);
        image[mutation.rva] = old;
    }
    const auto rate=ScanForNativeResourceRateAccess(), cost=ScanForNativeResourceCostAccess();
    check(rate==base+0x17B415B && cost==base+0xCAF4CFB && ValidateNativeResourceGroup(rate,cost),
          "2944 native resource group is unchanged by item compatibility repair");
    check(ScanForStatWriteAccess().address==base+0xCAF7133, "health write remains before native notifications");
    VirtualFree(image, 0, MEM_RELEASE);
    return failures ? 1 : 0;
}
