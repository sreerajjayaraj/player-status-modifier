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
    const auto rate = ScanForNativeResourceRateAccess();
    const auto cost = ScanForNativeResourceCostAccess();
    check(rate == base + 0x171E55B, "rate hook exact instruction boundary, RBX/R11");
    check(cost == base + 0xC4E3EAB, "cost hook after effective-value read, RSI/RBX");
    check(ValidateNativeResourceGroup(rate, cost), "cost calls the hooked evaluator");
    check(ScanForSpiritValueWriteAccess() == base + 0xC4E6A80, "commit function ABI");
    check(ScanForStatWriteAccess().address == base + 0xC4E6AC3, "health/mount write is before state notifications");
    check(ScanForItemGainAccess().address == base + 0xF05A3E0,
          "item hook resolves native ItemPushInventory destination, not unrelated aggregation");
    check(ScanForItemGainAccess().standalone_caller_return == base + 0x2A851D1,
          "standalone storage route resolves successful source-pop then destination-push");
    check(ScanForItemGainAccess().inventory_type_table_global == base + 0x6C2A038,
          "canonical container metadata global is decoded from the native source-pop path");
    const auto item_plan = ScanForItemGainAccess();
    check(item_plan.plan_site == base + 0x26EB026,
          "plan hook precedes SQL rows with RDI push, R14 receipts, R15 owner");
    check(item_plan.plan_caller_return == base + 0x2A850F0,
          "plan caller belongs to the validated standalone transfer");
    check(item_plan.container_mapper == base + 0x1A14EC0,
          "container mapper uses output-word pointer and uint16 route ABI");
    const auto mutate_storage = [&](size_t rva, const char* name) {
        const auto saved_byte = image[rva];
        image[rva] ^= 1;
        const auto rejected = ScanForItemGainAccess();
        check(rejected.address == 0 && rejected.plan_site == 0 &&
              rejected.plan_caller_return == 0 && rejected.container_mapper == 0, name);
        image[rva] = saved_byte;
    };
    mutate_storage(0x2A850EC, "changed standalone planning call target rejected");
    mutate_storage(0x2A850DD, "plan and commit must use the same standalone object");
    mutate_storage(0x2A851A0, "commit object must match the planning object");
    mutate_storage(0x2A85052, "pending source initializer must use push plus 80");
    mutate_storage(0x2A85092, "changed source initializer relationship rejected");
    mutate_storage(0x2A85109, "changed source receipt preparation relationship rejected");
    mutate_storage(0x26EAF55, "changed plan stack allocation invalidates RSP plus 78");
    mutate_storage(0x26EAF5F, "changed plan RDI push capture rejected");
    mutate_storage(0x26EAF71, "changed plan R15 owner capture rejected");
    mutate_storage(0x26EAFB4, "changed plan R14 receipt descriptor rejected");
    mutate_storage(0x26EAF6B, "plan owner lookup must equal push owner lookup");
    mutate_storage(0x26EB5A4, "debt owner lookup must equal the captured plan owner lookup");
    mutate_storage(0x207377D, "owner lookup must return the inventory-manager field");
    mutate_storage(0x26EB024, "SQL row call must retain its nonempty receipt gate");
    mutate_storage(0x26EB036, "changed SQL row builder relationship rejected");
    mutate_storage(0x2B4F861, "changed SQL row builder thunk rejected");
    mutate_storage(0x103BB2B7, "SQL builder must capture the R8 receipt vector");
    mutate_storage(0x103BB2E0, "SQL rows must read the captured receipt vector");
    mutate_storage(0x103BB379, "SQL quantity column must remain builder plus E0");
    mutate_storage(0x103BB380, "SQL quantity must read E0 plus 10");
    mutate_storage(0x103BB46E, "SQL input receipt stride must remain E0");
    mutate_storage(0x26ECE1C, "pending source vector must remain pop plus 50");
    mutate_storage(0x26ECE10, "pending source count must remain pop plus 58");
    mutate_storage(0x26ECE33, "pending source stride must remain 28");
    mutate_storage(0x26ECED8, "source preparation must consume pending requests");
    mutate_storage(0x207830A, "pending source canonical container field must remain plus 10");
    mutate_storage(0x20784AA, "source owner container count must remain plus 20");
    mutate_storage(0x20784BB, "source container identity must remain plus 10");
    mutate_storage(0x2078532, "source request consumer relationship rejected on drift");
    mutate_storage(0x234D9D1, "source request consumer must capture RCX as the real container");
    mutate_storage(0x234DA09, "pending request amount must remain signed qword plus 18");
    mutate_storage(0x2A85171, "SQL failure must bypass inventory mutation");
    mutate_storage(0x234DA13, "pending request nonpositive rejection must remain signed");
    mutate_storage(0x234DACC, "pending request key must remain uint16 at plus zero");
    mutate_storage(0x26EB8DB, "destination owner container array must remain plus 18");
    mutate_storage(0x26EB8DF, "destination owner count must remain plus 20");
    mutate_storage(0x26EB8BB, "mapper must read the receipt from R14, including its REX prefix");
    mutate_storage(0x26EB8D0, "destination E0 slot must remain plus D8");
    mutate_storage(0x26EB8F6, "destination container identity must remain plus 10");
    mutate_storage(0x26EB8C8, "changed mapper call relationship rejected");
    mutate_storage(0x1A14EC2, "mapper must capture its output pointer from RCX");
    mutate_storage(0x1A14ED0, "mapper jump-table base must be the image base");
    mutate_storage(0x1A14F22, "mapper must write a word through the output pointer");
    mutate_storage(0x1A14F18, "mapper route zero canonical field drift rejected");
    mutate_storage(0x1A15444, "mapper route jump-table target drift rejected");
    mutate_storage(0x26EB6F7, "debt record count must remain owner plus E0");
    mutate_storage(0x26EB713, "debt record array must remain owner plus D8");
    mutate_storage(0x26EB70A, "debt record stride must remain 10");
    mutate_storage(0x26EB71C, "debt lookup must use the same item metadata resolver");
    mutate_storage(0x26EB724, "debt key must compare external metadata ID");
    mutate_storage(0x26EB73D, "debt amount must remain qword plus 8");
    unsigned char plan_saved[55];
    auto* plan_duplicate = image + 0x2A85800;
    std::memcpy(plan_saved, plan_duplicate, sizeof(plan_saved));
    std::memcpy(plan_duplicate, image + 0x2A850DA, sizeof(plan_saved));
    check(ScanForItemGainAccess().address == 0, "ambiguous standalone plan caller rejected");
    std::memcpy(plan_duplicate, plan_saved, sizeof(plan_saved));
    mutate_storage(0x2A851B2, "missing source-removal call rejects storage route");
    mutate_storage(0x2A851BB, "changed source-success gate rejects storage route");
    mutate_storage(0x2A851CD, "destination call must resolve the same validated push function");
    mutate_storage(0x2A851A7, "drifted standalone pop-object layout rejected");
    mutate_storage(0x2A83DA6, "changed transfer mode derivation rejected");
    mutate_storage(0x26ED3B7, "drifted native source receipt stride rejected");
    mutate_storage(0x26ED3E6, "drifted source metadata-key offset rejected");
    mutate_storage(0x26ED491, "drifted canonical container lookup contract rejected");
    int32_t old_table_displacement = 0;
    std::memcpy(&old_table_displacement, image + 0x26ED3D3, sizeof(old_table_displacement));
    const int32_t invalid_displacement = 0x7FFFFFFF;
    std::memcpy(image + 0x26ED3D3, &invalid_displacement, sizeof(invalid_displacement));
    check(ScanForItemGainAccess().address == 0, "out-of-image metadata global is refused");
    std::memcpy(image + 0x26ED3D3, &old_table_displacement, sizeof(old_table_displacement));
    unsigned char storage_saved[49];
    auto* storage_duplicate = image + 0x2A852D0;
    std::memcpy(storage_saved, storage_duplicate, sizeof(storage_saved));
    std::memcpy(storage_duplicate, image + 0x2A851A4, sizeof(storage_saved));
    check(ScanForItemGainAccess().address == 0, "ambiguous standalone route rejected");
    std::memcpy(storage_duplicate, storage_saved, sizeof(storage_saved));
    auto* item_call = image + 0x26EB9F0;
    item_call[0] = 0x90;
    check(ScanForItemGainAccess().address == 0, "broken item receipt call fails closed");
    item_call[0] = 0xE8;
    auto* item_merge = image + 0xF05A548;
    item_merge[0] = 0x90;
    check(ScanForItemGainAccess().address == 0, "broken native merge contract fails closed");
    item_merge[0] = 0xE8;
    auto* item_copy = image + 0xF05A5A4;
    item_copy[0] = 0x90;
    check(ScanForItemGainAccess().address == 0, "broken native new-stack contract fails closed");
    item_copy[0] = 0xE8;
    image[0xF058FA6] = 0x20;
    check(ScanForItemGainAccess().address == 0, "drifted metadata stack field rejected");
    image[0xF058FA6] = 0x18;
    auto* caller_pattern = image + 0x26EB9DE;
    unsigned char original_duplicate[27];
    auto* item_duplicate = image + 0x26F5000;
    std::memcpy(original_duplicate, item_duplicate, sizeof(original_duplicate));
    std::memcpy(item_duplicate, caller_pattern, sizeof(original_duplicate));
    check(ScanForItemGainAccess().address == 0, "ambiguous receipt caller rejected");
    std::memcpy(item_duplicate, original_duplicate, sizeof(original_duplicate));
    check(ScanForPlayerPointerCapture().address == base + 0x2830481, "current player capture");
    if (rate != base + 0x171E55B || cost != base + 0xC4E3EAB) return 1;
    auto* signature = image + 0x171E533;
    signature[0] ^= 1;
    check(ScanForNativeResourceRateAccess() == 0, "mutated rate ABI fails closed");
    signature[0] ^= 1;
    unsigned char saved[45];
    auto* duplicate = signature + 0x400;
    std::memcpy(saved, duplicate, sizeof(saved));
    std::memcpy(duplicate, signature, sizeof(saved));
    check(ScanForNativeResourceRateAccess() == 0, "ambiguous rate ABI fails closed");
    std::memcpy(duplicate, saved, sizeof(saved));
    auto* call = image + 0xC4E3EA6;
    const auto opcode = call[0];
    call[0] = 0x90;
    check(!ValidateNativeResourceGroup(rate, cost), "broken caller/evaluator relationship rejected");
    call[0] = opcode;
    check(ValidateNativeResourceGroup(0, cost) == false, "partial hook group rejected");
    SetScannerTestModule(nullptr);
    VirtualFree(image, 0, MEM_RELEASE);
    return failures ? 1 : 0;
}
