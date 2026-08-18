#!/usr/bin/env python3
"""Validate planned linker regions without accepting unresolved state as release-valid."""
import argparse
import json
import re
from pathlib import Path

from generate_tmc_memory_map import ASSIGNMENT, REGIONS, linker_addresses, manifest_entries, manifest_hash

def fail(message):
    raise ValueError(message)

SECTION = re.compile(r"^\s*(ewram|iwram)\s*\(NOLOAD\)")

def linker_symbols_by_region(path):
    symbols = {region: {} for region in REGIONS}
    region = None
    for line in path.read_text().splitlines():
        section = SECTION.match(line)
        if section:
            region = section.group(1)
        for offset, name in ASSIGNMENT.findall(line):
            if region in symbols:
                symbols[region][name] = int(offset, 16)
    return symbols

def validate(source, addresses, linker_symbols, strict=False):
    entries = manifest_entries(source, addresses)
    by_name = {}
    for entry in entries:
        name, status, size = entry["name"], entry.get("status"), entry.get("size")
        if name in by_name:
            fail(f"duplicate symbol: {name}")
        by_name[name] = entry
        if status not in ("reviewed", "unresolved"):
            fail(f"{name}: status must be reviewed or unresolved")
        if status == "reviewed" and (not isinstance(size, int) or size <= 0):
            fail(f"{name}: reviewed size must be positive")
        if status == "unresolved" and size is not None:
            fail(f"{name}: unresolved size must be null")
        base, length, _ = REGIONS[entry["region"]]
        if not base <= entry["gba_virtual"] < base + length:
            fail(f"{name}: virtual address outside declared region")
        if status == "reviewed" and entry["gba_virtual"] + size > base + length:
            fail(f"{name}: reviewed range extends outside {entry['region']}")
        end_symbol = entry.get("end_symbol")
        if end_symbol and (end_symbol not in addresses or base + addresses[end_symbol] != entry["gba_virtual"] + size):
            fail(f"{name}: false end linker symbol {end_symbol}")
    for entry in entries:
        alias = entry.get("alias_of")
        overlap = entry.get("overlap")
        if alias:
            if alias not in by_name or entry["gba_virtual"] != by_name[alias]["gba_virtual"]:
                fail(f"{entry['name']}: false alias target")
        if overlap:
            if overlap not in by_name:
                fail(f"{entry['name']}: nonexistent overlap target {overlap}")
            target = by_name[overlap]
            if target["status"] != "reviewed":
                fail(f"{entry['name']}: overlap target {overlap} is unresolved")
            if not target["gba_virtual"] <= entry["gba_virtual"] < target["gba_virtual"] + target["size"]:
                fail(f"{entry['name']}: false overlap target {overlap}")
    for entry in entries:
        if entry["status"] != "reviewed":
            continue
        current = addresses[entry["name"]]
        next_symbols = [(offset, name) for name, offset in linker_symbols[entry["region"]].items()
                        if offset > current]
        if not next_symbols:
            continue
        next_offset, next_name = min(next_symbols)
        if current + entry["size"] <= next_offset:
            continue
        next_entry = by_name.get(next_name)
        if next_entry and next_entry.get("overlap") == entry["name"] and \
           entry["gba_virtual"] <= next_entry["gba_virtual"] < entry["gba_virtual"] + entry["size"]:
            continue
        fail(f"{entry['name']}: reviewed range crosses next linker symbol {next_name}")
    reviewed = [entry for entry in entries if entry["status"] == "reviewed"]
    for i, left in enumerate(reviewed):
        for right in reviewed[i + 1:]:
            overlaps = left["gba_virtual"] < right["gba_virtual"] + right["size"] and right["gba_virtual"] < left["gba_virtual"] + left["size"]
            if overlaps and left.get("alias_of") != right["name"] and right.get("alias_of") != left["name"] \
                    and left.get("overlap") != right["name"] and right.get("overlap") != left["name"]:
                fail(f"non-alias collision: {left['name']} / {right['name']}")
    unresolved = [entry["name"] for entry in entries if entry["status"] == "unresolved" or entry.get("overlap")]
    if strict and unresolved:
        fail("release-invalid unresolved entries: " + ", ".join(unresolved))
    return entries, unresolved

def self_test(source, addresses, linker_symbols):
    valid_overlap = json.loads(json.dumps(source))
    valid_overlap["symbols"][19].update(status="reviewed", size=0x4d0)
    valid_overlap["symbols"][20].update(overlap="gSave")
    validate(valid_overlap, addresses, linker_symbols)
    cases = [
        ("duplicate", lambda s: s["symbols"].append(dict(s["symbols"][0]))),
        ("zero_size", lambda s: s["symbols"][8].update(status="reviewed", size=0)),
        ("missing_overlap", lambda s: s["symbols"][20].update(overlap="missing")),
        ("false_overlap", lambda s: s["symbols"][20].update(overlap="gRand")),
        ("cross_next_symbol", lambda s: s["symbols"][2].update(status="reviewed", size=56)),
        ("false_end_symbol", lambda s: s["symbols"][20].update(end_symbol="gHUD")),
    ]
    for name, mutate in cases:
        candidate = json.loads(json.dumps(source))
        mutate(candidate)
        try:
            validate(candidate, addresses, linker_symbols)
        except ValueError:
            continue
        raise SystemExit(f"self-test unexpectedly accepted {name}")
    print("manifest malformed-input self-test: PASS")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=None)
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source_path = Path(args.source) if args.source else root / "tools/ra/tmc_memory_manifest.json"
    source = json.loads(source_path.read_text())
    linker_path = root / "linker.ld"
    addresses = linker_addresses(linker_path)
    linker_symbols = linker_symbols_by_region(linker_path)
    if args.self_test:
        self_test(source, addresses, linker_symbols)
        return
    try:
        entries, unresolved = validate(source, addresses, linker_symbols, args.strict)
    except ValueError as error:
        raise SystemExit(f"invalid manifest: {error}")
    for name in unresolved:
        print(f"UNRESOLVED: {name}")
    print(f"manifest sha256 {manifest_hash(source, entries)}")

if __name__ == "__main__":
    main()
