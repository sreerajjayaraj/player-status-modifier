#!/usr/bin/env python3
import argparse
import csv
import json
import struct
from pathlib import Path


ENTRY_BASE_SIZE = 68


def read_u8(buf, pos):
    return buf[pos], pos + 1


def read_u16(buf, pos):
    return struct.unpack_from("<H", buf, pos)[0], pos + 2


def read_u32(buf, pos):
    return struct.unpack_from("<I", buf, pos)[0], pos + 4


def read_u64(buf, pos):
    return struct.unpack_from("<Q", buf, pos)[0], pos + 8


def parse_entry(body, pos):
    start = pos
    flag, pos = read_u8(body, pos)
    item_key, pos = read_u32(body, pos)
    unk3, pos = read_u32(body, pos)
    unk4, pos = read_u32(body, pos)
    unk1_flag = bytes(body[pos:pos + 5])
    pos += 5
    unk_cond_flag, pos = read_u32(body, pos)
    unk_post_cond, pos = read_u32(body, pos)
    rates, pos = read_u64(body, pos)
    rates_100, pos = read_u64(body, pos)
    unk2, pos = read_u32(body, pos)
    max_amt, pos = read_u64(body, pos)
    min_amt, pos = read_u64(body, pos)
    unk3_flags, pos = read_u16(body, pos)
    item_key_dup, pos = read_u32(body, pos)
    extra = b""
    friendly_offset = None
    friendly_values = None
    if unk4 == 13:
        extra = bytes(body[pos:pos + 1])
        pos += 1
    elif unk4 == 10:
        extra = bytes(body[pos:pos + 4])
        pos += 4
    elif unk4 == 7:
        friendly_offset = pos
        extra = bytes(body[pos:pos + 28])
        friendly_values = list(struct.unpack_from("<7I", body, pos))
        pos += 28

    return {
        "offset": start,
        "size": pos - start,
        "flag": flag,
        "item_key": item_key,
        "unk3": unk3,
        "unk4": unk4,
        "unk1_flag": unk1_flag.hex(),
        "unk_cond_flag": unk_cond_flag,
        "unk_post_cond": unk_post_cond,
        "rates": rates,
        "rates_100": rates_100,
        "unk2": unk2,
        "max_amt": max_amt,
        "min_amt": min_amt,
        "unk3_flags": unk3_flags,
        "item_key_dup": item_key_dup,
        "extra": extra,
        "friendly_offset": friendly_offset,
        "friendly_values": friendly_values,
    }, pos


def parse_dropset(body, key, offset):
    pos = offset
    rec_key, pos = read_u32(body, pos)
    name_len, pos = read_u32(body, pos)
    name = body[pos:pos + name_len].decode("ascii", errors="replace")
    pos += name_len
    is_blocked, pos = read_u8(body, pos)
    drop_roll_type, pos = read_u8(body, pos)
    drop_roll_count, pos = read_u32(body, pos)
    dcs_len, pos = read_u32(body, pos)
    drop_condition_string = ""
    if dcs_len:
        drop_condition_string = body[pos:pos + dcs_len].decode("ascii", errors="replace")
        pos += dcs_len
    drop_tag_name_hash, pos = read_u32(body, pos)
    drop_count, pos = read_u32(body, pos)
    drops = []
    for index in range(drop_count):
        drop, pos = parse_entry(body, pos)
        drop["index"] = index
        drops.append(drop)
    return {
        "key": rec_key,
        "header_key": key,
        "body_offset": offset,
        "name": name,
        "is_blocked": is_blocked,
        "drop_roll_type": drop_roll_type,
        "drop_roll_count": drop_roll_count,
        "drop_condition_string": drop_condition_string,
        "drop_tag_name_hash": drop_tag_name_hash,
        "drop_count": drop_count,
        "drops": drops,
    }


def load_records(header):
    count = struct.unpack_from("<H", header, 0)[0]
    records = []
    for i in range(count):
        pos = 2 + i * 8
        records.append(struct.unpack_from("<II", header, pos))
    return records


