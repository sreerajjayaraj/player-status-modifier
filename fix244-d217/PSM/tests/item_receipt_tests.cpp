#include "config.h"
#include "runtime/item_receipt.h"
#include "runtime/runtime_state.h"
#include "logger.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <intrin.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <Windows.h>
#include <fstream>
#include <sstream>
#include <filesystem>

const char* DescribeItemReceiptTestScope(uintptr_t, uintptr_t, uintptr_t = 0, uintptr_t = 0, uintptr_t = 0);
int ExerciseStandaloneReceipt(uintptr_t, uintptr_t, uintptr_t, std::byte*, bool = false);
bool PrepareItemTransactionTest(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, const char**);

int TestScopeDiagnostics() {
    int failures = 0;
    alignas(16) std::array<std::byte, 0x4000> image{};
    alignas(16) std::array<std::byte, 0x200> behavior{};
    std::array<std::byte, 0xE0> source{}, destination{};
    std::array<std::byte, 0x28> pending{};
    const auto base = reinterpret_cast<uintptr_t>(image.data());
    const auto push = reinterpret_cast<uintptr_t>(behavior.data() + 0x98);
    const auto put = [](auto& bytes, size_t offset, auto value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    };
    put(image, offsetof(IMAGE_DOS_HEADER, e_lfanew), int32_t{0x100});
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data() + 0x100);
    nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(image.size());
    const auto type = [&](size_t object, uint32_t rva, const char* name) {
        put(behavior, object, base + rva);
        put(image, rva - 8, base + rva + 0x20);
        put(image, rva + 0x20, uint32_t{1});
        put(image, rva + 0x2C, rva + 0x40);
        std::memcpy(image.data() + rva + 0x50, name, std::strlen(name) + 1);
    };
    type(0, 0x800, ".?AVMoveItemBehavior_InventoryToOtherInventory@pa@@");
    type(0x30, 0x1000, ".?AVItemPopInventory@pa@@");
    type(0x98, 0x1800, ".?AVItemPushInventory@pa@@");
    put(behavior, 0x40, uintptr_t{0x10000000});
    put(behavior, 0xA8, uintptr_t{0x20000000});
    put(behavior, 0x100, uint16_t{0x18});
    put(behavior, 0x50, uint32_t{1});
    put(behavior, 0xF8, uint32_t{1});
    put(behavior, 0x48, reinterpret_cast<uintptr_t>(source.data()));
    put(behavior, 0xF0, reinterpret_cast<uintptr_t>(destination.data()));
    put(source, 8, uint16_t{42});
    put(destination, 8, uint16_t{42});
    put(source, 0x10, int64_t{1});
    put(destination, 0x10, int64_t{1});
    const auto pristine = behavior;
    const auto expect = [&](const char* reason, uintptr_t address = 0) {
        const auto actual = DescribeItemReceiptTestScope(address ? address : push, base);
        const bool ok = std::strcmp(actual, reason) == 0;
        std::printf("%s: scope reason expected=%s actual=%s\n", ok ? "PASS" : "FAIL", reason, actual);
        if (!ok) ++failures;
    };
    expect("qualified");
    put(behavior, 0, uintptr_t{0});
    expect("parent-type"); behavior = pristine;
    put(behavior, 0x30, uintptr_t{0});
    expect("pop-type"); behavior = pristine;
    put(behavior, 0x98, uintptr_t{0});
    expect("push-type"); behavior = pristine;
    put(behavior, 0xA8, uintptr_t{0});
    expect("actor-pointer"); behavior = pristine;
    put(behavior, 0xA8, uintptr_t{0x10000000});
    expect("same-actor"); behavior = pristine;
    put(behavior, 0x100, uint16_t{0x56});
    expect("transfer-mode"); behavior = pristine;
    put(behavior, 0x50, uint32_t{2});
    expect("source-count"); behavior = pristine;
    put(behavior, 0xF8, uint32_t{2});
    expect("destination-count"); behavior = pristine;
    put(behavior, 0x48, uintptr_t{0});
    expect("receipt-pointer"); behavior = pristine;
    put(destination, 8, uint16_t{43});
    expect("item-mismatch"); put(destination, 8, uint16_t{42});
    put(destination, 0x10, int64_t{0});
    expect("nonpositive-quantity"); put(destination, 0x10, int64_t{1});
    put(source, 0x10, int64_t{2});
    expect("quantity-mismatch"); put(source, 0x10, int64_t{1});
    expect("qualified");
    expect("invalid-push", 1);
    const auto before_source = source, before_destination = destination;
    expect("qualified");
    if (source != before_source || destination != before_destination || behavior != pristine) ++failures;
    behavior = {};
    type(0x98, 0x1800, ".?AVItemPushInventory@pa@@");
    type(0x118, 0x1000, ".?AVItemPopInventory@pa@@");
    put(behavior, 0xA8, uintptr_t{0x10000000});
    put(behavior, 0x128, uintptr_t{0x10000000});
    put(behavior, 0x100, uint16_t{0x37});
    put(behavior, 0x138, uint32_t{1});
    put(behavior, 0xF8, uint32_t{1});
    put(behavior, 0x130, reinterpret_cast<uintptr_t>(source.data()));
    put(behavior, 0xF0, reinterpret_cast<uintptr_t>(destination.data()));
    put(source, 0xDA, uint16_t{100});
    put(behavior, 0xE8, uint32_t{1});
    put(behavior, 0x170, uint32_t{1});
    put(behavior, 0x168, reinterpret_cast<uintptr_t>(pending.data()));
    put(pending, 0, uint16_t{42});
    put(pending, 8, int64_t{0x12345678}); // UID must never be mistaken for amount.
    put(pending, 0x18, int64_t{1});
    put(pending, 0x10, uint16_t{17});
    put(image, 0x3068, uint32_t{1});
    put(image, 0x306C, uint32_t{1});
    put(image, 0x3078, base + 0x3200);
    put(image, 0x3080, base + 0x3400);
    put(image, 0x3200, uint32_t{1});
    put(image, 0x3208, uint32_t{100});
    put(image, 0x320C, uint32_t{0});
    put(image, 0x3400, base + 0x3500);
    put(image, 0x3504, uint16_t{100});
    put(image, 0x3506, uint16_t{17});
    put(image, 0x3600, base + 0x3000);
    const auto standalone_pristine = behavior;
    const auto expect_standalone = [&](const char* reason, uintptr_t caller = 0x123456) {
        const auto actual = DescribeItemReceiptTestScope(push, base, caller, 0x123456, base + 0x3600);
        const bool ok = std::strcmp(actual, reason) == 0;
        std::printf("%s: standalone scope expected=%s actual=%s\n", ok ? "PASS" : "FAIL", reason, actual);
        if (!ok) ++failures;
    };
    expect_standalone("qualified");
    expect_standalone("parent-type", 0x123457);
    expect_standalone("parent-type", 0);
    put(behavior, 0x118, uintptr_t{0});
    expect_standalone("pop-type"); behavior = standalone_pristine;
    put(behavior, 0x138, uint32_t{2});
    expect_standalone("source-count"); behavior = standalone_pristine;
    put(behavior, 0xF8, uint32_t{2});
    expect_standalone("destination-count"); behavior = standalone_pristine;
    put(behavior, 0x100, uint16_t{0x18});
    expect_standalone("transfer-mode"); behavior = standalone_pristine;
    put(behavior, 0x100, uint16_t{0x38});
    expect_standalone("qualified"); behavior = standalone_pristine;
    put(source, 0x10, int64_t{2});
    expect_standalone("qualified"); put(source, 0x10, int64_t{1});
    put(image, 0x3504, uint16_t{99});
    expect_standalone("source-container-unresolved"); put(image, 0x3504, uint16_t{100});
    put(image, 0x3200, uint32_t{32});
    expect_standalone("source-container-unresolved"); put(image, 0x3200, uint32_t{1});
    put(image, 0x3068, uint32_t{0});
    expect_standalone("source-container-unresolved"); put(image, 0x3068, uint32_t{1});
    expect_standalone("qualified");
    failures += ExerciseStandaloneReceipt(push, base, base + 0x3600, destination.data());
    failures += ExerciseStandaloneReceipt(push, base, base + 0x3600, destination.data(), true);
    return failures;
}

