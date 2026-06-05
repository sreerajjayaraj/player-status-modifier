#!/usr/bin/env python3
import argparse
import json
import struct
from pathlib import Path


TARGET_VALUE = 100
PATCHABLE_VALUES = {5, 25, 50}
MAX_AMOUNT_DELTA = 46
MIN_AMOUNT_DELTA = 54


def le32(value):
    return struct.pack("<I", value).hex().upper()


def known_entry_rel_offset(name, field_name):
    known = {
        ("DropSet_Friendly_Talk", "max_amt"): 64,
        ("DropSet_Friendly_Talk", "min_amt"): 72,
        ("DropSet_Friendly_Donate", "max_amt"): 64,
        ("DropSet_Friendly_Donate", "min_amt"): 72,
    }
    return known.get((name, field_name))


def display_name(row):
    name = row.get("name") or ""
    if name:
        return name
    return f"UnnamedFriendly_{row['set_key']}_item_{row['item_key']}"


def build_change(row, field_name, field_delta):
    original_value = int(row[field_name])
    absolute_offset = int(row["drop_offset"]) + field_delta
    name = row.get("name") or ""

    change = {
        "key": int(row["set_key"]),
        "offset": absolute_offset,
        "original": le32(original_value),
        "patched": le32(TARGET_VALUE),
        "label": (
            f"[PlayerStatusModifier][Affinity][BroadFriendly] "
            f"{display_name(row)} {field_name} {original_value} -> {TARGET_VALUE}"
        ),
    }
    rel_offset = known_entry_rel_offset(name, field_name)
    if name and rel_offset is not None:
        change["entry"] = name
        change["rel_offset"] = rel_offset
    else:
        change["entry_hint"] = display_name(row)
        change["note"] = "Unnamed dropset row; absolute v1.05.01 offset patch."
    return change


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidates", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    rows = json.loads(Path(args.candidates).read_text(encoding="utf-8"))
    changes = []
    patched_rows = 0
    skipped_rows = 0

    for row in rows:
        max_amt = int(row["max_amt"])
        min_amt = int(row["min_amt"])
        if max_amt not in PATCHABLE_VALUES or min_amt not in PATCHABLE_VALUES:
            skipped_rows += 1
            continue
        if max_amt != min_amt:
            skipped_rows += 1
            continue
        patched_rows += 1
        changes.append(build_change(row, "max_amt", MAX_AMOUNT_DELTA))
        changes.append(build_change(row, "min_amt", MIN_AMOUNT_DELTA))

    doc = {
        "name": "Player Status Modifier - Affinity 100 Broad Friendly Diagnostic",
        "version": "1.05.01-fix39-data",
        "description": (
            "Experimental companion DMM data patch for relationship testing. "
            "Patches every positive dropset friendly amount candidate found in "
            "v1.05.01 from 5, 25, or 50 to 100, including the proven greeting "
            "and donation rows plus unnamed friendly rows that may cover gifts "
            "or character-specific relationship rewards. It intentionally does "
            "not include the failed pet abyss-gear 95 route; petting may still "
            "need a runtime hook if this diagnostic does not change it."
        ),
        "author": "Codex + NattKh workflow",
        "format": 2,
        "patches": [
            {
                "game_file": "gamedata/dropsetinfo.pabgb",
                "changes": changes,
            }
        ],
        "diagnostic": {
            "source_candidates": str(Path(args.candidates).as_posix()),
            "patched_rows": patched_rows,
            "skipped_rows": skipped_rows,
            "patched_values": sorted(PATCHABLE_VALUES),
            "target_value": TARGET_VALUE,
        },
    }

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {output} rows={patched_rows} changes={len(changes)} skipped={skipped_rows}")


if __name__ == "__main__":
    main()
