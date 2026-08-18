#!/usr/bin/env python3
"""Admit a complete, requested-only TMC RA binary capture state matrix."""

import argparse
import json
import os
import re
import sys
from collections.abc import Mapping
from pathlib import Path, PureWindowsPath

from compare_tmc_ra_snapshot import (  # noqa: E402
    CaptureError,
    POST_EVALUATION_PHASE,
    REFERENCE_EMULATOR,
    compare as compare_snapshot,
    read_capture,
    read_requested,
)


MANIFEST_FORMAT = "tmc-ra-state-manifest-v1"
CAPTURE_FORMAT = "tmc-ra-capture-v1"
DEFAULT_MANIFEST = Path(__file__).with_name("tmc_ra_state_manifest.json")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
MD5_RE = re.compile(r"^[0-9a-f]{32}$")
STATE_FIELDS = frozenset({"id", "name", "native", "reference"})
MANIFEST_FIELDS = frozenset({"format", "states"})
STATE_IDS = tuple(f"S{index}" for index in range(15))
STATE_NAMES = (
    "boot/title",
    "file select",
    "new game intro",
    "first controllable overworld",
    "room transition",
    "pause/items",
    "quest/status",
    "world map",
    "dungeon entry",
    "dungeon map/floor transition",
    "item pickup / progress flag change",
    "save and reload",
    "soft reset/relaunch",
    "authenticated achievement evaluation",
    "normal achievement trigger",
)
INVARIANT_FIELDS = ("format", "manifest_sha256", "snapshot_bytes", "rom_md5")


class CaptureComparisonError(ValueError):
    """Raised when a state matrix is not admissible evidence."""


def _fail(message):
    raise CaptureComparisonError(message)


def _reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_json_constant(value):
    raise ValueError(f"invalid JSON constant: {value}")


def _read_json(path, label):
    try:
        with Path(path).open(encoding="utf-8") as stream:
            return json.load(
                stream,
                object_pairs_hook=_reject_duplicate_keys,
                parse_constant=_reject_json_constant,
            )
    except (OSError, UnicodeError, ValueError) as error:
        _fail(f"{label}: malformed JSON: {error}")


def _document(source, label):
    if isinstance(source, Mapping):
        return source
    if isinstance(source, (str, os.PathLike)):
        return _read_json(source, label)
    _fail(f"{label}: expected a JSON object or path")


def _require_object(value, label):
    if not isinstance(value, Mapping):
        _fail(f"{label}: expected an object")


def _require_fields(document, required, allowed, label):
    _require_object(document, label)
    missing = sorted(required - document.keys())
    unknown = sorted(
        (str(key) for key in document.keys() if key not in allowed),
        key=str,
    )
    if missing:
        _fail(f"{label}: missing field(s): {', '.join(missing)}")
    if unknown:
        _fail(f"{label}: unknown field(s): {', '.join(unknown)}")


def _safe_relative_path(value, label):
    if not isinstance(value, str) or not value:
        _fail(f"{label}: expected a non-empty relative path")
    if "\x00" in value or "\\" in value:
        _fail(f"{label}: path separators must be POSIX and safe")
    if any(part in ("", ".", "..") for part in value.split("/")):
        _fail(f"{label}: path must not contain empty, '.' or '..' components")

    path = Path(value)
    if path.is_absolute() or PureWindowsPath(value).is_absolute():
        _fail(f"{label}: absolute paths are not allowed")
    if re.match(r"^[A-Za-z]:", value):
        _fail(f"{label}: drive-qualified paths are not allowed")
    return value


