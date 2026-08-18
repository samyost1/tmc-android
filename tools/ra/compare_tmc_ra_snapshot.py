#!/usr/bin/env python3
"""Fail-closed comparison of native and mGBA TMC RA snapshot captures.

Capture directories contain fixed-size snapshot/provenance/validity files and
metadata.json.  The native directory also contains requested.bin, unless a
different requested bitmap is supplied with --requested.  Only selected
requested bytes are compared; unknown or invalid selected bytes never pass.
Memory values are never printed.
"""

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path


SNAPSHOT_BYTES = 0x58000
IWRAM_BYTES = 0x08000
SAVE_OFFSET = 0x48000
SAVE_BYTES = 0x10000
SAVE_VALID_BYTES = 0x02000
CAPTURE_FILES = ("snapshot.bin", "provenance.bin", "validity.bin")
METADATA_FIELDS = (
    "format",
    "manifest_sha256",
    "snapshot_bytes",
    "frame",
    "rom_md5",
    "state",
    "checkpoint",
    "phase",
)
FORMAT = "tmc-ra-capture-v1"
POST_EVALUATION_PHASE = "post-evaluation"
REFERENCE_EMULATOR = "mGBA 0.10.5"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
MD5_RE = re.compile(r"^[0-9a-f]{32}$")
PROVENANCE_INVALID = 0
PROVENANCE_RAW_IWRAM = 1
PROVENANCE_RAW_EWRAM = 2
PROVENANCE_SAVE = 3
PROVENANCE_EXPLICIT = 4


class CaptureError(ValueError):
    """Raised when capture input is not admissible evidence."""


def _fail(message):
    raise CaptureError(message)


def _reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _require_identity_metadata(metadata, label):
    for field in ("state", "checkpoint"):
        value = metadata.get(field)
        if not isinstance(value, str) or not value:
            _fail(f"{label}: invalid {field}")
    if metadata.get("phase") != POST_EVALUATION_PHASE:
        _fail(f"{label}: invalid phase")


def _read_metadata(directory, label):
    path = Path(directory) / "metadata.json"
    try:
        metadata = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=lambda value: (_ for _ in ()).throw(
                ValueError(f"invalid JSON constant: {value}")
            ),
        )
    except (OSError, UnicodeError, ValueError) as error:
        _fail(f"{label}: invalid metadata: {error}")
    if not isinstance(metadata, dict):
        _fail(f"{label}: metadata must be an object")
    if metadata.get("format") != FORMAT:
        _fail(f"{label}: unsupported metadata format")
    digest = metadata.get("manifest_sha256")
    if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
        _fail(f"{label}: invalid manifest_sha256")
    if "rom_md5" in metadata and (
        not isinstance(metadata["rom_md5"], str) or not MD5_RE.fullmatch(metadata["rom_md5"])
    ):
        _fail(f"{label}: invalid rom_md5")
    if type(metadata.get("snapshot_bytes")) is not int or metadata["snapshot_bytes"] != SNAPSHOT_BYTES:
        _fail(f"{label}: invalid snapshot_bytes")
    if type(metadata.get("frame")) is not int or not 0 <= metadata["frame"] <= 0xFFFFFFFF:
        _fail(f"{label}: invalid frame")
    _require_identity_metadata(metadata, label)
    return metadata


def _read_exact(path, label):
    try:
        value = Path(path).read_bytes()
    except OSError as error:
        _fail(f"{label}: unreadable: {error}")
    if len(value) != SNAPSHOT_BYTES:
        _fail(f"{label}: expected exactly 0x{SNAPSHOT_BYTES:x} bytes")
    return value


def read_capture(directory, label):
    directory = Path(directory)
    return {
        "metadata": _read_metadata(directory, label),
        **{
            name: _read_exact(directory / name, f"{label}/{name}")
            for name in CAPTURE_FILES
        },
    }


def read_requested(path, label="requested"):
    requested = _read_exact(path, label)
    if any(value not in (0, 1) for value in requested):
        _fail(f"{label}: bitmap entries must be 0 or 1")
    if not any(requested):
        _fail(f"{label}: bitmap selects no bytes")
    return requested


