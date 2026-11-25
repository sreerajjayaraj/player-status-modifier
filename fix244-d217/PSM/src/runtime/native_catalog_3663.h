#pragma once

#include <cstddef>
#include <cstdint>
#include "runtime/runtime_state.h"

namespace psm::native_catalog {
template<class Read>
bool ReadRow(const Read& read, uintptr_t global, uint16_t index, uintptr_t& manager,
             uint32_t& count, uintptr_t& records, uintptr_t& row) {
    // Each typed manager stores an array of 24-byte cache records. A
    // record with an empty row pointer is still loading. Leave it alone.
    const size_t row_pointer = static_cast<size_t>(index) * 24 + 8;
    return read(global, 0, &manager) && read(manager, 0x28, &count) &&
        index < count && read(manager, 0x20, &records) &&
        read(records, row_pointer, &row) && row >= kMinimumPointerAddress;
}
}
