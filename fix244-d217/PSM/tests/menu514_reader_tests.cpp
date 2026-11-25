#include "hud_scanner.h"
#include "native_3446.h"
#include "scanner.h"
#ifdef PSM_MENU514_READER_FIXED
#include "native_menu_reader.h"
#endif

#include <Windows.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

void Log(const char*, ...) {}

namespace {
constexpr std::size_t kReader = 0x18EDD20;
constexpr std::size_t kMenuHandler = 0xDEB00;
constexpr std::size_t kMenuRecord = 0x512010;
constexpr std::size_t kMenuOriginal = 0x5132A8;

struct Map {
    unsigned char* data = nullptr;
    std::size_t size = 0;

    explicit Map(const char* path) {
        std::ifstream file(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
        if (bytes.size() < 4096) return;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            std::size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > bytes.size()) return;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfHeaders > bytes.size()) return;
        size = nt->OptionalHeader.SizeOfImage;
        data = static_cast<unsigned char*>(VirtualAlloc(nullptr, size + 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!data) return;
        std::memcpy(data, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (std::size_t(section->PointerToRawData) + section->SizeOfRawData > bytes.size() ||
                std::size_t(section->VirtualAddress) + section->SizeOfRawData > size) {
                VirtualFree(data, 0, MEM_RELEASE);
                data = nullptr;
                return;
            }
            std::memcpy(data + section->VirtualAddress, bytes.data() + section->PointerToRawData, section->SizeOfRawData);
        }
    }

    ~Map() { if (data) VirtualFree(data, 0, MEM_RELEASE); }
};

template<class T> void Write(unsigned char* at, const T& value) { std::memcpy(at, &value, sizeof(T)); }
void AbsoluteJump(unsigned char* at, uintptr_t target) {
    constexpr unsigned char prefix[] = {0xFF, 0x25, 0, 0, 0, 0};
    std::memcpy(at, prefix, sizeof(prefix));
    Write(at + sizeof(prefix), target);
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    Map game(argv[1]), menu(argv[2]);
    if (!game.data || !menu.data) return 2;
    SetScannerTestModule(game.data);
    const auto base = reinterpret_cast<uintptr_t>(game.data);
    const auto menuBase = reinterpret_cast<uintptr_t>(menu.data);
    const auto rate = base + 0x18EDDBE;
    const auto cost = base + 0xB619BDB;
    const auto mount = [&] { return psm::hud::ScanMountedResourceContracts(game.data, game.size); };
    const auto hud = [&] { return psm::hud::ScanContracts(game.data, game.size, cost); };
    int failures = 0;
    const auto check = [&](bool pass, const char* name) {
        std::printf("%s %s\n", pass ? "PASS" : "FAIL", name);
        failures += !pass;
    };
    check(ValidateNativeResourceGroup(rate, cost) && bool(mount()) && bool(hud()), "stock 3576 reader and all native consumers");
    auto* bridge = game.data + game.size;
    const auto bridgeAddress = reinterpret_cast<uintptr_t>(bridge);
    auto* entry = game.data + kReader;
    auto* record = menu.data + kMenuRecord;
    std::array<unsigned char, 5> original{};
    std::memcpy(original.data(), entry, original.size());
    std::memcpy(bridge, original.data(), original.size());
    AbsoluteJump(bridge + 5, base + kReader + 5);
    AbsoluteJump(bridge + 19, menuBase + kMenuHandler);
    Write(record, uint64_t{0x314B524243524C44});
    Write(record + 8, uint32_t{1});
    Write(record + 0x0C, uint32_t{0x70});
    Write(record + 0x10, uint64_t{0x3432343345565255});
    Write(record + 0x18, base + kReader);
    Write(record + 0x20, menuBase + kMenuHandler);
    Write(record + 0x28, menuBase);
    Write(record + 0x30, menuBase + 0xDCC90);
    std::memcpy(record + 0x38, entry, 21);
    Write(record + 0x50, uint32_t{1});
    Write(menu.data + kMenuOriginal, bridgeAddress);
    Write(menu.data + 0x513430, menuBase + kMenuRecord);
    Write(menu.data + 0x513440, menuBase + kMenuOriginal);
    entry[0] = 0xE9;
    Write(entry + 1, int32_t(bridgeAddress + 19 - (base + kReader + 5)));

#ifdef PSM_MENU514_READER_FIXED
    psm::menu_reader::SetTestProvider(menu.data, {bridge, nullptr, [](void* opaque, uintptr_t at, std::size_t size) {
        return at == reinterpret_cast<uintptr_t>(opaque) && size == 33;
    }});
#endif

    check(ValidateNativeResourceGroup(rate, cost), "owned Menu reader preserves native resource group");
    const auto mounted = mount();
    check(mounted && mounted.current_evaluator == base + kReader &&
          mounted.query_after_lookup == base + 0x18E7192 && mounted.entry_lookup == base + 0x18E7610,
          "owned Menu reader preserves the original mounted actor and query observers");
    check(bool(hud()), "owned Menu reader preserves the observation-only HUD path");

    for (const auto& mutation : std::array<std::pair<unsigned char*, const char*>, 8>{
             {{entry + 5, "reader bytes outside the detour"},
              {entry + 1, "wrong entry relay"},
              {bridge, "wrong displaced native prologue"},
              {bridge + 11, "wrong original continuation"},
              {bridge + 25, "wrong provider relay"},
              {record + 0x50, "unpublished Menu ownership"},
              {menu.data + kMenuHandler + 0x48, "changed Menu callback"},
              {menu.data + 0xDF34A, "changed Menu installer"}}}) {
        *mutation.first ^= 1;
        check(!ValidateNativeResourceGroup(rate, cost) && !mount() && !hud(), mutation.second);
        *mutation.first ^= 1;
    }

    game.data[0x18E7191] ^= 1;
    check(!mount() && ValidateNativeResourceGroup(rate, cost), "damaged mount lookup link fails independently");
    game.data[0x18E7191] ^= 1;
    check(ValidateNativeResourceGroup(rate, cost) && bool(mount()) && bool(hud()), "all fixture mutations restored");
    std::printf("Menu 5.1.4 curve reader fixture failures=%d\n", failures);
    return failures ? 1 : 0;
}