def _ranges(indices):
    if not indices:
        return []
    values = sorted(indices)
    result = []
    start = end = values[0]
    for value in values[1:]:
        if value == end + 1:
            end = value
        else:
            result.append((start, end))
            start = end = value
    result.append((start, end))
    return result


def _metadata_mismatches(native, reference):
    return [
        field
        for field in METADATA_FIELDS
        if native["metadata"].get(field) != reference["metadata"].get(field)
    ]


def _provenance_is_valid(role, offset, provenance):
    if role == "native":
        if offset < SAVE_OFFSET:
            return provenance == PROVENANCE_EXPLICIT
        if offset < SAVE_OFFSET + SAVE_BYTES:
            return provenance == PROVENANCE_SAVE
        return False
    if offset < IWRAM_BYTES:
        return provenance == PROVENANCE_RAW_IWRAM
    if offset < SAVE_OFFSET:
        return provenance == PROVENANCE_RAW_EWRAM
    if offset < SAVE_OFFSET + SAVE_BYTES:
        return provenance == PROVENANCE_SAVE
    return False


def compare(native, reference, requested, validated_scope=False, allow_frame_offset=False):
    _require_identity_metadata(native["metadata"], "native")
    _require_identity_metadata(reference["metadata"], "reference")
    if reference["metadata"].get("reference_emulator") != REFERENCE_EMULATOR:
        _fail("reference: unsupported reference_emulator")
    mismatches = _metadata_mismatches(native, reference)
    if allow_frame_offset:
        mismatches = [field for field in mismatches if field != "frame"]
    if mismatches:
        _fail("metadata mismatch: " + ", ".join(mismatches))

    if validated_scope:
        selected = [index for index, valid in enumerate(native["validity.bin"]) if valid]
        scope = "native-validated-scope-not-requested"
    else:
        selected = [index for index, bit in enumerate(requested) if bit]
        scope = "requested-only"
    if not selected:
        _fail("comparison selects no bytes")

    unknown = []
    value_mismatches = []
    provenance_mismatches = []
    for index in selected:
        native_valid = native["validity.bin"][index] == 1
        reference_valid = reference["validity.bin"][index] == 1
        if not native_valid or not reference_valid:
            unknown.append(index)
            continue
        if native["snapshot.bin"][index] != reference["snapshot.bin"][index]:
            value_mismatches.append(index)
        if not _provenance_is_valid("native", index, native["provenance.bin"][index]) or not _provenance_is_valid(
            "reference", index, reference["provenance.bin"][index]
        ):
            provenance_mismatches.append(index)

    return {
        "scope": scope,
        "selected": len(selected),
        "compared": len(selected) - len(unknown),
        "matched": len(selected) - len(unknown) - len(value_mismatches),
        "unknown": _ranges(unknown),
        "mismatches": _ranges(value_mismatches),
        "provenance_mismatches": _ranges(provenance_mismatches),
        "frame_offset": reference["metadata"]["frame"] - native["metadata"]["frame"],
        "result": (
            "UNVERIFIED"
            if allow_frame_offset or unknown or value_mismatches or provenance_mismatches
            else "PASS"
        ),
    }


def execute(native_directory, reference_directory, requested_path=None,
            validated_scope=False, allow_frame_offset=False):
    try:
        native = read_capture(native_directory, "native")
        reference = read_capture(reference_directory, "reference")
        if requested_path is None and not validated_scope:
            requested_path = Path(native_directory) / "requested.bin"
        requested = read_requested(requested_path) if requested_path is not None else None
        result = compare(native, reference, requested, validated_scope, allow_frame_offset)
    except CaptureError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(
        f"{result['result']} scope={result['scope']} "
        f"selected={result['selected']} compared={result['compared']} "
        f"matched={result['matched']} frame_offset={result['frame_offset']:+d}"
    )
    print(f"unknown_ranges={result['unknown']}")
    print(f"mismatch_ranges={result['mismatches']}")
    print(f"provenance_mismatch_ranges={result['provenance_mismatches']}")
    return 0 if result["result"] == "PASS" else 2


