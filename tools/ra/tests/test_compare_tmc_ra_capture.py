import copy
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parents[1]))
from compare_tmc_ra_capture import (  # noqa: E402
    CaptureComparisonError,
    DEFAULT_MANIFEST,
    STATE_IDS,
    STATE_NAMES,
    compare_state_matrix,
    load_manifest,
)
from compare_tmc_ra_snapshot import (  # noqa: E402
    IWRAM_BYTES,
    POST_EVALUATION_PHASE,
    PROVENANCE_EXPLICIT,
    PROVENANCE_INVALID,
    PROVENANCE_RAW_EWRAM,
    PROVENANCE_RAW_IWRAM,
    PROVENANCE_SAVE,
    REFERENCE_EMULATOR,
    SAVE_OFFSET,
    SAVE_VALID_BYTES,
    SNAPSHOT_BYTES,
)


class CompareTmcRaCaptureTest(unittest.TestCase):
    manifest_path = DEFAULT_MANIFEST
    manifest_sha256 = "a" * 64
    rom_md5 = "b" * 32

    def _manifest_document(self):
        return json.loads(self.manifest_path.read_text(encoding="utf-8"))

    def _write_capture(self, directory, state_index, role, metadata=None):
        directory.mkdir(parents=True)
        offsets = (state_index + 1, SAVE_OFFSET + state_index)
        snapshot = bytearray(SNAPSHOT_BYTES)
        for offset in offsets:
            snapshot[offset] = (0x40 + state_index + offset) & 0xFF

        validity = bytes([1]) * (SAVE_OFFSET + SAVE_VALID_BYTES) + bytes(
            SNAPSHOT_BYTES - SAVE_OFFSET - SAVE_VALID_BYTES
        )
        provenance = bytearray(SNAPSHOT_BYTES)
        if role == "native":
            provenance[:SAVE_OFFSET] = bytes([PROVENANCE_EXPLICIT]) * SAVE_OFFSET
        else:
            provenance[:IWRAM_BYTES] = bytes([PROVENANCE_RAW_IWRAM]) * IWRAM_BYTES
            provenance[IWRAM_BYTES:SAVE_OFFSET] = bytes([PROVENANCE_RAW_EWRAM]) * (
                SAVE_OFFSET - IWRAM_BYTES
            )
        provenance[SAVE_OFFSET : SAVE_OFFSET + SAVE_VALID_BYTES] = bytes(
            [PROVENANCE_SAVE]
        ) * SAVE_VALID_BYTES

        for name, value in {
            "snapshot.bin": bytes(snapshot),
            "provenance.bin": bytes(provenance),
            "validity.bin": validity,
        }.items():
            (directory / name).write_bytes(value)

        frame = 100 + state_index
        capture_metadata = {
            "format": "tmc-ra-capture-v1",
            "frame": frame,
            "manifest_sha256": self.manifest_sha256,
            "snapshot_bytes": SNAPSHOT_BYTES,
            "rom_md5": self.rom_md5,
            "state": STATE_IDS[state_index],
            "checkpoint": STATE_NAMES[state_index],
            "phase": POST_EVALUATION_PHASE,
        }
        if role == "reference":
            capture_metadata["reference_emulator"] = REFERENCE_EMULATOR
        if role == "native":
            capture_metadata.update(
                {
                    "requested_bytes": len(offsets),
                    "requested_out_of_range_bytes": 0,
                    "generation": 1,
                    "requested_ranges": len(offsets),
                    "invalid_requested_bytes": 0,
                    "invalid_requested_ranges": 0,
                }
            )
            requested = bytearray(SNAPSHOT_BYTES)
            for offset in offsets:
                requested[offset] = 1
            (directory / "requested.bin").write_bytes(requested)
        if metadata:
            capture_metadata.update(metadata)
        (directory / "metadata.json").write_text(
            json.dumps(capture_metadata), encoding="utf-8"
        )

    def _write_matrix(self, root):
        for state_index, state in enumerate(load_manifest(self.manifest_path)):
            self._write_capture(
                root / state["native"], state_index, "native"
            )
            self._write_capture(
                root / state["reference"], state_index, "reference"
            )

    def _metadata_path(self, root, state_id, role):
        state = load_manifest(self.manifest_path)[int(state_id[1:])]
        return root / state[role] / "metadata.json"

    def _update_metadata(self, root, state_id, role, **updates):
        path = self._metadata_path(root, state_id, role)
        metadata = json.loads(path.read_text(encoding="utf-8"))
        metadata.update(updates)
        path.write_text(json.dumps(metadata), encoding="utf-8")

    def test_manifest_is_exactly_the_canonical_ordered_matrix(self):
        states = load_manifest(self.manifest_path)
        self.assertEqual(tuple(state["id"] for state in states), STATE_IDS)
        self.assertEqual(tuple(state["name"] for state in states), STATE_NAMES)
        self.assertEqual(
            states[0]["native"], "S0/native"
        )
        self.assertEqual(
            states[-1]["reference"], "S14/reference"
        )

    def test_complete_binary_matrix_passes_requested_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            result_root = Path(temporary)
            self._write_matrix(result_root)

            result = compare_state_matrix(self.manifest_path, result_root)

        self.assertEqual(result["status"], "PASS")
        self.assertEqual(result["state_count"], 15)
        self.assertEqual(result["requested_bytes"], 30)
        self.assertEqual(result["requested_out_of_range_bytes"], 0)
        self.assertTrue(all(state["scope"] == "requested-only" for state in result["states"]))
        self.assertTrue(all(state["matched"] == 2 for state in result["states"]))

    def test_returns_validated_requested_bitmaps_and_fails_closed_on_tampering(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_matrix(root)

            result = compare_state_matrix(self.manifest_path, root)
            expected = {}
            for state_index, state in enumerate(load_manifest(self.manifest_path)):
                requested = bytearray(SNAPSHOT_BYTES)
                requested[state_index + 1] = 1
                requested[SAVE_OFFSET + state_index] = 1
                expected[state["id"]] = bytes(requested)

            self.assertEqual(result["requested_bitmaps"], expected)
            self.assertTrue(
                all(type(bitmap) is bytes for bitmap in result["requested_bitmaps"].values())
            )

            requested_path = root / "S0/native/requested.bin"
            tampered = bytearray(requested_path.read_bytes())
            tampered[0] = 2
            requested_path.write_bytes(tampered)

            self.assertEqual(result["requested_bitmaps"], expected)
            with self.assertRaises(CaptureComparisonError):
                compare_state_matrix(self.manifest_path, root)

    def test_manifest_rejects_missing_duplicate_unknown_reordered_and_unsafe_paths(self):
        base = self._manifest_document()
        cases = {
            "missing state": lambda value: value["states"].pop(),
            "duplicate state": lambda value: value["states"].__setitem__(
                1, copy.deepcopy(value["states"][0])
            ),
            "unknown state": lambda value: value["states"][0].update(id="SX"),
            "reordered state": lambda value: value["states"].__setitem__(
                slice(0, 2), value["states"][0:2][::-1]
            ),
            "absolute path": lambda value: value["states"][0].update(
                native="/outside"
            ),
            "traversal path": lambda value: value["states"][0].update(
                reference="S0/../outside"
            ),
        }

        for name, mutate in cases.items():
            with self.subTest(name=name):
                manifest = copy.deepcopy(base)
                mutate(manifest)
                with self.assertRaises(CaptureComparisonError):
                    load_manifest(manifest)

    def test_missing_capture_and_empty_requested_bitmap_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_matrix(root)
            shutil.rmtree(root / "S14" / "reference")
            with self.assertRaises(CaptureComparisonError):
                compare_state_matrix(self.manifest_path, root)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_matrix(root)
            requested = root / "S0" / "native" / "requested.bin"
            requested.write_bytes(bytes(SNAPSHOT_BYTES))
            with self.assertRaises(CaptureComparisonError):
                compare_state_matrix(self.manifest_path, root)

    def test_selected_bytes_metadata_and_provenance_tampering_fail_closed(self):
        mutations = {
            "invalid selected validity": lambda root: (
                root.joinpath("S0/native/validity.bin").write_bytes(
                    bytes([1, 0]) + bytes([1]) * (SNAPSHOT_BYTES - 2)
                )
            ),
            "invalid selected provenance": lambda root: self._tamper_provenance(root),
            "mismatched selected value": lambda root: self._tamper_snapshot(root),
            "requested count": lambda root: self._update_metadata(
                root, "S0", "native", requested_bytes=1
            ),
            "requested out of range": lambda root: self._update_metadata(
                root, "S0", "native", requested_out_of_range_bytes=1
            ),
            "missing generation": lambda root: self._remove_metadata(
                root, "S0", "native", "generation"
            ),
            "zero generation": lambda root: self._update_metadata(
                root, "S0", "native", generation=0
            ),
            "missing requested ranges": lambda root: self._remove_metadata(
                root, "S0", "native", "requested_ranges"
            ),
            "zero requested ranges": lambda root: self._update_metadata(
                root, "S0", "native", requested_ranges=0
            ),
            "invalid requested bytes": lambda root: self._update_metadata(
                root, "S0", "native", invalid_requested_bytes=1
            ),
            "invalid requested ranges": lambda root: self._update_metadata(
                root, "S0", "native", invalid_requested_ranges=1
            ),
            "frame mismatch": lambda root: self._update_metadata(
                root, "S0", "reference", frame=999
            ),
            "ROM mismatch": lambda root: self._update_metadata(
                root, "S0", "reference", rom_md5="c" * 32
            ),
            "manifest mismatch": lambda root: self._update_metadata(
                root, "S0", "reference", manifest_sha256="d" * 64
            ),
            "cross-state manifest invariant": lambda root: (
                self._update_metadata(
                    root, "S1", "native", manifest_sha256="e" * 64
                ),
                self._update_metadata(
                    root, "S1", "reference", manifest_sha256="e" * 64
                ),
            ),
        }

        for name, mutate in mutations.items():
            with self.subTest(name=name):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    self._write_matrix(root)
                    mutate(root)
                    with self.assertRaises(CaptureComparisonError):
                        compare_state_matrix(self.manifest_path, root)

    def test_capture_identity_phase_and_reference_marker_are_canonical(self):
        mutations = {
            "missing state": lambda root: self._remove_metadata(root, "S0", "native", "state"),
            "unknown state": lambda root: self._update_metadata(
                root, "S0", "native", state="SX"
            ),
            "state mismatch": lambda root: self._update_metadata(
                root, "S0", "reference", state="S1"
            ),
            "missing checkpoint": lambda root: self._remove_metadata(
                root, "S0", "reference", "checkpoint"
            ),
            "unknown checkpoint": lambda root: self._update_metadata(
                root, "S0", "reference", checkpoint="unknown"
            ),
            "checkpoint mismatch": lambda root: self._update_metadata(
                root, "S0", "reference", checkpoint=STATE_NAMES[1]
            ),
            "missing phase": lambda root: self._remove_metadata(
                root, "S0", "native", "phase"
            ),
            "wrong phase": lambda root: self._update_metadata(
                root, "S0", "native", phase="pre-evaluation"
            ),
            "wrong reference emulator": lambda root: self._update_metadata(
                root, "S0", "reference", reference_emulator="other"
            ),
        }

        for name, mutate in mutations.items():
            with self.subTest(name=name):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    self._write_matrix(root)
                    mutate(root)
                    with self.assertRaises(CaptureComparisonError):
                        compare_state_matrix(self.manifest_path, root)

    def _remove_metadata(self, root, state_id, role, field):
        path = self._metadata_path(root, state_id, role)
        metadata = json.loads(path.read_text(encoding="utf-8"))
        del metadata[field]
        path.write_text(json.dumps(metadata), encoding="utf-8")

    def _tamper_provenance(self, root):
        path = root / "S0/native/provenance.bin"
        value = bytearray(path.read_bytes())
        value[1] = PROVENANCE_INVALID
        path.write_bytes(value)

    def _tamper_snapshot(self, root):
        path = root / "S0/reference/snapshot.bin"
        value = bytearray(path.read_bytes())
        value[1] ^= 1
        path.write_bytes(value)


if __name__ == "__main__":
    unittest.main()
