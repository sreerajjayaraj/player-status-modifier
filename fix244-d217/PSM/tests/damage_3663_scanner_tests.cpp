#include "damage_menu514_contract.h"

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
    std::vector<unsigned char> game_data, menu_data;
    psm::v3446::Image game, menu;
    if (!Map(argv[1], game_data) || !Map(argv[2], menu_data) ||
        !game.Init(game_data.data(), game_data.size()) || !menu.Init(menu_data.data(), menu_data.size())) return 2;
    using namespace psm::damage_menu514;
    int failures = 0;
    const auto check = [&](bool ok, const char* description) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", description);
        failures += !ok;
    };
    const auto native = Resolve(game, nullptr);
    check(native.code_begin && !native.menu, "verified native damage dispatcher without Menu");
    constexpr size_t tailwind = 0x194D119;
    constexpr size_t menu_entry_witness_size = 0x29;
    constexpr size_t menu_body_witness = 0x194D13F;
    check(kNativeSite == 0x194D122 && kNativeSite >= tailwind + 9 &&
          kNativeSite >= kNativeEntry + menu_entry_witness_size &&
          kNativeSite + kPatchSize <= menu_body_witness,
          "PSM hook lies beyond both entry witness and Tailwind and before Menu body witness");
    const auto with_menu = Resolve(game, &menu);
    check(with_menu.code_begin && with_menu.menu == reinterpret_cast<uintptr_t>(menu.data),
          "exact Menu 6.0 native damage forwarding ABI prepared before its hook appears");
    std::array<unsigned char,9> original{};
    std::memcpy(original.data(), game_data.data()+tailwind, original.size());
    constexpr unsigned char simulated_tailwind[] = {0xE9,0x01,0x02,0x03,0x04,0x90,0x90,0x90,0x90};
    std::memcpy(game_data.data()+tailwind, simulated_tailwind, sizeof(simulated_tailwind));
    check(Resolve(game, &menu) == with_menu, "Tailwind changes neither PSM nor Menu validation spans");
    std::memcpy(game_data.data()+tailwind, original.data(), original.size());
    game_data[kNativeEntry] = 0xE9;
    check(!Resolve(game, &menu).code_begin, "unknown native entry detour rejected even with Menu present");
    game_data[kNativeEntry] = kNativePrologue[0];
    game_data[kNativeSite+1] ^= 1;
    check(!Resolve(game, &menu).code_begin, "foreign write in PSM nine-byte span rejected");
    game_data[kNativeSite+1] ^= 1;
    game_data[0x67BBAA4] ^= 1;
    check(!Resolve(game, &menu).code_begin, "changed dispatcher unwind fails closed");
    game_data[0x67BBAA4] ^= 1;
    alignas(16) std::array<unsigned char,0xC50> stack{};
    const auto caller = native.code_begin + 0x40;
    constexpr uintptr_t source = 0x12345678;
    std::memcpy(stack.data()+0x78,&caller,sizeof(caller));
    std::memcpy(stack.data()+0xA0,&source,sizeof(source));
    Frame frame{};
    check(DecodeFrame(native,reinterpret_cast<uintptr_t>(stack.data()),frame) &&
          frame.caller == caller && frame.source == source && !frame.menu,
          "current post-prologue stack preserves caller and source ABI");
    const auto menu_return = with_menu.menu + 0xF5516;
    std::memcpy(stack.data()+0x78,&menu_return,sizeof(menu_return));
    std::memcpy(stack.data()+0xC48,&caller,sizeof(caller));
    check(DecodeFrame(with_menu,reinterpret_cast<uintptr_t>(stack.data()),frame) &&
          frame.caller == caller && frame.source == source && frame.menu,
          "current Menu forwarder preserves native caller and source at exact offsets");
    check(!DecodeFrame(native,reinterpret_cast<uintptr_t>(stack.data()),frame),
          "unknown caller receives no typed damage route");
    check(Resolve(game,&menu) == with_menu, "stock dispatcher restored after inert mutations");
    // A simulated MinHook lease in inert storage, with the same layout as Menu.
    const auto entry = reinterpret_cast<uintptr_t>(game.data) + kNativeEntry;
    const auto trampoline = entry + 0x1000;
    auto* bridge = reinterpret_cast<unsigned char*>(trampoline);
    const auto relay = [](unsigned char* at, uintptr_t target) {
        constexpr unsigned char jump[] = {0xFF,0x25,0,0,0,0};
        std::memcpy(at,jump,sizeof(jump));
        std::memcpy(at+sizeof(jump),&target,sizeof(target));
    };
    std::memcpy(bridge,kNativePrologue,5);
    relay(bridge+5,entry+5);
    relay(bridge+19,with_menu.menu+kMenu60.handler.BeginAddress);
    const auto branch_delta = static_cast<int32_t>(trampoline+19-entry-5);
    game_data[kNativeEntry] = 0xE9;
    std::memcpy(game_data.data()+kNativeEntry+1,&branch_delta,sizeof(branch_delta));
    std::memcpy(menu_data.data()+kMenu60.target_slot,&entry,sizeof(entry));
    std::memcpy(menu_data.data()+kMenu60.original_slot,&trampoline,sizeof(trampoline));
    const auto private_range = [](void* expected, uintptr_t p, size_t n) {
        return p == reinterpret_cast<uintptr_t>(expected) && n == 33;
    };
    const psm::capture_compat::Reader fixture_reader{bridge,nullptr,private_range};
    check(Resolve(game,&menu,fixture_reader) == with_menu,
          "current Menu installed first and its exact relay and jump-back validate");
    bridge[25] ^= 1;
    check(!Resolve(game,&menu,fixture_reader).code_begin, "incorrect Menu relay refused");
    bridge[25] ^= 1;
    bridge[11] ^= 1;
    check(!Resolve(game,&menu,fixture_reader).code_begin, "incorrect Menu jump-back refused");
    bridge[11] ^= 1;
    check(!Resolve(game,nullptr,fixture_reader).code_begin, "unidentified owner refused");
    auto* menu_enabled = menu_data.data()+kMenu60.enabled_slot;
    const auto set_menu_damage_percent = [&](int32_t percent) {
        std::memcpy(menu_data.data()+kMenu60.multiplier_slot,&percent,sizeof(percent));
    };
    *menu_enabled = 0;
    set_menu_damage_percent(200);
    check(!PreferMenuOutgoing(&with_menu), "saved Menu multiplier is inactive until Menu enables it");
    *menu_enabled = 1;
    check(PreferMenuOutgoing(&with_menu), "active Menu 200 percent takes outgoing precedence");
    set_menu_damage_percent(100);
    check(!PreferMenuOutgoing(&with_menu), "Menu 100 percent leaves PSM outgoing enabled");
    set_menu_damage_percent(50);
    check(PreferMenuOutgoing(&with_menu), "active Menu 50 percent takes outgoing precedence");

    std::printf("damage 3663 fixture failures=%d game_and_menu_code_executed=0\n", failures);
    return failures ? 1 : 0;
}
