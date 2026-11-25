#pragma once

#include "native_3446.h"

namespace psm::native_catalog {
inline size_t ManagerGlobal(const v3446::Image& image, size_t getter,
                            const v3446::Profile& profile) {
    if (!getter || !image.Exec(getter, profile.size) ||
        std::memcmp(image.data + getter, profile.code, profile.size) ||
        !image.Fragments(getter, profile)) return 0;

    constexpr unsigned char load[] = {0x48, 0x8B, 0x0D};
    const auto instruction = getter + 8;
    int32_t displacement = 0;
    if (std::memcmp(image.data + instruction, load, sizeof(load)) ||
        !image.Read(instruction + 3, displacement)) return 0;
    const auto target = static_cast<int64_t>(instruction) + 7 + displacement;
    if (target <= 0 || target % alignof(uintptr_t)) return 0;
    const auto rva = static_cast<size_t>(target);
    if (!image.Range(rva, sizeof(uintptr_t))) return 0;
    constexpr DWORD data_flags = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    for (unsigned index = 0; index < image.nt.FileHeader.NumberOfSections; ++index) {
        const auto& section = image.sections[index];
        const auto size = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
        if ((section.Characteristics & (data_flags | IMAGE_SCN_MEM_EXECUTE)) == data_flags &&
            rva >= section.VirtualAddress && rva - section.VirtualAddress <= size &&
            sizeof(uintptr_t) <= size - (rva - section.VirtualAddress)) return rva;
    }
    return 0;
}
}
