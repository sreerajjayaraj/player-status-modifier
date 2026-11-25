#include "item_3446.h"
#include "runtime/item_metadata.h"

#include <Windows.h>
#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {
bool Map(const char* path, std::vector<unsigned char>& image) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (length < 4096 || length > 0x20000000) return false;
    std::vector<unsigned char> raw(static_cast<size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(raw.data()), length)) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<size_t>(dos->e_lfanew) > raw.size() - sizeof(IMAGE_NT_HEADERS64)) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage > 0x20000000 ||
        nt->OptionalHeader.SizeOfHeaders > raw.size() ||
        nt->OptionalHeader.SizeOfHeaders > nt->OptionalHeader.SizeOfImage) return false;
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    if (reinterpret_cast<const unsigned char*>(sections) + nt->FileHeader.NumberOfSections * sizeof(*sections) >
        raw.data() + raw.size()) return false;
    image.resize(nt->OptionalHeader.SizeOfImage);
    std::memcpy(image.data(), raw.data(), nt->OptionalHeader.SizeOfHeaders);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        const auto size = section.SizeOfRawData;
        if (size_t(section.PointerToRawData) + size > raw.size() ||
            size_t(section.VirtualAddress) + size > image.size()) return false;
        std::memcpy(image.data() + section.VirtualAddress, raw.data() + section.PointerToRawData, size);
    }
    return true;
}
}


