#!/usr/bin/env python3
"""Fast PE/string/xref scanner for Crimson Desert affinity research.

This is intentionally tiny and dependency-free.  It maps RVAs to file offsets,
finds selected ASCII strings, scans executable sections for RIP-relative xrefs
to those strings, and prints nearby call targets.
"""

from __future__ import annotations

import argparse
import dataclasses
import struct
from pathlib import Path


IMAGE_BASE = 0x140000000

TARGET_STRINGS = [
    b"OnFriendlyItem_Give",
    b"OnFriendlyItem_Take",
    b"AdditionalNpcFriendlyValue",
    b"AdditionalPetFriendlyValue",
    b"AIFunction_VaryFriendly",
    b"AIFunction_VaryFriendlyWithLogout",
    b"_friendlyDataList",
    b"FriendlySaveData",
    b"FriendlyChanged",
    b"PetFriendlyReached",
    b"eErrNoInvalidFriendlyData",
    b"eErrNoCantDoUpdateFriendlyDataYet",
    b"eErrNoNotEnoughFriendly",
    b"_varyFriendly",
    b"DropFriendlyData",
    b"friendlyData",
    b"Additional Friendly",
    b"Additional Pet Friendly",
    b"TrustGain",
    b"Trust",
    b"FavoriteItem",
    b"Contribution",
    b"ExpensionInventory",
    b"MiniGame",
    b"AddPetFriendly",
    b"AddHorseExp",
    b"Item_Skill_AbyssGear_Equip_AddPetFriendly_LV1",
    b"Item_Skill_AbyssGear_Equip_AddPetFriendly_LV2",
    b"Item_Skill_AbyssGear_Equip_AddPetFriendly_LV3",
    b"Item_Skill_AbyssGear_Equip_AddHorseExp_LV1",
    b"Item_Skill_AbyssGear_Equip_AddHorseExp_LV2",
    b"Item_Skill_AbyssGear_Equip_AddHorseExp_LV3",
]


@dataclasses.dataclass(frozen=True)
class Section:
    name: str
    va: int
    vsize: int
    raw: int
    raw_size: int
    chars: int

    @property
    def is_exec(self) -> bool:
        return bool(self.chars & 0x20000000)

    @property
    def is_readable(self) -> bool:
        return bool(self.chars & 0x40000000)

    def contains_rva(self, rva: int) -> bool:
        return self.va <= rva < self.va + max(self.vsize, self.raw_size)


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def i32(data: bytes, off: int) -> int:
    return struct.unpack_from("<i", data, off)[0]


