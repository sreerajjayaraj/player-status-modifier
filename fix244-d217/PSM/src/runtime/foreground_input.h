#pragma once

#include <Windows.h>

namespace psm::input {

template<class Foreground, class Owner>
bool IsForegroundOwner(HWND window, DWORD process, Foreground foreground, Owner owner) noexcept {
    if (!window || !process) return false;
    DWORD pid = 0;
    return owner(window, &pid) && pid == process && foreground() == window;
}

template<class Active, class Poll>
bool ReadFocusedButton(Active active, Poll poll) noexcept {
    return active() && poll() && active();
}

class ForegroundInput {
public:
    ForegroundInput() noexcept : window_(GetForegroundWindow()) {}

    bool Active() const noexcept {
        return IsForegroundOwner(window_, GetCurrentProcessId(), GetForegroundWindow,
                                 GetWindowThreadProcessId);
    }

    template<class Poll>
    bool ReadButton(Poll poll) const noexcept {
        return ReadFocusedButton([this] { return Active(); }, poll);
    }

private:
    HWND window_;
};

}  // namespace psm::input
