#include "damage_menu514_contract.h"
#include "hud_scanner.h"

#include <Windows.h>
#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

void Log(const char*, ...) {}

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
    if (!Map(argv[1],game_data) || !Map(argv[2],menu_data) ||
        !game.Init(game_data.data(),game_data.size()) ||
        !menu.Init(menu_data.data(),menu_data.size())) return 2;
    using namespace psm::damage_menu514;
    const auto base = reinterpret_cast<uintptr_t>(game.data);
    int failures = 0;
    const auto check = [&](bool passes, const char* label) {
        std::printf("%s %s\n", passes ? "PASS" : "FAIL", label);
        failures += !passes;
    };

    const auto native = Resolve(game,nullptr);
    check(native.code_begin && !native.menu && native.native_site == 0x194EFB2 &&
          native.native_site >= 0x194EF80 + 41 && native.native_site >= 0x194EFA9 + 9 &&
          native.native_site + kPatchSize <= 0x194EFCF, "3732 standalone damage resolves outside Menu witnesses");
    check(Resolve(game,&menu).menu == reinterpret_cast<uintptr_t>(menu.data),
          "DLMM 6.0.5 verified stat forwarding is recognized");
    const auto provider = Resolve(game,&menu);
    // Production installs the typed-slot observer before damage setup.
    // Simulate the five-byte branch replacing that observer's own span.
    const auto savedSlot = std::array<unsigned char,5>{
        game_data[0x194EC3B],game_data[0x194EC3C],game_data[0x194EC3D],game_data[0x194EC3E],game_data[0x194EC3F]};
    const unsigned char statsDetour[5] = {0xE9,0,0,0,0};
    std::memcpy(game_data.data()+0x194EC3B,statsDetour,sizeof(statsDetour));
    check(IsCurrentGame(game) && Resolve(game,&menu) == provider,
          "live ordering leaves the interior dispatcher available after the PSM stats hook");
    std::memcpy(game_data.data()+0x194EC3B,savedSlot.data(),savedSlot.size());
    check(provider.contract == &kMenu605 && provider.native_site == native.native_site,
          "provider binding keeps the verified interior site");
    alignas(16) std::array<unsigned char,0xC50> stack{};
    const auto menu_ret = provider.menu + kMenu605.return_rva;
    const auto native_ret = native.code_begin + 0x40;
    const uintptr_t source = 0x12345678;
    std::memcpy(stack.data()+0x78,&menu_ret,sizeof(menu_ret));
    std::memcpy(stack.data()+0xA0,&source,sizeof(source));
    std::memcpy(stack.data()+0xC48,&native_ret,sizeof(native_ret));
    Frame frame{};
    check(DecodeFrame(provider,reinterpret_cast<uintptr_t>(stack.data()),frame) &&
          frame.menu && frame.caller == native_ret && frame.source == source,
          "verified 6.0.5 forwarder recovers the original game caller");
    const unsigned char enabled = 1;
    const int32_t percent = 150;
    std::memcpy(menu_data.data()+kMenu605.multiplier_slot,&percent,sizeof(percent));
    menu_data[kMenu605.enabled_slot] = 0;
    check(!PreferMenuOutgoing(&provider),"disabled DLMM outgoing damage leaves PSM in control");
    menu_data[kMenu605.enabled_slot] = enabled;
    check(PreferMenuOutgoing(&provider),"active DLMM outgoing damage does not stack with PSM");

    std::array<unsigned char,9> native_tls{};
    std::memcpy(native_tls.data(),game_data.data()+0x194EFA9,native_tls.size());
    game_data[0x194EFA9] = 0xE9;
    check(Resolve(game,&menu)==provider,"Tailwind TLS site stays independent");
    std::memcpy(game_data.data()+0x194EFA9,native_tls.data(),native_tls.size());
    game_data[0x194EF80] = 0xE9;
    check(!Resolve(game,&menu).code_begin,"unproven foreign dispatcher owner is refused");
    game_data[0x194EF80] = kNativePrologue[0];
    game_data[native.native_site+1] ^= 1;
    check(!Resolve(game,&menu).code_begin,"edited interior span is refused");
    game_data[native.native_site+1] ^= 1;

    const auto scan_hud = [&] {
        return psm::hud::ScanContracts(game.data,game.size,base+0xB3C0FDB);
    };
    const auto hud = scan_hud();
    check(hud && hud.visibility_after_call==base+0x121F2CC &&
          hud.gauge_before_current_load==base+0x1051D19 &&
          hud.cost_after_commit==base+0xB3C1044,
          "3732 native visibility, gauge load and actual committed-cost relationship");
    check(psm::hud::ValidateHudModifierListContract(game.data,game.size),
          "3732 sustained cost modifier relationships");
    for (const auto corrupt : {0x121F213u,0x1051D19u,0xB3C1044u}) {
        game_data[corrupt] ^= 1;
        check(!scan_hud(),"changed native HUD witness disables the coordinated group");
        game_data[corrupt] ^= 1;
    }
    game_data[0x19547DC] ^= 1;
    check(!psm::hud::ValidateHudModifierListContract(game.data,game.size),
          "changed sustained modifier route does not publish activity");
    return failures ? 1 : 0;
}