int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::vector<unsigned char> game, menu;
    if (!Map(argv[1], game) || !Map(argv[2], menu)) return 2;

    int failures = 0;
    const auto check = [&](bool ok, const char* description) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", description);
        failures += !ok;
    };
    const auto base = reinterpret_cast<uintptr_t>(game.data());
    const auto resolve = [&](const void* menuImage = nullptr) {
        return psm::items3446::ResolveDetailed(game.data(), menuImage);
    };
    const auto assertNative = [&](const ItemGainCaptureTarget& item) {
        return item.address == base + 0xD5CF2E0 &&
            item.native_plan_function == base + 0x2A504B0 &&
            item.native_inventory_owner_lookup == base + 0x23643B0 &&
            item.native_move_parent == base + 0x2A58FE0 &&
            item.item_info_lookup == base + 0x3DB320 &&
            item.stack_size_offset == 0x10 && item.weighted_offset == 0x1E &&
            item.item_id_offset == 0x8;
    };
    const auto target = resolve();
    check(assertNative(target), "native 3732 item receipt and planner resolve without a provider");
    check(assertNative(resolve(menu.data())), "DLMM 6.0.5 present leaves the native item graph available");

    std::array<unsigned char, 0x20> sample{};
    sample[0x10] = 12;
    sample[0x08] = 42;
    sample[0x1E] = 1;
    psm::items::MetadataSnapshot metadata{};
    const psm::items::MetadataProfile profile{
        target.metadata_kind, target.stack_size_offset, target.weighted_offset, target.item_id_offset
    };
    check(target.address &&
          psm::items::ReadMetadataSnapshot(reinterpret_cast<uintptr_t>(sample.data()), profile, metadata) &&
          metadata.stack_size == 12 && metadata.item_id == 42 && metadata.weighted == 1,
          "item metadata reads the current exclusion byte");

    for (const auto site : {0x2A504B0u, 0x23643B0u, 0x266EF40u, 0xD5CF415u, 0xD60FD9Du}) {
        game[site] ^= 1;
        check(!resolve(menu.data()).address, "changed item entry or metadata proof refuses the group");
        game[site] ^= 1;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(game.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(game.data() + dos->e_lfanew);
    nt->FileHeader.TimeDateStamp ^= 1;
    nt->OptionalHeader.CheckSum ^= 1;
    check(assertNative(resolve(menu.data())), "native proof ignores PE timestamp and checksum");
    // Present both 6.0.5 capture records without executing either PE. The
    // trampoline bytes are a fixture read by the same production verifier.
    const auto menuBase = reinterpret_cast<uintptr_t>(menu.data());
    psm::v3446::Image menuImage;
    if (!menuImage.Init(menu.data(), menu.size())) return 2;
    const auto descriptor = psm::capture_compat::Context::DataExport(
        menuImage, "DesertLinkInventoryCaptureApiV1");
    if (!descriptor) return 2;
    const auto rebase = [&](std::size_t rva) {
        uintptr_t pointer = 0;
        std::memcpy(&pointer, menu.data() + rva, sizeof(pointer));
        pointer += menuBase - menuImage.nt.OptionalHeader.ImageBase;
        std::memcpy(menu.data() + rva, &pointer, sizeof(pointer));
    };
    for (std::size_t slot = 4; slot < 17; ++slot) rebase(descriptor + slot * 8);
    const auto loadConfig = menuImage.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
    rebase(loadConfig.VirtualAddress + offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardCFDispatchFunctionPointer));

    using psm::item_insert_3663::Lane;
    const auto insert = psm::item_insert_3663::MenuCapture(menu.data(), Lane::Insert);
    const auto owner = psm::item_insert_3663::MenuCapture(menu.data(), Lane::OwnerLookup);
    check(insert.handler == menuBase + 0x74F10 && owner.handler == menuBase + 0x75030,
          "relocated 6.0.5 capture export and its native tails validate");
    if (!insert.handler || !owner.handler) return 1;

    const auto limit = base + game.size() + 0x40000000;
    auto cursor = base + game.size();
    unsigned char* bridge = nullptr;
    while (cursor < limit && !bridge) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region))) break;
        const auto begin = reinterpret_cast<uintptr_t>(region.BaseAddress);
        const auto end = begin + region.RegionSize;
        if (end <= cursor) break;
        if (region.State == MEM_FREE) {
            const auto next = (cursor + 0xffff) & ~uintptr_t{0xffff};
            if (next <= end && 0x10000 <= end - next)
                bridge = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(next),
                    0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        }
        cursor = end;
    }
    if (!bridge) return 2;

    const auto putJump = [](unsigned char* out, uintptr_t destination) {
        constexpr unsigned char op[]{0xFF, 0x25, 0, 0, 0, 0};
        std::memcpy(out, op, sizeof(op));
        std::memcpy(out + sizeof(op), &destination, sizeof(destination));
    };
    const auto captured = [&](std::size_t rva, unsigned char* stub, const auto& lane) {
        auto* gameEntry = game.data() + rva;
        const auto entry = reinterpret_cast<uintptr_t>(gameEntry);
        const auto original = reinterpret_cast<uintptr_t>(stub);
        const auto relay = original + lane.span + 14;
        const auto relative = static_cast<intptr_t>(relay) - static_cast<intptr_t>(entry + 5);
        if (relative < INT32_MIN || relative > INT32_MAX) return false;
        const auto delta = static_cast<std::int32_t>(relative);
        std::memcpy(stub, gameEntry, lane.span);
        putJump(stub + lane.span, entry + lane.span);
        putJump(stub + lane.span + 14, lane.handler);
        const std::array<uintptr_t, 5> record{entry, original, relay, 1, 0x100000001ULL};
        std::memcpy(reinterpret_cast<void*>(lane.record), record.data(), sizeof(record));
        gameEntry[0] = 0xE9;
        std::memcpy(gameEntry + 1, &delta, sizeof(delta));
        return true;
    };
    DWORD oldProtection = 0;
    if (!captured(0x266EF40, bridge, insert) || !captured(0x23643B0, bridge + 64, owner) ||
        !VirtualProtect(bridge, 0x10000, PAGE_EXECUTE_READ, &oldProtection)) {
        VirtualFree(bridge, 0, MEM_RELEASE);
        return 2;
    }
    unsigned mask = 0;
    const auto shared = psm::items3446::ResolveDetailed(game.data(), menu.data(), &mask);
    check(assertNative(shared) && mask == 3, "both active DesertLink 6.0.5 inventory captures coexist");
    psm::v3446::Image gameImage;
    if (!gameImage.Init(game.data(), game.size())) return 2;
    check(psm::items3732::ValidateBeforeEnable(gameImage, menu.data(), target),
          "activation recheck sees the same native owners through proved captures");
    game[0x266EF41] ^= 1;
    check(!psm::items3732::ValidateBeforeEnable(gameImage, menu.data(), target),
          "activation refuses a changed capture relay");
    VirtualFree(bridge, 0, MEM_RELEASE);
    return failures ? 1 : 0;
}