def row_from_drop(ds, drop):
    values = drop["friendly_values"] or []
    friendly_rel = None
    if drop["friendly_offset"] is not None:
        friendly_rel = drop["friendly_offset"] - ds["body_offset"]
    return {
        "set_key": ds["key"],
        "name": ds["name"],
        "drop_index": drop["index"],
        "drop_offset": drop["offset"],
        "drop_rel": drop["offset"] - ds["body_offset"],
        "friendly_offset": drop["friendly_offset"],
        "friendly_rel": friendly_rel,
        "item_key": drop["item_key"],
        "item_key_dup": drop["item_key_dup"],
        "flag": drop["flag"],
        "unk3": drop["unk3"],
        "unk4": drop["unk4"],
        "unk_cond_flag": drop["unk_cond_flag"],
        "unk_post_cond": drop["unk_post_cond"],
        "rates": drop["rates"],
        "rates_100": drop["rates_100"],
        "max_amt": drop["max_amt"],
        "min_amt": drop["min_amt"],
        "friendly_values": values,
        "friendly_hex": drop["extra"].hex(),
    }


def amount_like(row):
    amounts = {5, 10, 15, 20, 25, 50, 100}
    return int(row["max_amt"]) in amounts or int(row["min_amt"]) in amounts


def interesting(row):
    name = row["name"].lower()
    value_set = set(row["friendly_values"])
    tokens = [
        "friendly",
        "friend",
        "favor",
        "favour",
        "pet",
        "animal",
        "donate",
        "gift",
        "talk",
        "greet",
        "favor",
        "contribution",
        "favorite",
        "minigame",
        "horse",
        "wolf",
        "bear",
        "trust",
    ]
    return (
        any(token in name for token in tokens)
        or amount_like(row)
        or bool(value_set & {1, 2, 3, 5, 10, 15, 20, 25, 50, 100})
    )


def interesting_non_friendly(row):
    name = row["name"].lower()
    tokens = [
        "friendly",
        "friend",
        "favor",
        "favour",
        "pet",
        "animal",
        "donate",
        "gift",
        "talk",
        "greet",
        "contribution",
        "favorite",
        "minigame",
        "trust",
    ]
    return any(token in name for token in tokens) or amount_like(row)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pabgh", required=True)
    parser.add_argument("--pabgb", required=True)
    parser.add_argument("--out-prefix", required=True)
    parser.add_argument("--include-non-friendly", action="store_true")
    args = parser.parse_args()

    header = Path(args.pabgh).read_bytes()
    body = Path(args.pabgb).read_bytes()
    records = load_records(header)

    rows = []
    for key, offset in records:
        try:
            ds = parse_dropset(body, key, offset)
        except Exception as exc:
            rows.append({
                "set_key": key,
                "name": f"PARSE_ERROR:{exc}",
                "drop_index": -1,
                "drop_offset": offset,
                "drop_rel": 0,
                "friendly_offset": None,
                "friendly_rel": None,
                "item_key": 0,
                "item_key_dup": 0,
                "flag": 0,
                "unk3": 0,
                "unk4": 0,
                "unk_cond_flag": 0,
                "unk_post_cond": 0,
                "rates": 0,
                "rates_100": 0,
                "max_amt": 0,
                "min_amt": 0,
                "friendly_values": [],
                "friendly_hex": "",
            })
            continue
        for drop in ds["drops"]:
            row = row_from_drop(ds, drop)
            if drop["unk4"] == 7:
                if interesting(row):
                    rows.append(row)
            elif args.include_non_friendly and interesting_non_friendly(row):
                rows.append(row)

    out_prefix = Path(args.out_prefix)
    out_prefix.parent.mkdir(parents=True, exist_ok=True)

    json_rows = []
    for row in rows:
        out = dict(row)
        out["friendly_values"] = list(row["friendly_values"])
        json_rows.append(out)
    out_prefix.with_suffix(".json").write_text(json.dumps(json_rows, indent=2), encoding="utf-8")

    columns = [
        "set_key", "name", "drop_index", "drop_offset", "drop_rel",
        "friendly_offset", "friendly_rel", "item_key", "item_key_dup",
        "flag", "unk3", "unk4", "unk_cond_flag", "unk_post_cond",
        "rates", "rates_100", "max_amt", "min_amt", "friendly_values", "friendly_hex",
    ]
    with out_prefix.with_suffix(".csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            out = dict(row)
            out["friendly_values"] = " ".join(str(v) for v in row["friendly_values"])
            writer.writerow(out)

    print(f"records={len(records)} interesting_friendly_rows={len(rows)}")
    for row in rows[:80]:
        print(
            f"{row['set_key']} {row['name']} drop={row['drop_index']} "
            f"rel={row['friendly_rel']} vals={row['friendly_values']} "
            f"hex={row['friendly_hex']}"
        )


if __name__ == "__main__":
    main()