def _write_capture(directory, metadata, fill=0, role="native"):
    directory.mkdir()
    for name in CAPTURE_FILES:
        if name == "validity.bin":
            value = bytes([1]) * (SAVE_OFFSET + SAVE_VALID_BYTES) + bytes(
                SNAPSHOT_BYTES - SAVE_OFFSET - SAVE_VALID_BYTES
            )
        elif name == "provenance.bin":
            value = bytearray(SNAPSHOT_BYTES)
            if role == "native":
                value[:SAVE_OFFSET] = bytes([PROVENANCE_EXPLICIT]) * SAVE_OFFSET
            else:
                value[:IWRAM_BYTES] = bytes([PROVENANCE_RAW_IWRAM]) * IWRAM_BYTES
                value[IWRAM_BYTES:SAVE_OFFSET] = bytes([PROVENANCE_RAW_EWRAM]) * (
                    SAVE_OFFSET - IWRAM_BYTES
                )
            value[SAVE_OFFSET : SAVE_OFFSET + SAVE_VALID_BYTES] = bytes([PROVENANCE_SAVE]) * (
                SAVE_VALID_BYTES
            )
            value = bytes(value)
        else:
            value = bytes([fill]) * SNAPSHOT_BYTES
        (directory / name).write_bytes(value)
    metadata = dict(metadata)
    if role == "reference":
        metadata.setdefault("reference_emulator", REFERENCE_EMULATOR)
    (directory / "metadata.json").write_text(json.dumps(metadata), encoding="utf-8")


def self_test():
    metadata = {
        "format": FORMAT,
        "manifest_sha256": "a" * 64,
        "snapshot_bytes": SNAPSHOT_BYTES,
        "frame": 120,
        "state": "S0",
        "checkpoint": "boot/title",
        "phase": POST_EVALUATION_PHASE,
    }
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        native_dir = root / "native"
        reference_dir = root / "reference"
        _write_capture(native_dir, metadata)
        _write_capture(reference_dir, metadata, role="reference")
        requested = bytearray(SNAPSHOT_BYTES)
        requested[1] = requested[3] = 1
        (native_dir / "requested.bin").write_bytes(requested)

        native = read_capture(native_dir, "native")
        reference = read_capture(reference_dir, "reference")
        assert compare(native, reference, requested)["result"] == "PASS"
        assert compare(native, reference, None, validated_scope=True)["result"] == "PASS"

        changed = bytearray(reference["snapshot.bin"])
        changed[2] = 1
        (reference_dir / "snapshot.bin").write_bytes(changed)
        assert compare(read_capture(native_dir, "native"), read_capture(reference_dir, "reference"),
                       requested)["result"] == "PASS"
        changed[3] = 1
        (reference_dir / "snapshot.bin").write_bytes(changed)
        assert compare(read_capture(native_dir, "native"), read_capture(reference_dir, "reference"),
                       requested)["result"] == "UNVERIFIED"

        invalid = bytearray(native["validity.bin"])
        invalid[1] = 0
        (native_dir / "validity.bin").write_bytes(invalid)
        assert compare(read_capture(native_dir, "native"), read_capture(reference_dir, "reference"),
                       requested)["result"] == "UNVERIFIED"

        requested[1] = 2
        (native_dir / "requested.bin").write_bytes(requested)
        try:
            read_requested(native_dir / "requested.bin")
        except CaptureError:
            pass
        else:
            raise AssertionError("non-binary requested bitmap accepted")
    print("compare_tmc_ra_snapshot: ALL PASS")


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--native")
    parser.add_argument("--reference")
    parser.add_argument("--requested")
    parser.add_argument("--validated-scope", action="store_true")
    parser.add_argument("--allow-frame-offset", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        self_test()
        return 0
    if not args.native or not args.reference:
        parser.error("--native and --reference are required unless --self-test is used")
    return execute(
        args.native,
        args.reference,
        args.requested,
        args.validated_scope,
        args.allow_frame_offset,
    )


if __name__ == "__main__":
    raise SystemExit(main())