def parse_sections(data: bytes) -> list[Section]:
    e_lfanew = u32(data, 0x3C)
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise ValueError("not a PE file")

    file_header = e_lfanew + 4
    section_count = u16(data, file_header + 2)
    optional_size = u16(data, file_header + 16)
    section_table = file_header + 20 + optional_size

    sections: list[Section] = []
    for i in range(section_count):
        off = section_table + i * 40
        name = data[off:off + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        vsize = u32(data, off + 8)
        va = u32(data, off + 12)
        raw_size = u32(data, off + 16)
        raw = u32(data, off + 20)
        chars = u32(data, off + 36)
        sections.append(Section(name, va, vsize, raw, raw_size, chars))
    return sections


def rva_to_file(sections: list[Section], rva: int) -> int | None:
    for section in sections:
        if section.contains_rva(rva):
            return section.raw + (rva - section.va)
    return None


def file_to_rva(sections: list[Section], file_off: int) -> int | None:
    for section in sections:
        if section.raw <= file_off < section.raw + section.raw_size:
            return section.va + (file_off - section.raw)
    return None


def find_all(data: bytes, needle: bytes) -> list[int]:
    out: list[int] = []
    pos = 0
    while True:
        found = data.find(needle, pos)
        if found < 0:
            return out
        out.append(found)
        pos = found + 1


def scan_rip_xrefs_to_targets(data: bytes, sections: list[Section], target_rvas: set[int]) -> dict[int, list[int]]:
    refs_by_target: dict[int, list[int]] = {target_rva: [] for target_rva in target_rvas}
    for section in sections:
        if not section.is_exec:
            continue
        start = section.raw
        end = min(len(data), section.raw + section.raw_size)
        chunk = data[start:end]
        for i in range(0, max(0, len(chunk) - 4)):
            disp = i32(chunk, i)
            instr_next_rva = section.va + i + 4
            target_rva = instr_next_rva + disp
            if target_rva in refs_by_target:
                refs_by_target[target_rva].append(section.va + i)
    return refs_by_target


def scan_call_targets_near(data: bytes, sections: list[Section], ref_rva: int, window: int = 0x180) -> list[tuple[int, int]]:
    file_off = rva_to_file(sections, ref_rva)
    if file_off is None:
        return []
    start = max(0, file_off - window)
    end = min(len(data) - 5, file_off + window)
    calls: list[tuple[int, int]] = []
    for off in range(start, end):
        op = data[off]
        if op == 0xE8:
            rel = i32(data, off + 1)
            call_rva = file_to_rva(sections, off)
            if call_rva is None:
                continue
            target = call_rva + 5 + rel
            calls.append((call_rva, target))
    return calls


def dump_bytes_near(data: bytes, sections: list[Section], rva: int, before: int = 0x40, after: int = 0x80) -> str:
    file_off = rva_to_file(sections, rva)
    if file_off is None:
        return ""
    start = max(0, file_off - before)
    end = min(len(data), file_off + after)
    lines: list[str] = []
    for off in range(start, end, 16):
        row = data[off:min(end, off + 16)]
        row_rva = file_to_rva(sections, off)
        if row_rva is None:
            continue
        hex_bytes = " ".join(f"{b:02X}" for b in row)
        ascii_bytes = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
        marker = "=>" if off <= file_off < off + 16 else "  "
        lines.append(f"{marker} RVA {row_rva:08X} VA {IMAGE_BASE + row_rva:016X}: {hex_bytes:<47} {ascii_bytes}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    data = args.exe.read_bytes()
    sections = parse_sections(data)

    lines: list[str] = []
    lines.append("# Fast affinity PE route scan")
    lines.append("")
    lines.append(f"exe={args.exe}")
    lines.append(f"size={len(data)}")
    lines.append("")
    lines.append("## sections")
    for section in sections:
        lines.append(
            f"{section.name:8} rva=0x{section.va:08X} raw=0x{section.raw:08X} "
            f"vsize=0x{section.vsize:08X} raw_size=0x{section.raw_size:08X} "
            f"exec={int(section.is_exec)} read={int(section.is_readable)}"
        )

    string_hits_by_target: dict[bytes, list[tuple[int, int]]] = {}
    target_rvas: set[int] = set()

    for needle in TARGET_STRINGS:
        hits = find_all(data, needle)
        mapped_hits: list[tuple[int, int]] = []
        for file_off in hits:
            rva = file_to_rva(sections, file_off)
            if rva is not None:
                mapped_hits.append((file_off, rva))
                target_rvas.add(rva)
        string_hits_by_target[needle] = mapped_hits

    refs_by_target = scan_rip_xrefs_to_targets(data, sections, target_rvas)
    all_call_targets: dict[int, set[int]] = {}

    for needle in TARGET_STRINGS:
        lines.append("")
        lines.append(f"## {needle.decode('ascii', 'replace')}")
        mapped_hits = string_hits_by_target.get(needle, [])
        if not mapped_hits:
            lines.append("not found")
            continue

        for file_off, rva in mapped_hits:
            lines.append(f"string file=0x{file_off:X} rva=0x{rva:08X} va=0x{IMAGE_BASE + rva:016X}")
            xrefs = refs_by_target.get(rva, [])
            if not xrefs:
                lines.append("  no rip xrefs")
                continue

            for ref_rva in xrefs[:32]:
                lines.append(f"  xref rva=0x{ref_rva:08X} va=0x{IMAGE_BASE + ref_rva:016X}")
                calls = scan_call_targets_near(data, sections, ref_rva)
                if calls:
                    lines.append("    nearby direct calls:")
                for call_rva, target_rva in calls[-16:]:
                    all_call_targets.setdefault(target_rva, set()).add(ref_rva)
                    lines.append(
                        f"      call rva=0x{call_rva:08X} va=0x{IMAGE_BASE + call_rva:016X} "
                        f"target_rva=0x{target_rva:08X} target_va=0x{IMAGE_BASE + target_rva:016X}"
                    )
                lines.append("    bytes:")
                lines.append(dump_bytes_near(data, sections, ref_rva))

    lines.append("")
    lines.append("# Common nearby direct-call targets")
    for target_rva, refs in sorted(all_call_targets.items(), key=lambda item: (-len(item[1]), item[0])):
        if len(refs) < 2:
            continue
        lines.append(
            f"target_rva=0x{target_rva:08X} target_va=0x{IMAGE_BASE + target_rva:016X} "
            f"near_refs={len(refs)} refs="
            + ",".join(f"0x{r:08X}" for r in sorted(refs))
        )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
