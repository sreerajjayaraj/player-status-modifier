#include "hooks/game_hooks.h"
#include "runtime/foreground_input.h"

#include <cstdint>
#include <cstdio>

namespace {
__declspec(noinline) int AuthoredCode(int n) { return n + 7; }
volatile std::uint32_t g_authored_data = 42;
}

int main() {
    int failures = 0;
    unsigned checks = 0;
    const auto check = [&](bool valid, const char* name) {
        ++checks;
        if (!valid) ++failures;
        std::printf("%s: %s\n", valid ? "PASS" : "FAIL", name);
    };

    using namespace psm::input;
    const auto expected = reinterpret_cast<HWND>(1);
    const auto other = reinterpret_cast<HWND>(2);
    HWND foreground = expected;
    DWORD owner = 41;
    const auto window = [&] { return foreground; };
    const auto process = [&](HWND candidate, DWORD* out) {
        if (candidate != expected || !owner) return 0;
        *out = owner;
        return 101;
    };
    const auto focused = [&] { return IsForegroundOwner(expected, 41, window, process); };
    unsigned polls = 0;
    const auto key = [&] { ++polls; return true; };

    check(focused() && ReadFocusedButton(focused, key) && polls == 1,
          "foreground owner polls the input API exactly once");
    owner = 900;
    check(!focused() && !ReadFocusedButton(focused, key) && polls == 1,
          "foreign foreground PID results in zero input calls");
    owner = 0;
    check(!ReadFocusedButton(focused, key) && polls == 1,
          "unavailable window owner results in zero input calls");
    owner = 41;
    foreground = other;
    check(!ReadFocusedButton(focused, key) && polls == 1,
          "changed foreground window does not poll an earlier snapshot");
    foreground = expected;
    const auto lose_focus = [&] { foreground = other; ++polls; return true; };
    check(!ReadFocusedButton(focused, lose_focus) && polls == 2 &&
          !ReadFocusedButton(focused, key) && polls == 2,
          "focus loss discards partial sample and suppresses subsequent controller calls");
    check(!IsForegroundOwner(nullptr, 41, window, process),
          "missing foreground window cannot qualify");

    using namespace psm::game;
    const auto self = GetModuleHandleW(nullptr);
    const auto other_image = GetModuleHandleW(L"kernel32.dll");
    const auto other_code = GetProcAddress(other_image, "GetModuleHandleW");
    check(!MainModule() && !IsHost(), "inert test process cannot load PSM hooks as CrimsonDesert.exe");
    check(ExecutableSpan(self, reinterpret_cast<uintptr_t>(AuthoredCode), 1),
          "executable bytes belong to the supplied PE image");
    check(!ExecutableSpan(self, reinterpret_cast<uintptr_t>(&g_authored_data), 1),
          "writable data section refuses hook ownership");
    check(!ExecutableSpan(self, reinterpret_cast<uintptr_t>(other_code), 1),
          "another image's code refuses hook ownership");
    auto* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READ);
    check(page && !ExecutableSpan(self, reinterpret_cast<uintptr_t>(page), 1),
          "private executable page refuses hook ownership");
    if (page) VirtualFree(page, 0, MEM_RELEASE);
    check(!ExecutableSpan(self, UINTPTR_MAX - 3, 8), "overflowed span refuses hook ownership");
    std::printf("PSM input and hook ownership %u checks, %d failures\n", checks, failures);
    return failures != 0;
}