def load_manifest(source=DEFAULT_MANIFEST):
    """Load and validate the canonical ordered S0-S14 state manifest."""
    document = _document(source, "state manifest")
    _require_fields(document, MANIFEST_FIELDS, MANIFEST_FIELDS, "state manifest")
    if document["format"] != MANIFEST_FORMAT:
        _fail(f"state manifest: unsupported format {document['format']!r}")

    states = document["states"]
    if not isinstance(states, list):
        _fail("state manifest: states must be a list")
    if len(states) != len(STATE_IDS):
        _fail(f"state manifest: expected exactly {len(STATE_IDS)} states")

    validated = []
    seen_paths = set()
    for index, state in enumerate(states):
        label = f"state manifest states[{index}]"
        _require_fields(state, STATE_FIELDS, STATE_FIELDS, label)
        state_id = state["id"]
        if state_id != STATE_IDS[index]:
            if state_id in STATE_IDS:
                _fail(
                    f"{label}: noncanonical order, expected {STATE_IDS[index]} "
                    f"but found {state_id}"
                )
            _fail(f"{label}: unknown state id {state_id!r}")
        if state["name"] != STATE_NAMES[index]:
            _fail(
                f"{label}: noncanonical name for {state_id}, "
                f"expected {STATE_NAMES[index]!r}"
            )

        state_paths = {}
        for role in ("native", "reference"):
            path = _safe_relative_path(state[role], f"{label} {role}")
            if path in seen_paths:
                _fail(f"{label} {role}: duplicate capture path {path!r}")
            seen_paths.add(path)
            state_paths[role] = path

        validated.append(
            {
                "id": state_id,
                "name": state["name"],
                "native": state_paths["native"],
                "reference": state_paths["reference"],
            }
        )
    return tuple(validated)


def _capture_path(root, relative, label):
    try:
        capture_root = Path(root).resolve()
        candidate = (capture_root / relative).resolve()
        candidate.relative_to(capture_root)
    except (OSError, ValueError, TypeError) as error:
        _fail(f"{label}: capture path escapes capture root: {error}")
    return candidate


def _require_counter(metadata, field, expected, label):
    value = metadata.get(field)
    if type(value) is not int or value < 0:
        _fail(f"{label}: invalid {field}")
    if value != expected:
        _fail(f"{label}: {field}={value}, expected {expected}")
    return value


def _require_native_contract(metadata, requested_count, label):
    generation = metadata.get("generation")
    if type(generation) is not int or generation <= 0:
        _fail(f"{label}: invalid generation")

    requested_ranges = metadata.get("requested_ranges")
    if type(requested_ranges) is not int or requested_ranges <= 0:
        _fail(f"{label}: invalid requested_ranges")

    _require_counter(metadata, "invalid_requested_bytes", 0, label)
    _require_counter(metadata, "invalid_requested_ranges", 0, label)
    _require_counter(metadata, "requested_bytes", requested_count, label)
    _require_counter(metadata, "requested_out_of_range_bytes", 0, label)


def _metadata_invariants(capture, label):
    metadata = capture["metadata"]
    if metadata.get("format") != CAPTURE_FORMAT:
        _fail(f"{label}: unsupported capture format")
    digest = metadata.get("manifest_sha256")
    if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
        _fail(f"{label}: invalid manifest_sha256")
    rom_md5 = metadata.get("rom_md5")
    if not isinstance(rom_md5, str) or not MD5_RE.fullmatch(rom_md5):
        _fail(f"{label}: invalid rom_md5")
    return tuple(metadata.get(field) for field in INVARIANT_FIELDS)


def _require_matrix_invariants(expected, actual, label):
    mismatches = [
        field
        for field, expected_value, actual_value in zip(
            INVARIANT_FIELDS, expected, actual
        )
        if expected_value != actual_value
    ]
    if mismatches:
        _fail(f"{label}: invariant metadata mismatch: {', '.join(mismatches)}")


def _require_canonical_capture(capture, state, label, reference=False):
    metadata = capture["metadata"]
    for field, expected in (("state", state["id"]), ("checkpoint", state["name"])):
        value = metadata.get(field)
        if value != expected:
            if field not in metadata:
                _fail(f"{label}: missing {field}")
            _fail(f"{label}: {field}={value!r}, expected {expected!r}")
    if metadata.get("phase") != POST_EVALUATION_PHASE:
        _fail(f"{label}: phase must be {POST_EVALUATION_PHASE!r}")
    if reference and metadata.get("reference_emulator") != REFERENCE_EMULATOR:
        _fail(f"{label}: reference_emulator must be {REFERENCE_EMULATOR!r}")