bool InstallItemReceiptTestHook(void*, uintptr_t, uintptr_t);
void RemoveItemReceiptTestHook();
void SetItemReceiptTestMetadata3446(uintptr_t);
void RemoveEconomyHooks();
void SetItemReceiptTestScope(const void*, uint32_t);
void SetItemReceiptTestNativeScope(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
uintptr_t fixture_return = 0;
uint8_t fixture_flag = 0;
volatile uintptr_t fixture_result = 0;
alignas(16) std::array<std::byte, 0x500> item_info{};
std::atomic<bool> pause_lookup{false}, lookup_entered{false}, resume_lookup{false};
uint32_t fixture_receipt_count = 1;
bool fixture_hook_installed = false;
uintptr_t fixture_push = 0, fixture_image = 0, fixture_table = 0;
uintptr_t fixture_push_caller = 0x123456;

__declspec(noinline) uintptr_t __fastcall FixtureLookup(const uint16_t*) {
    if (pause_lookup.exchange(false)) {
        lookup_entered.store(true);
        while (!resume_lookup.load()) std::this_thread::yield();
    }
    return reinterpret_cast<uintptr_t>(item_info.data());
}

__declspec(noinline) uintptr_t __fastcall FixtureLookup3446(uintptr_t unused, uint32_t key) {
    return unused == 0 && key != 0xFFFF ? FixtureLookup(nullptr) : 0;
}

uintptr_t __fastcall FixtureContainerMap(uint16_t* output, uint16_t input, uint16_t variant) {
    *output = variant == 0xBEEF ? input : uint16_t{0xFFFF};
    return reinterpret_cast<uintptr_t>(output);
}

__declspec(noinline) uintptr_t __fastcall NativeFixture(uintptr_t container, uint32_t* error,
                                                       const void* input, uint16_t slot, uint8_t flag) {
    fixture_return = reinterpret_cast<uintptr_t>(_ReturnAddress());
    fixture_flag = flag;
    auto* destination = *reinterpret_cast<psm::items::Receipt**>(container) + slot;
    const auto* receipt = static_cast<const psm::items::Receipt*>(input);
    if (psm::items::Quantity(*destination) > 0) {
        const auto sum = psm::items::Quantity(*destination) + psm::items::Quantity(*receipt);
        std::memcpy(destination->data() + 0x10, &sum, sizeof(sum));
    } else {
        *destination = *receipt;
    }
    *error = 0;
    return reinterpret_cast<uintptr_t>(error);
}

__declspec(noinline) void FixtureCaller(uintptr_t container, uint32_t* error, const void* input) {
    if (fixture_hook_installed) {
        if (fixture_push) SetItemReceiptTestNativeScope(fixture_push, fixture_image, fixture_push_caller, 0x123456, fixture_table);
        else SetItemReceiptTestScope(input, fixture_receipt_count);
    }
    // Prevent tail-call conversion: the guarded return address must belong here.
    fixture_result = NativeFixture(container, error, input, 0, 0xA5);
    _ReadWriteBarrier();
}

int ExerciseStandaloneReceipt(uintptr_t push, uintptr_t image, uintptr_t table, std::byte* input, bool compact) {
    using namespace psm::items;
    int failures = 0;
    const std::size_t stack_offset=compact?0x10:0x18, weighted_offset=compact?0x1D:0x3F0;
    std::printf("Storage transaction metadata profile: %s\n",compact?"3446-key-EDX":"legacy-pointer-RCX");
    const auto check = [&](bool ok, const char* name) {
        std::printf("%s: standalone production callback %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    const auto put = [](void* address, auto value) { std::memcpy(address, &value, sizeof(value)); };
    ModConfig config{};
    config.general.enabled = true;
    config.items.gain_multiplier = 3;
    g_runtime_enabled.store(true);
    SetConfigSnapshot(L"", config);
    Receipt destination{};
    std::memcpy(destination.data(), input, destination.size());
    put(destination.data() + 0x10, int64_t{942});
    alignas(16) std::array<std::byte, 0x40> container{};
    put(container.data(), destination.data());
    put(container.data() + 8, int16_t{1});
    put(container.data() + 0x10, uint16_t{0});
    put(item_info.data() + stack_offset, int64_t{10000});
    uint32_t error = 99;
    const auto address = reinterpret_cast<uintptr_t>(container.data());
    FixtureCaller(address, &error, input);
    const bool installed = InstallItemReceiptTestHook(reinterpret_cast<void*>(NativeFixture),
        fixture_return, reinterpret_cast<uintptr_t>(FixtureLookup));
    check(installed, "installs native receiver fixture");
    if (!installed) return failures;
    if(compact) SetItemReceiptTestMetadata3446(reinterpret_cast<uintptr_t>(FixtureLookup3446));
    fixture_hook_installed = true;
    fixture_push = push; fixture_image = image; fixture_table = table;
    alignas(16) std::array<std::byte, 0x120> owner{};
    uintptr_t container_entry = address;
    put(owner.data() + 0x18, &container_entry);
    put(owner.data() + 0x20, uint32_t{1});
    const auto pending = *reinterpret_cast<std::array<std::byte, 0x28>**>(push + 0xD0);
    const auto pending_original = *pending;
    const auto prepare = [&](uintptr_t caller = 0x123456) {
        const char* reason = nullptr;
        const bool ok = PrepareItemTransactionTest(push, reinterpret_cast<uintptr_t>(owner.data()),
            image, caller, reinterpret_cast<uintptr_t>(FixtureContainerMap), &reason);
        std::printf("plan fixture: %s\n", reason);
        return ok;
    };
    put(destination.data() + 0x10, int64_t{942});
    put(input + 0xCA, uint16_t{0xBEEF});
    const auto original_request = *reinterpret_cast<const Receipt*>(input);
    void* obsolete_debt = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    check(obsolete_debt != nullptr, "guard page for obsolete owner debt fields");
    put(owner.data() + 0xD8, obsolete_debt); put(owner.data() + 0xE0, uint32_t{1});
    check(prepare(), "current 3462 owner debt layout ignores unrelated D8/E0 guard data");
    put(owner.data() + 0xD8, uintptr_t{0}); put(owner.data() + 0xE0, uint32_t{0});
    if (obsolete_debt) VirtualFree(obsolete_debt, 0, MEM_RELEASE);
    put(input + 0x10, int64_t{1});
    check(prepare(), "scales destination before SQL rows are serialized");
    check(Quantity(destination) == 942 && *pending == pending_original,
          "preparation alone does not write inventory or remove source items");
    const auto sql_delta = Quantity(*reinterpret_cast<const Receipt*>(input));
    check(!prepare(), "repeated planning callback cannot apply x3 twice");
    FixtureCaller(address, &error, input);
    check(Quantity(destination) == 945, "matches iron-ore case: 942 + (1 x 3) = 945");
    check(Quantity(*reinterpret_cast<const Receipt*>(input)) == 3,
          "transaction and client acknowledgement carry the same delivered delta as native inventory");
    auto metadata = *reinterpret_cast<const Receipt*>(input);
    put(metadata.data() + 0x10, int64_t{1});
    check(original_request == metadata && *pending == pending_original, "only destination quantity changed, source request and metadata intact");
    const auto client_delta = Quantity(*reinterpret_cast<const Receipt*>(input));
    check(sql_delta == 3 && client_delta == 3 && 942 + client_delta == Quantity(destination),
          "SQL, native write, and client delta agree");
    put(input + 0x10, int64_t{1});
    put(container.data() + 0x10, uint16_t{17});
    put(input + 0xC8, uint16_t{17});
    check(!prepare(), "same container cannot create extra items");
    FixtureCaller(address, &error, input);
    check(Quantity(destination) == 946, "same actor and canonical container are not multiplied");
    put(container.data() + 0x10, uint16_t{0});
    put(input + 0xC8, uint16_t{0});
    fixture_push_caller = 0x123457;
    check(!prepare(0x123457), "wrong planning caller is rejected");
    FixtureCaller(address, &error, input);
    check(Quantity(destination) == 947, "unrelated outer push caller is not multiplied");
    fixture_push_caller = 0x123456;
    config.items.gain_multiplier = 2;
    SetConfigSnapshot(L"", config);
    check(prepare(), "next transaction uses changed live config");
    FixtureCaller(address, &error, input);
    check(Quantity(destination) == 949, "uses changed multiplier without reinstalling hooks");
    destination = {};
    put(input + 0x10, int64_t{1});
    check(prepare(), "new-stack transaction gets same coherent bonus");
    FixtureCaller(address, &error, input);
    check(Quantity(destination) == 2, "new destination stack also gets the bonus");
    check(error == 0 && fixture_flag == 0xA5, "preserves native status and fifth argument");
    put(input + 0x10, int64_t{1});
    const auto decline = [&](const char* name) {
        const auto saved_input = *reinterpret_cast<const std::array<std::byte, 0xE0>*>(input);
        check(!prepare() && saved_input == *reinterpret_cast<const std::array<std::byte, 0xE0>*>(input), name);
    };
    put(reinterpret_cast<void*>(push + 0xD8), uint32_t{2});
    decline("batch source rejected without mutation");
    put(reinterpret_cast<void*>(push + 0xD8), uint32_t{1});
    put(reinterpret_cast<void*>(push + 0x60), uint32_t{2});
    decline("split destination plan rejected without mutation");
    put(reinterpret_cast<void*>(push + 0x60), uint32_t{1});
    put(reinterpret_cast<void*>(push + 0xD0), input);
    decline("aliased source and destination records rejected");
    put(reinterpret_cast<void*>(push + 0xD0), pending);
    put(pending->data(), uint16_t{43}); decline("item mismatch rejected");
    *pending = pending_original;
    put(pending->data() + 0x18, int64_t{2}); decline("quantity mismatch rejected");
    *pending = pending_original;
    item_info[weighted_offset] = std::byte{1}; decline("weighted item rejected"); item_info[weighted_offset] = std::byte{0};
    put(item_info.data() + stack_offset, int64_t{1}); decline("singleton rejected");
    put(item_info.data() + stack_offset, int64_t{100});
    put(destination.data() + 0x10, int64_t{99}); decline("extra occupancy rejected before SQL/source pop");
    put(destination.data() + 0x10, int64_t{2});
    std::array<std::byte, 0x10> debt{};
    put(owner.data() + 0xF8, debt.data()); put(owner.data() + 0x100, uint32_t{1});
    decline("matching current-3462 pending item debt rejected"); put(owner.data() + 0x100, uint32_t{0});
    config.general.enabled = false; SetConfigSnapshot(L"", config); decline("disabled config changes no transaction");
    config.general.enabled = true; config.items.gain_multiplier = 3; SetConfigSnapshot(L"", config);
    put(item_info.data() + stack_offset, int64_t{10000});
    int64_t source_total = 53, sql_total = 946, client_total = 946;
    put(destination.data() + 0x10, int64_t{946});
    for (int i = 0; i < 3; ++i) {
        put(input + 0x10, int64_t{1});
        check(prepare(), "video reproduction withdrawal planned");
        sql_total += Quantity(*reinterpret_cast<const Receipt*>(input));
        source_total -= 1;
        FixtureCaller(address, &error, input);
        client_total += Quantity(*reinterpret_cast<const Receipt*>(input));
    }
    check(source_total == 50 && sql_total == 955 && client_total == 955 && Quantity(destination) == 955,
          "video regression: 3 withdrawals remove 3 and deliver 9 across all representations");
    put(input + 0x10, int64_t{1});
    put(pending->data() + 0x10, uint16_t{0});
    put(reinterpret_cast<void*>(image + 0x3506), uint16_t{0});
    put(container.data() + 0x10, uint16_t{17});
    put(input + 0xC8, uint16_t{17});
    put(destination.data() + 0x10, int64_t{50});
    check(prepare(), "reverse direction deposit is qualified");
    FixtureCaller(address, &error, input);
    check(Quantity(destination) == 53 && Quantity(*reinterpret_cast<const Receipt*>(input)) == 3,
          "one-item deposit delivers three to storage with the same client delta");
    *pending = pending_original;
    put(reinterpret_cast<void*>(image + 0x3506), uint16_t{17});
    put(input + 0xC8, uint16_t{0});
    put(input + 0x10, int64_t{1});
    RemoveItemReceiptTestHook();
    fixture_hook_installed = false;
    fixture_push = fixture_image = fixture_table = 0;
    return failures;
}

int TestBorrowedItemPlans();
int RunMenuItemGainTests();
int main() {
    using namespace psm::items;
    int failures = TestScopeDiagnostics() + TestBorrowedItemPlans();
    const auto check = [&failures](bool ok, const char* what) {
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };
    const auto log_path = std::filesystem::current_path() /
        (L"item-diagnostics-test-" + std::to_wstring(GetCurrentProcessId()) + L".log");
    check(InitializeLogger(log_path.wstring(), true, false, 1000), "diagnostic fixture log opens");
    ModConfig config{};
    config.general.enabled = true;
    config.items.gain_multiplier = 1.0;
    check(ShouldInstallItemGainHook(config), "neutral startup retains item hooks for live enable");
    const auto neutral_hooks = ShouldInstallItemGainHook(config);
    for (double multiplier : {3.0, 1.0, 2.0, 1.0}) {
        config.items.gain_multiplier = multiplier;
        check(ShouldInstallItemGainHook(config) == neutral_hooks,
              "live multiplier changes never change item hook requirements");
    }
    config.items.gain_multiplier = 3.0;
    g_runtime_enabled.store(true);
    SetConfigSnapshot(L"", config);
    Receipt source{}, receipt{};
    for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<std::byte>(i);
    const auto set_quantity = [&source](int64_t value) {
        std::memcpy(source.data() + 0x10, &value, sizeof(value));
    };
    set_quantity(5);
    const auto untouched = source;
    check(PrepareReceipt(source, 40, receipt) && Quantity(receipt) == 15, "existing-stack receipt uses x3");
    check(source == untouched, "source/request/rollback quantity is untouched");
    auto restored = receipt;
    std::memcpy(restored.data() + 0x10, source.data() + 0x10, sizeof(int64_t));
    check(restored == source, "all item metadata and borrowed pointers remain byte-identical");
    check(PrepareReceipt(source, 0, receipt) && Quantity(receipt) == 15, "new-stack receipt uses x3");
    for (double multiplier : {1.0, 2.0, 0.5, 3.0}) {
        config.items.gain_multiplier = multiplier;
        SetConfigSnapshot(L"", config);
        PrepareReceipt(source, 10, receipt);
        check(Quantity(receipt) == static_cast<int64_t>(5 * multiplier), "next receipt observes changed live config");
    }
    config.general.enabled = false;
    SetConfigSnapshot(L"", config);
    check(!PrepareReceipt(source, 10, receipt) && receipt == source, "General disabled passes native receipt through");
    config.general.enabled = true;
    SetConfigSnapshot(L"", config);
    g_runtime_enabled.store(false);
    check(!PrepareReceipt(source, 10, receipt) && receipt == source, "runtime disabled passes native receipt through");
    g_runtime_enabled.store(true);
    for (int64_t amount : {0LL, -1LL, -100LL}) {
        set_quantity(amount);
        check(!PrepareReceipt(source, 10, receipt) && receipt == source, "zero and negative amounts never multiply");
    }
    set_quantity(std::numeric_limits<int64_t>::max() / 2);
    check(!PrepareReceipt(source, 10, receipt) && receipt == source, "unsafe target sum is not sent to native merge");
    set_quantity(5);
    check(!PrepareReceipt(source, -1, receipt), "invalid destination quantity fails closed");
    check(FitsPlannedOccupancy(40, 5, 15, 100), "bonus fits already-planned occupancy");
    check(!FitsPlannedOccupancy(90, 5, 15, 100), "bonus requiring extra planned slots is refused");
    check(!FitsPlannedOccupancy(0, 1, 3, 1), "singleton is not duplicated");
    check(!FitsPlannedOccupancy(0, 5, 2, 100), "post-pop receipt is never reduced below source withdrawal");

    alignas(16) std::array<std::byte, 0x40> container{};
    Receipt destination = source;
    auto* destination_ptr = &destination;
    std::memcpy(container.data(), &destination_ptr, sizeof(destination_ptr));
    const int16_t one = 1;
    std::memcpy(container.data() + 8, &one, sizeof(one));
    const int64_t stack_size = 100;
    std::memcpy(item_info.data() + 0x18, &stack_size, sizeof(stack_size));
    uint32_t error = 99;
    const auto container_address = reinterpret_cast<uintptr_t>(container.data());
    FixtureCaller(container_address, &error, source.data());
    const auto caller_return = fixture_return;
    const bool installed = InstallItemReceiptTestHook(reinterpret_cast<void*>(NativeFixture),
        caller_return, reinterpret_cast<uintptr_t>(FixtureLookup));
    check(installed, "real SafetyHook ABI fixture installs outside the game");
    if (installed) {
        fixture_hook_installed = true;
        destination = source;
        const auto original_source = source;
        FixtureCaller(container_address, &error, source.data());
        check(Quantity(destination) == 10, "receiver observer never applies an unplanned late bonus");
        check(source == original_source, "production callback preserves original source receipt");
        check(error == 0 && fixture_result == reinterpret_cast<uintptr_t>(&error) && fixture_flag == 0xA5,
              "error pointer, return value, and fifth ABI argument are preserved");
        destination = {};
        FixtureCaller(container_address, &error, source.data());
        check(Quantity(destination) == 5, "receiver observer preserves new-stack transaction quantity");
        config.items.gain_multiplier = 2;
        SetConfigSnapshot(L"", config);
        FixtureCaller(container_address, &error, source.data());
        check(Quantity(destination) == 10, "config change cannot alter an already planned receipt at receiver");
        NativeFixture(container_address, &error, source.data(), 0, 0x42);
        check(Quantity(destination) == 15, "unrelated caller is never multiplied");
        fixture_receipt_count = 2;
        FixtureCaller(container_address, &error, source.data());
        check(Quantity(destination) == 20, "batch is passed through instead of exceeding the original plan");
        fixture_receipt_count = 1;
        item_info[0x3F0] = std::byte{1};
        FixtureCaller(container_address, &error, source.data());
        item_info[0x3F0] = std::byte{0};
        destination = source;
        pause_lookup.store(true);
        std::thread in_flight([&] { FixtureCaller(container_address, &error, source.data()); });
        for (int wait = 0; !lookup_entered.load() && wait < 5000; ++wait)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(lookup_entered.load(), "callback is paused before calling native receiver");
        RemoveEconomyHooks();
        resume_lookup.store(true);
        in_flight.join();
        check(Quantity(destination) == 10 && fixture_result == reinterpret_cast<uintptr_t>(&error),
              "in-flight native call survives concurrent config hook disable");
        FixtureCaller(container_address, &error, source.data());
        check(Quantity(destination) == 15, "disabled hook resumes native behavior");
        RemoveItemReceiptTestHook();
    }
    failures += RunMenuItemGainTests();
    ShutdownLogger();
    std::ifstream log_stream(log_path);
    std::ostringstream log_buffer;
    log_buffer << log_stream.rdbuf();
    const auto diagnostic_text = log_buffer.str();
    check(diagnostic_text.find("requested=5 delivered=5 scaled=0 before=5 before-read=1 after=10 after-read=1") != std::string::npos,
          "receiver log is observation, not proof of client or database delivery");
    check(diagnostic_text.find("requested=5 delivered=5 scaled=0 before=15 before-read=1 after=20 after-read=1") != std::string::npos,
          "emitted rejected-event log retains actual native quantities");
    check(diagnostic_text.find("reason=weighted-item") != std::string::npos,
          "metadata refusal names its actual guard");
    std::printf("item receipt: %d failures\n", failures);
    return failures ? 1 : 0;
}
