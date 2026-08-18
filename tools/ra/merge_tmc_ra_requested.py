#!/usr/bin/env python3
"""Merge requested-address bitmaps from a complete, validated TMC RA matrix."""

import argparse
import hashlib
import json
import os
import sys
import tempfile
from pathlib import Path

from compare_tmc_ra_capture import (  # noqa: E402
    CaptureComparisonError,
    compare_state_matrix,
    load_manifest,
)
from compare_tmc_ra_snapshot import (  # noqa: E402
    POST_EVALUATION_PHASE,
    PROVENANCE_SAVE,
    REFERENCE_EMULATOR,
    SAVE_OFFSET,
    SNAPSHOT_BYTES,
)


class UnionError(ValueError):
    """Raised when requested-address evidence cannot be merged safely."""


def _validate_bitmap(bitmap, label):
    try:
        size = len(bitmap)
    except TypeError as error:
        raise UnionError(f"{label}: expected a bitmap") from error
    if size != SNAPSHOT_BYTES:
        raise UnionError(f"{label}: expected exactly 0x{SNAPSHOT_BYTES:x} bytes")
    if any(value not in (0, 1) for value in bitmap):
        raise UnionError(f"{label}: bitmap entries must be 0 or 1")
    if not any(bitmap):
        raise UnionError(f"{label}: bitmap selects no bytes")
    return bytes(bitmap)


def _require_complete_matrix(comparison, states):
    expected_ids = tuple(state["id"] for state in states)
    summaries = comparison.get("states") if isinstance(comparison, dict) else None
    actual_ids = (
        tuple(
            summary.get("id")
            for summary in summaries
            if isinstance(summary, dict)
        )
        if isinstance(summaries, (tuple, list))
        else ()
    )
    if (
        not isinstance(comparison, dict)
        or comparison.get("result") != "PASS"
        or comparison.get("status") != "PASS"
        or comparison.get("state_count") != len(states)
        or len(summaries or ()) != len(states)
        or actual_ids != expected_ids
        or any(
            not isinstance(summary, dict) or summary.get("result") != "PASS"
            for summary in summaries or ()
        )
    ):
        raise UnionError("capture matrix did not PASS all canonical S0-S14 states")


def merge_requested(manifest, capture_root):
    """Return ``(union_bitmap, summary)`` after validating every S0-S14 state."""
    try:
        comparison = compare_state_matrix(manifest, capture_root)
        states = load_manifest(manifest)
    except CaptureComparisonError as error:
        raise UnionError(str(error)) from error
    _require_complete_matrix(comparison, states)

    # Dependency: the comparator must return the exact validated bytes here.
    requested_bitmaps = (
        comparison.get("requested_bitmaps")
        if isinstance(comparison, dict)
        else None
    )
    expected_ids = {state["id"] for state in states}
    if (
        not isinstance(requested_bitmaps, dict)
        or set(requested_bitmaps) != expected_ids
    ):
        raise UnionError(
            "comparator did not return validated in-memory requested bitmaps; "
            "merge is blocked until compare_state_matrix exposes them"
        )

    union = bytearray(SNAPSHOT_BYTES)
    for state in states:
        requested = _validate_bitmap(
            requested_bitmaps[state["id"]], f"{state['id']} requested"
        )
        for offset, value in enumerate(requested):
            union[offset] |= value

    bitmap = _validate_bitmap(bytes(union), "union")
    return bitmap, {
        "inputs": len(states),
        "states": len(states),
        "selected": sum(bitmap),
        "sha256": hashlib.sha256(bitmap).hexdigest(),
    }


def write_union(path, bitmap):
    """Atomically write a validated, non-empty union bitmap."""
    bitmap = _validate_bitmap(bitmap, "union")
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            dir=destination.parent,
            prefix=f".{destination.name}.",
            delete=False,
        ) as stream:
            temporary = Path(stream.name)
            stream.write(bitmap)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
    except OSError as error:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        raise UnionError(f"cannot write {destination}: {error}") from error


def _write_self_test_capture(directory, metadata, requested, include_requested):
    directory.mkdir(parents=True)
    validity = bytearray(SNAPSHOT_BYTES)
    provenance = bytearray(SNAPSHOT_BYTES)
    for offset, value in enumerate(requested):
        if value:
            validity[offset] = 1
            provenance[offset] = PROVENANCE_SAVE
    for name, value in {
        "snapshot.bin": bytes(SNAPSHOT_BYTES),
        "provenance.bin": bytes(provenance),
        "validity.bin": bytes(validity),
    }.items():
        (directory / name).write_bytes(value)
    (directory / "metadata.json").write_text(
        json.dumps(metadata), encoding="utf-8"
    )
    if include_requested:
        (directory / "requested.bin").write_bytes(requested)


def self_test():
    manifest = Path(__file__).with_name("tmc_ra_state_manifest.json")
    metadata = {
        "format": "tmc-ra-capture-v1",
        "manifest_sha256": "a" * 64,
        "snapshot_bytes": SNAPSHOT_BYTES,
        "rom_md5": "b" * 32,
    }
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for state_index, state in enumerate(load_manifest(manifest)):
            requested = bytearray(SNAPSHOT_BYTES)
            requested[SAVE_OFFSET + state_index] = 1
            native_metadata = {
                **metadata,
                "frame": 120 + state_index,
                "state": state["id"],
                "checkpoint": state["name"],
                "phase": POST_EVALUATION_PHASE,
                "requested_bytes": 1,
                "requested_out_of_range_bytes": 0,
                "generation": 1,
                "requested_ranges": 1,
                "invalid_requested_bytes": 0,
                "invalid_requested_ranges": 0,
            }
            reference_metadata = {
                **metadata,
                "frame": 120 + state_index,
                "state": state["id"],
                "checkpoint": state["name"],
                "phase": POST_EVALUATION_PHASE,
                "reference_emulator": REFERENCE_EMULATOR,
            }
            _write_self_test_capture(
                root / state["native"],
                native_metadata,
                bytes(requested),
                True,
            )
            _write_self_test_capture(
                root / state["reference"],
                reference_metadata,
                bytes(requested),
                False,
            )

        output = root / "union.bin"
        original_compare = compare_state_matrix

        def unaugmented_compare(manifest_path, capture_root):
            comparison = dict(original_compare(manifest_path, capture_root))
            comparison.pop("requested_bitmaps", None)
            return comparison

        globals()["compare_state_matrix"] = unaugmented_compare
        try:
            merge_requested(manifest, root)
        except UnionError as error:
            assert "validated in-memory requested bitmaps" in str(error)
        else:
            raise AssertionError("merge accepted an unaugmented comparator result")
        finally:
            globals()["compare_state_matrix"] = original_compare
        assert not output.exists()
    print("merge_tmc_ra_requested: ALL PASS")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--manifest", type=Path, help="canonical S0-S14 state manifest")
    parser.add_argument("--capture-root", type=Path, help="root containing all state captures")
    parser.add_argument("--output", type=Path, help="destination for the union bitmap")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        self_test()
        return 0

    missing = [
        option
        for option, value in (
            ("--manifest", args.manifest),
            ("--capture-root", args.capture_root),
            ("--output", args.output),
        )
        if value is None
    ]
    if missing:
        parser.error(
            "the following arguments are required for a merge: "
            + ", ".join(missing)
        )

    try:
        bitmap, summary = merge_requested(args.manifest, args.capture_root)
        write_union(args.output, bitmap)
    except UnionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(
        f"PASS states={summary['states']} selected={summary['selected']} "
        f"sha256={summary['sha256']} output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