def _compare_state(state, root, expected_invariants=None):
    state_id = state["id"]
    native_directory = _capture_path(
        root, state["native"], f"{state_id} native path"
    )
    reference_directory = _capture_path(
        root, state["reference"], f"{state_id} reference path"
    )
    try:
        native = read_capture(native_directory, f"{state_id} native")
        reference = read_capture(reference_directory, f"{state_id} reference")
        requested = read_requested(native_directory / "requested.bin", f"{state_id} requested")
    except CaptureError as error:
        _fail(str(error))

    _require_canonical_capture(native, state, f"{state_id} native")
    _require_canonical_capture(reference, state, f"{state_id} reference", reference=True)
    native_invariants = _metadata_invariants(native, f"{state_id} native")
    reference_invariants = _metadata_invariants(reference, f"{state_id} reference")
    _require_matrix_invariants(
        native_invariants, reference_invariants, f"{state_id} native/reference"
    )
    if expected_invariants is not None:
        _require_matrix_invariants(
            expected_invariants, native_invariants, f"{state_id} native"
        )

    requested_count = sum(requested)
    _require_native_contract(
        native["metadata"], requested_count, f"{state_id} native metadata"
    )

    try:
        result = compare_snapshot(
            native,
            reference,
            requested,
            validated_scope=False,
            allow_frame_offset=False,
        )
    except CaptureError as error:
        _fail(f"{state_id}: {error}")
    if result["scope"] != "requested-only":
        _fail(f"{state_id}: comparison was not requested-only")
    if result["result"] != "PASS":
        _fail(
            f"{state_id}: binary comparison {result['result']} "
            f"(unknown={result['unknown']}, mismatches={result['mismatches']}, "
            f"provenance={result['provenance_mismatches']})"
        )

    return {
        "id": state_id,
        "name": state["name"],
        "result": "PASS",
        "scope": result["scope"],
        "frame": native["metadata"]["frame"],
        "manifest_sha256": native["metadata"]["manifest_sha256"],
        "rom_md5": native["metadata"].get("rom_md5"),
        "requested_bytes": requested_count,
        "requested_out_of_range_bytes": 0,
        "selected": result["selected"],
        "compared": result["compared"],
        "matched": result["matched"],
    }, native_invariants, requested


def compare_state_matrix(manifest=DEFAULT_MANIFEST, capture_root=None):
    """Compare every canonical state and return an in-memory PASS summary."""
    if capture_root is None:
        _fail("capture root is required")
    states = load_manifest(manifest)
    summaries = []
    requested_bitmaps = {}
    expected_invariants = None
    for state in states:
        summary, invariants, requested = _compare_state(
            state, capture_root, expected_invariants
        )
        if expected_invariants is None:
            expected_invariants = invariants
        summaries.append(summary)
        requested_bitmaps[state["id"]] = requested
    return {
        "result": "PASS",
        "status": "PASS",
        "states": tuple(summaries),
        "requested_bitmaps": requested_bitmaps,
        "state_count": len(summaries),
        "requested_bytes": sum(item["requested_bytes"] for item in summaries),
        "requested_out_of_range_bytes": sum(
            item["requested_out_of_range_bytes"] for item in summaries
        ),
        "manifest_sha256": expected_invariants[1],
        "rom_md5": expected_invariants[3],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--capture-root", type=Path, required=True)
    args = parser.parse_args(argv)

    try:
        result = compare_state_matrix(args.manifest, args.capture_root)
    except CaptureComparisonError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(
        f"PASS states={result['state_count']} "
        f"requested_bytes={result['requested_bytes']} "
        f"requested_out_of_range_bytes={result['requested_out_of_range_bytes']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
