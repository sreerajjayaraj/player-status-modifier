#include "item_3446.h"
#include "menu_item_gain_512.h"

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
    if (argc != 3 && (argc != 4 || std::strcmp(argv[3], "rebuilt"))) return 2;
    std::vector<unsigned char> game, menu;
    if (!Map(argv[1], game) || !Map(argv[2], menu)) return 2;
    const bool rebuilt = argc == 4;
    const auto original_image_size = menu.size();
    if (rebuilt) {
        const auto offset = reinterpret_cast<const IMAGE_DOS_HEADER*>(menu.data())->e_lfanew;
        menu.resize(menu.size() + 0x1000);
        auto* header = reinterpret_cast<IMAGE_NT_HEADERS64*>(menu.data() + offset);
        header->FileHeader.TimeDateStamp = 0xCDEF9876;
        header->OptionalHeader.SizeOfImage = static_cast<DWORD>(menu.size());
        header->OptionalHeader.CheckSum = 0x12345678;
    }
    unsigned mask = 0;
    psm::items3446::ResolveTrace trace;
    const auto native = psm::items3446::ResolveDetailed(game.data(), menu.data(), &mask, &trace);
    int failures = 0;
    const auto check = [&](bool ok, const char* description) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", description);
        failures += !ok;
    };
    const auto base = reinterpret_cast<uintptr_t>(game.data());
    const bool menu604 = original_image_size == 0xCF1000;
    const bool menu601 = (rebuilt && !menu604) ||
        (trace.observer_stamp == 0xDA60636F && trace.observer_size == 0xC00000);
    const bool newer_menu = menu601 ||
        (trace.observer_stamp == 0x1927CF17 && trace.observer_size == 0xC01000);
    const bool first_menu = trace.observer_stamp == 0xB878A84D && trace.observer_size == 0xBF9000;
    check(trace.observer_present && (first_menu || newer_menu || menu604),
          "copied Menu image is present in the inert fixture");
    check(native.address == base + 0xDC6F5E0 && native.native_plan_function == base + 0x2A4DD30 &&
          trace.stage == psm::items3446::ResolveStage::Complete && !trace.missing && !mask && !trace.owner_route,
          "Menu presence leaves the fully checked native inventory path available");
    if (first_menu) {
        check(psm::menu_gain512::Resolve(game.data(), menu.data()).state == psm::menu_gain512::State::Unsupported,
              "older Menu 6.0 queued-request adapter refuses unproved routes");
    }
    for (const auto rva : {0x23626F0u, 0x266CA90u}) {
        const auto saved = game[rva];
        game[rva] = 0xE9;
        const auto refused = psm::items3446::ResolveDetailed(game.data(), menu.data(), &mask, &trace);
        check(!refused.address && !mask, "unproved foreign entry cannot authorize native inventory mutation");
        game[rva] = saved;
    }
    check(psm::items3446::Resolve(game.data(), menu.data()).address == native.address,
          "native inventory proof restores after mutation");
    if (menu604) {
        check(psm::menu_gain512::Resolve(game.data(), menu.data()).state == psm::menu_gain512::State::Unsupported,
              "unknown private Add Item queue is independent of native item gain");
    }
    if (newer_menu || menu604) {
        const auto queue_slot = [menu601](size_t rva) { return menu601 ? rva - 0x1230 : rva; };
        const auto queue_code = [menu601](size_t rva) {
            return menu601 ? rva + (rva < 0x83F50 ? 0x120 : 0xD0) : rva;
        };
        const size_t insert_row = menu601 ? 0x416828 : 0x4177D8;
        const size_t owner_row = menu601 ? 0x416898 : 0x417848;
        const size_t worker_unwind = menu601 ? 0x5E7DE8 : 0x5E91C0;
        const size_t capture_unwind = menu601 ? 0x5E7820 : 0x5E8B68;
        const auto nt_offset = reinterpret_cast<const IMAGE_DOS_HEADER*>(menu.data())->e_lfanew;
        if (!menu604) {
            const auto selected = psm::menu_gain512::Resolve(game.data(), menu.data());
            auto& export_size = reinterpret_cast<IMAGE_NT_HEADERS64*>(menu.data() + nt_offset)
                ->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
            const auto original_export_size = export_size;
            export_size = 0;
            check(psm::menu_gain512::Resolve(game.data(), menu.data()).state ==
                  psm::menu_gain512::State::Unsupported, "missing capture export cannot select a queue");
            export_size = original_export_size;
            const auto& queue = selected;
            check(queue.state == psm::menu_gain512::State::NotReady &&
                  !queue.worker && queue.probe.rva == queue_slot(0x64CE98),
                  "new Menu queue recognizes the proved contract and awaits native binding");
            if (queue.state != psm::menu_gain512::State::NotReady)
                std::printf("queue refusal %s at 0x%zX\n", queue.probe.reason, queue.probe.rva);
        }
        constexpr size_t native_insert = 0x266CA90;
        constexpr size_t native_call = 0x2367B55;
        const size_t handler = menu604 ? 0x750D0 : queue_code(0x73280);
        const size_t capture_state = menu604 ? 0x72E140 : menu601 ? 0x647DA0 : 0x648FD0;
        const size_t capacity_record = capture_state + 40;
        const size_t descriptor = menu604 ? 0x429720 : menu601 ? 0x4154E0 : 0x416490;
        constexpr size_t owner = 0x23626F0;
        const size_t owner_handler = menu604 ? 0x751F0 : queue_code(0x733A0);
        const size_t owner_record = capture_state + 80;
        constexpr size_t owner_span = 6;
        constexpr size_t span = 5;
        auto* entry = game.data() + native_insert;
        const auto native_address = reinterpret_cast<uintptr_t>(entry);
        const auto menu_base = reinterpret_cast<uintptr_t>(menu.data());
        const auto absolute_jump = [](unsigned char* at, uintptr_t target) {
            constexpr unsigned char op[] = {0xFF, 0x25, 0, 0, 0, 0};
            std::memcpy(at, op, sizeof(op));
            std::memcpy(at + sizeof(op), &target, sizeof(target));
        };
        auto cursor = native_address;
        const auto limit = native_address + 0x60000000;
        unsigned char* bridge = nullptr;
        while (cursor < limit && !bridge) {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region))) break;
            const auto begin = reinterpret_cast<uintptr_t>(region.BaseAddress);
            const auto end = begin + region.RegionSize;
            if (end <= cursor) break;
            if (region.State == MEM_FREE) {
                const auto at = (cursor + 0xffff) & ~uintptr_t{0xffff};
                if (at <= end && 0x10000 <= end - at)
                    bridge = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(at),
                        0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            }
            cursor = end;
        }
        if (!bridge) return 2;
        const auto original = reinterpret_cast<uintptr_t>(bridge);
        const auto relay = original + span + 14;
        std::array<unsigned char, span> displaced{};
        std::memcpy(displaced.data(), entry, span);
        std::memcpy(bridge, entry, span);
        absolute_jump(bridge + span, native_address + span);
        absolute_jump(bridge + span + 14, menu_base + handler);
        auto* owner_entry = game.data() + owner;
        auto* owner_bridge = bridge + 64;
        const auto owner_address = reinterpret_cast<uintptr_t>(owner_entry);
        const auto owner_original = reinterpret_cast<uintptr_t>(owner_bridge);
        const auto owner_relay = owner_original + owner_span + 14;
        std::array<unsigned char, owner_span> owner_displaced{};
        std::memcpy(owner_displaced.data(), owner_entry, owner_span);
        std::memcpy(owner_bridge, owner_entry, owner_span);
        absolute_jump(owner_bridge + owner_span, owner_address + owner_span);
        absolute_jump(owner_bridge + owner_span + 14, menu_base + owner_handler);
        DWORD previous = 0;
        if (!VirtualProtect(bridge, 0x10000, PAGE_EXECUTE_READ, &previous)) return 2;
        const auto relative = static_cast<intptr_t>(relay) - static_cast<intptr_t>(native_address + span);
        if (relative < INT32_MIN || relative > INT32_MAX) return 2;
        const auto offset = static_cast<int32_t>(relative);
        for (size_t slot = 4; slot < 17; ++slot) {
            uint64_t pointer = 0;
            std::memcpy(&pointer, menu.data() + descriptor + slot * 8, sizeof(pointer));
            pointer = menu_base + pointer - 0x180000000ULL;
            std::memcpy(menu.data() + descriptor + slot * 8, &pointer, sizeof(pointer));
        }
        if (menu604) {
            const auto* header = reinterpret_cast<const IMAGE_NT_HEADERS64*>(menu.data() + nt_offset);
            const auto config = header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress;
            constexpr auto offset = offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, GuardCFDispatchFunctionPointer);
            const uint64_t dispatch = menu_base + 0x3DB9A0;
            std::memcpy(menu.data() + config + offset, &dispatch, sizeof(dispatch));
        }
        std::array<uintptr_t, 5> record{native_address, original, relay, 1, 0x100000001ULL};
        std::memcpy(menu.data() + capacity_record, record.data(), sizeof(record));
        entry[0] = 0xE9;
        std::memcpy(entry + 1, &offset, sizeof(offset));
        if (!menu604) {
            using namespace psm::menu_gain512;
            const std::array queue_slots{
                std::pair<size_t, uintptr_t>{queue_slot(0x64CE98), base + 0x266E8F0},
                std::pair<size_t, uintptr_t>{queue_slot(0x64CEA0), menu_base + queue_code(0x73600)},
                std::pair<size_t, uintptr_t>{queue_slot(0x64CEA8), base + 0x2366E30},
                std::pair<size_t, uintptr_t>{queue_slot(0x64CEB0), base + 0x3DB570},
                std::pair<size_t, uintptr_t>{queue_slot(0x64CEB8), base + 0x266FED0},
                std::pair<size_t, uintptr_t>{queue_slot(0x64CED0), base + 0xDCB7A60},
                std::pair<size_t, uintptr_t>{queue_slot(0x64CED8), base + 0xCF54FC0},
                std::pair<size_t, uintptr_t>{queue_slot(0x64CF00), base + 0x7246370}
            };
            for (const auto& [rva, pointer] : queue_slots)
                std::memcpy(menu.data() + rva, &pointer, sizeof(pointer));
            const auto native_insert_slot = base + native_insert;
            const auto stock_pointer = reinterpret_cast<uintptr_t>(entry);
            std::memcpy(menu.data() + queue_slot(0x64CEA0), &native_insert_slot, sizeof(native_insert_slot));
            const auto native_binding = Resolve(game.data(), menu.data());
            check(native_binding.state == State::Ready && !native_binding.capacity_wrapper &&
                  native_insert_slot == stock_pointer, "current Menu also accepts its proved direct native insertion binding");
            std::memcpy(menu.data() + queue_slot(0x64CEA0), &queue_slots[1].second, sizeof(uintptr_t));
            const auto ready = Resolve(game.data(), menu.data());
            check(ready.state == State::Ready && ready.worker == menu_base + queue_code(0x844A0) &&
                  ready.caller == menu_base + queue_code(0x7F23F) && ready.capacity_wrapper,
                  "fully bound current Menu admits only its paired queued grant worker");
            if (ready.state != State::Ready)
                std::printf("queue refusal %s at 0x%zX\n", ready.probe.reason, ready.probe.rva);
            for (const auto& [rva, pointer] : queue_slots) {
                menu[rva] ^= 1;
                const auto invalid = Resolve(game.data(), menu.data());
                check(invalid.state == State::Unsupported && invalid.probe.rva == rva,
                      "changed bound native queue slot refuses the worker");
                menu[rva] ^= 1;
            }
            for (const auto rva : {queue_code(0x7F23A), queue_code(0x849B5), queue_code(0x849D3),
                                   queue_code(0x84F05), queue_code(0x8543E), queue_code(0x7BE30),
                                   queue_code(0x8A860), queue_code(0x7EA10), queue_code(0x7C790),
                                   worker_unwind, queue_code(0x73600)}) {
                menu[rva] ^= 1;
                check(Resolve(game.data(), menu.data()).state == State::Unsupported,
                      "changed queue, mirror plan, commit, cleanup, holder or unwind refuses worker");
                menu[rva] ^= 1;
            }
            for (const auto rva : {0x266E920u,0x2366EFEu,0x266C700u,0x3DB570u,0xDC7FAB0u,
                                   0x3DAF18u,0xDCB7A60u,0xDC6F5E0u}) {
                game[rva] ^= 1;
                check(Resolve(game.data(), menu.data()).state == State::Unsupported,
                      "changed native construction, insert relation, commit, cleanup, item metadata or receipt refuses worker");
                game[rva] ^= 1;
            }
        }
        const auto captured = psm::items3446::ResolveDetailed(game.data(), menu.data(), &mask, &trace);
        check(captured.address == native.address && captured.push_function == native.push_function &&
              captured.native_plan_function == native.native_plan_function &&
              captured.native_move_parent == native.native_move_parent &&
              trace.stage == psm::items3446::ResolveStage::Complete && mask == 2 &&
              trace.insert_route == psm::item_insert_3663::Route::Menu,
              "exact installed Menu capacity observer preserves all PSM hook targets");
        const auto invalid = [&](const char* label) {
            const auto refused = psm::items3446::ResolveDetailed(game.data(), menu.data(), &mask, &trace);
            check(!refused.address && trace.stage == psm::items3446::ResolveStage::NativeCallLinks &&
                  trace.failed_call == native_call && trace.expected_target == native_insert && !mask, label);
        };
        for (const auto mutation : {descriptor + 8, capacity_record, capacity_record + 8,
                                    capacity_record + 16, capacity_record + 32,
                                    capacity_record + 36, handler + 0xB2}) {
            menu[mutation] ^= 1;
            invalid("changed descriptor, record or forwarding handler refuses the captured edge");
            menu[mutation] ^= 1;
        }
        entry[1] ^= 1;
        invalid("changed native branch cannot select the approved relay");
        entry[1] ^= 1;
        entry[span] ^= 1;
        invalid("changed native insert body refuses the captured edge");
        entry[span] ^= 1;
        if (!VirtualProtect(bridge, 0x10000, PAGE_READWRITE, &previous)) return 2;
        for (const auto mutation : {0u, 5u, 11u, 19u, 25u}) {
            bridge[mutation] ^= 1;
            if (!VirtualProtect(bridge, 0x10000, PAGE_EXECUTE_READ, &previous)) return 2;
            invalid("changed trampoline replay, continuation or handler relay refuses the captured edge");
            if (!VirtualProtect(bridge, 0x10000, PAGE_READWRITE, &previous)) return 2;
            bridge[mutation] ^= 1;
        }
        if (!VirtualProtect(bridge, 0x10000, PAGE_EXECUTE_READ, &previous)) return 2;
        const std::array native_witness_mutations = menu604
            ? std::array<size_t,3>{handler + 0xC9, 0x6CC1F0, 0x85A76}
            : std::array<size_t,3>{insert_row, queue_code(0x77595), capture_unwind};
        for (const auto mutation : native_witness_mutations) {
            menu[mutation] ^= 1;
            invalid("changed provider native-target row, installer or unwind refuses capture");
            menu[mutation] ^= 1;
        }
        game[0x6926508] ^= 1;
        invalid("changed native insert unwind refuses capture");
        game[0x6926508] ^= 1;
        if (!menu604) {
            using namespace psm::menu_gain512;
            auto* receipt = game.data() + 0xDC6F5E0;
            const auto receipt_address = reinterpret_cast<uintptr_t>(receipt);
            const auto receipt_trampoline = reinterpret_cast<uintptr_t>(bridge + 128);
            const auto fake_callback = reinterpret_cast<uintptr_t>(bridge + 256);
            std::array<unsigned char,17> receipt_prefix{};
            std::memcpy(receipt_prefix.data(),receipt,receipt_prefix.size());
            const auto relative_jump = [](unsigned char* at, uintptr_t target) {
                const auto delta=static_cast<intptr_t>(target)-reinterpret_cast<intptr_t>(at+5);
                if (delta<INT32_MIN || delta>INT32_MAX) return false;
                at[0]=0xE9;
                const auto encoded=static_cast<int32_t>(delta);
                std::memcpy(at+1,&encoded,4);
                return true;
            };
            if (!VirtualProtect(bridge,0x10000,PAGE_READWRITE,&previous)) return 2;
            auto* receipt_bridge = bridge + 128;
            std::memcpy(receipt_bridge,receipt_prefix.data(),5);
            if (!relative_jump(receipt_bridge+5,receipt_address+5) ||
                !relative_jump(receipt,receipt_trampoline+10)) return 2;
            absolute_jump(receipt_bridge+10,fake_callback);
            if (!VirtualProtect(bridge,0x10000,PAGE_EXECUTE_READ,&previous)) return 2;
            const OwnedReceipt owned{receipt_address,fake_callback,receipt_trampoline,receipt_prefix.data(),5,true};
            check(Resolve(game.data(),menu.data(),owned).state == State::Ready,
                  "the retained PSM five-byte receipt trampoline and return preserve the provider route");
            check(Resolve(game.data(),menu.data()).state == State::Unsupported,
                  "identical receipt detour without its own descriptor is refused");
            receipt[1] ^= 1;
            check(Resolve(game.data(),menu.data(),owned).state == State::Unsupported,
                  "changed PSM receipt relay is refused");
            std::memcpy(receipt,receipt_prefix.data(),receipt_prefix.size());
            if (!VirtualProtect(bridge,0x10000,PAGE_READWRITE,&previous)) return 2;
            std::memcpy(receipt_bridge,receipt_prefix.data(),17);
            absolute_jump(receipt_bridge+17,receipt_address+17);
            absolute_jump(receipt,fake_callback);
            std::memset(receipt+14,0x90,3);
            if (!VirtualProtect(bridge,0x10000,PAGE_EXECUTE_READ,&previous)) return 2;
            auto far_owned=owned;
            far_owned.size=17;
            check(Resolve(game.data(),menu.data(),far_owned).state == State::Ready,
                  "the retained PSM seventeen-byte receipt trampoline also preserves the provider route");
            receipt[15] ^= 1;
            check(Resolve(game.data(),menu.data(),far_owned).state == State::Unsupported,
                  "changed long receipt padding refuses the worker");
            std::memcpy(receipt,receipt_prefix.data(),receipt_prefix.size());
        }
        using psm::item_insert_3663::ObserveStable;
        using psm::item_insert_3663::Observe;
        using psm::item_insert_3663::StableProof;
        using psm::item_insert_3663::Lane;
        using psm::item_insert_3663::Route;
        psm::v3446::Image loaded;
        if (!loaded.Init(game.data(), game.size())) return 2;
        const auto published = ObserveStable(loaded, menu.data(), Lane::Insert);
        std::memcpy(entry, displaced.data(), span);
        unsigned samples = 0;
        const auto transitioning = StableProof([&] {
            if (++samples == 2) {
                entry[0] = 0xE9;
                std::memcpy(entry + 1, &offset, sizeof(offset));
            }
            return Observe(loaded, menu.data(), Lane::Insert);
        });
        check(published && transitioning == published && samples == 3,
              "stock insertion may settle into the exact published capture during validation");

        const auto owner_delta = static_cast<intptr_t>(owner_relay) -
                                 static_cast<intptr_t>(owner_address + 5);
        if (owner_delta < INT32_MIN || owner_delta > INT32_MAX) return 2;
        const auto owner_offset = static_cast<int32_t>(owner_delta);
        const std::array<uintptr_t,5> owner_capture{owner_address, owner_original, owner_relay, 1, 0x100000001ULL};
        std::memcpy(menu.data() + owner_record, owner_capture.data(), sizeof(owner_capture));
        owner_entry[0] = 0xE9;
        std::memcpy(owner_entry + 1, &owner_offset, sizeof(owner_offset));
        const auto combined = psm::items3446::ResolveDetailed(game.data(), menu.data(), &mask, &trace);
        check(combined.address == native.address && combined.native_inventory_owner_lookup ==
              native.native_inventory_owner_lookup && mask == 3 && trace.owner_route == 1,
              "both Menu captures preserve the same native owner and PSM transaction sites");
        for (const auto at : {owner_handler + 0x56, owner_record + 16, owner_record + 24, menu604 ? size_t{0x6CC210} : owner_row}) {
            menu[at] ^= 1;
            check(!ObserveStable(loaded, menu.data(), Lane::OwnerLookup),
                  "changed owner handler, record or native row refuses the lookup");
            menu[at] ^= 1;
        }
        const auto old_replay_tail = owner_entry[5];
        owner_entry[5] ^= 1;
        check(!ObserveStable(loaded, menu.data(), Lane::OwnerLookup),
              "sixth native instruction byte must remain intact outside the five-byte branch");
        owner_entry[5] = old_replay_tail;
        std::memcpy(entry, displaced.data(), span);
        const auto owner_only = psm::items3446::ResolveDetailed(game.data(), menu.data(), &mask, &trace);
        check(owner_only.address == native.address && mask == 1 && trace.insert_route == Route::Native,
              "native insertion and separately captured owner lookup remain usable");
        std::memcpy(owner_entry, owner_displaced.data(), owner_span);
        check(psm::items3446::ResolveDetailed(game.data(), menu.data(), &mask, &trace).address ==
              native.address && !mask && trace.insert_route == Route::Native,
              "stock insertion and owner remain valid after provider unhooks");
        VirtualFree(bridge, 0, MEM_RELEASE);
    }
    std::printf("inventory 3663/Menu fixture failures=%d game_and_menu_code_executed=0\n", failures);
    return failures ? 1 : 0;
}
