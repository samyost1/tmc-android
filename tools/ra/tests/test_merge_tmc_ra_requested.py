import hashlib
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).parents[1]))
from compare_tmc_ra_capture import (  # noqa: E402
    DEFAULT_MANIFEST,
    compare_state_matrix as compare_capture_state_matrix,
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
    compare,
    read_capture,
    read_requested,
)
from merge_tmc_ra_requested import (  # noqa: E402
    main,
    merge_requested,
)


class MergeTmcRaRequestedTest(unittest.TestCase):
    manifest_path = DEFAULT_MANIFEST
    manifest_sha256 = "a" * 64
    rom_md5 = "b" * 32

    def _requested_bitmap(self, state_index):
        requested = bytearray(SNAPSHOT_BYTES)
        requested[state_index + 1] = 1
        requested[SAVE_OFFSET + state_index] = 1
        return bytes(requested)

    def _write_capture(self, directory, state_index, role, metadata=None):
        directory.mkdir(parents=True)
        offsets = (state_index + 1, SAVE_OFFSET + state_index)
        requested = self._requested_bitmap(state_index)
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

        state = load_manifest(self.manifest_path)[state_index]
        capture_metadata = {
            "format": "tmc-ra-capture-v1",
            "frame": 100 + state_index,
            "manifest_sha256": self.manifest_sha256,
            "snapshot_bytes": SNAPSHOT_BYTES,
            "rom_md5": self.rom_md5,
            "state": state["id"],
            "checkpoint": state["name"],
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
            (directory / "requested.bin").write_bytes(requested)
        if metadata:
            capture_metadata.update(metadata)
        (directory / "metadata.json").write_text(
            json.dumps(capture_metadata), encoding="utf-8"
        )

    def _write_matrix(self, root):
        requested_bitmaps = {}
        for state_index, state in enumerate(load_manifest(self.manifest_path)):
            self._write_capture(root / state["native"], state_index, "native")
            self._write_capture(root / state["reference"], state_index, "reference")
            requested_bitmaps[state["id"]] = self._requested_bitmap(state_index)
        return requested_bitmaps

    def _state_path(self, root, state_id, role):
        state = load_manifest(self.manifest_path)[int(state_id[1:])]
        return root / state[role]

    def _update_metadata(self, root, state_id, role, **updates):
        path = self._state_path(root, state_id, role) / "metadata.json"
        metadata = json.loads(path.read_text(encoding="utf-8"))
        metadata.update(updates)
        path.write_text(json.dumps(metadata), encoding="utf-8")

    def _run_merge(self, root, output):
        return main(
            [
                "--manifest",
                str(self.manifest_path),
                "--capture-root",
                str(root),
                "--output",
                str(output),
            ]
        )

    def test_merges_all_15_states_and_reports_union_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            requested_bitmaps = self._write_matrix(root)

            def compare_with_bitmaps(manifest, capture_root):
                result = compare_capture_state_matrix(manifest, capture_root)
                return {**result, "requested_bitmaps": requested_bitmaps}

            with patch(
                "merge_tmc_ra_requested.compare_state_matrix",
                side_effect=compare_with_bitmaps,
            ):
                bitmap, summary = merge_requested(self.manifest_path, root)

            expected = bytearray(SNAPSHOT_BYTES)
            for state_index in range(15):
                expected[state_index + 1] = 1
                expected[SAVE_OFFSET + state_index] = 1
            expected = bytes(expected)
            self.assertEqual(bitmap, expected)
            self.assertEqual(summary["inputs"], 15)
            self.assertEqual(summary["states"], 15)
            self.assertEqual(summary["selected"], 30)
            self.assertEqual(
                summary["sha256"],
                hashlib.sha256(expected).hexdigest(),
            )

            output = root / "union.bin"
            with patch(
                "merge_tmc_ra_requested.compare_state_matrix",
                side_effect=compare_with_bitmaps,
            ):
                self.assertEqual(self._run_merge(root, output), 0)
            self.assertEqual(output.read_bytes(), expected)

    def test_unaugmented_comparator_result_fails_closed_without_bitmaps(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_matrix(root)
            output = root / "union.bin"
            output.write_bytes(b"existing union")

            comparison = compare_capture_state_matrix(self.manifest_path, root)
            comparison.pop("requested_bitmaps")
            with patch(
                "merge_tmc_ra_requested.compare_state_matrix",
                return_value=comparison,
            ):
                self.assertEqual(self._run_merge(root, output), 1)
            self.assertEqual(output.read_bytes(), b"existing union")

    def test_matrix_fixtures_use_canonical_capture_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_matrix(root)

            for state in load_manifest(self.manifest_path):
                for role in ("native", "reference"):
                    metadata = json.loads(
                        (
                            root / state[role] / "metadata.json"
                        ).read_text(encoding="utf-8")
                    )
                    self.assertEqual(metadata["state"], state["id"])
                    self.assertEqual(metadata["checkpoint"], state["name"])
                    self.assertEqual(metadata["phase"], POST_EVALUATION_PHASE)
                    if role == "reference":
                        self.assertEqual(
                            metadata["reference_emulator"], REFERENCE_EMULATOR
                        )

    def test_tampered_evidence_fails_without_overwriting_output(self):
        mutations = {
            "validity": self._tamper_validity,
            "provenance": self._tamper_provenance,
            "snapshot": self._tamper_snapshot,
            "reference metadata": lambda root: self._update_metadata(
                root, "S0", "reference", frame=999
            ),
            "requested bitmap": self._tamper_requested,
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    self._write_matrix(root)
                    output = root / "union.bin"
                    output.write_bytes(b"existing union")
                    mutate(root)

                    self.assertEqual(self._run_merge(root, output), 1)
                    self.assertEqual(output.read_bytes(), b"existing union")

    def test_missing_s14_fails_without_creating_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_matrix(root)
            shutil.rmtree(self._state_path(root, "S14", "reference"))
            output = root / "union.bin"

            self.assertEqual(self._run_merge(root, output), 1)
            self.assertFalse(output.exists())

    def test_empty_union_fails_without_creating_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_matrix(root)
            for state in load_manifest(self.manifest_path):
                requested_path = (
                    root / state["native"] / "requested.bin"
                )
                requested_path.write_bytes(bytes(SNAPSHOT_BYTES))
                self._update_metadata(
                    root, state["id"], "native", requested_bytes=0
                )
            output = root / "union.bin"

            self.assertEqual(self._run_merge(root, output), 1)
            self.assertFalse(output.exists())

    def test_validated_scope_only_title_capture_is_not_admitted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write_matrix(root)
            for role in ("native", "reference"):
                path = self._state_path(root, "S0", role) / "validity.bin"
                validity = bytearray(path.read_bytes())
                validity[1] = 0
                path.write_bytes(validity)

            native_path = self._state_path(root, "S0", "native")
            reference_path = self._state_path(root, "S0", "reference")
            native = read_capture(native_path, "native")
            reference = read_capture(reference_path, "reference")
            requested = read_requested(native_path / "requested.bin")
            self.assertEqual(
                compare(
                    native,
                    reference,
                    requested,
                    validated_scope=True,
                )["result"],
                "PASS",
            )

            output = root / "union.bin"
            self.assertEqual(self._run_merge(root, output), 1)
            self.assertFalse(output.exists())

    def test_cli_requires_manifest_and_capture_root_and_rejects_positionals(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "union.bin"

            with self.assertRaises(SystemExit) as missing:
                main(["--output", str(output)])
            self.assertEqual(missing.exception.code, 2)

            with self.assertRaises(SystemExit) as positional:
                main(
                    [
                        "--manifest",
                        str(self.manifest_path),
                        "--capture-root",
                        str(root),
                        "--output",
                        str(output),
                        str(root / "capture"),
                    ]
                )
            self.assertEqual(positional.exception.code, 2)
            self.assertFalse(output.exists())

    def _tamper_validity(self, root):
        path = self._state_path(root, "S0", "native") / "validity.bin"
        value = bytearray(path.read_bytes())
        value[1] = 0
        path.write_bytes(value)

    def _tamper_provenance(self, root):
        path = self._state_path(root, "S0", "native") / "provenance.bin"
        value = bytearray(path.read_bytes())
        value[1] = PROVENANCE_INVALID
        path.write_bytes(value)

    def _tamper_snapshot(self, root):
        path = self._state_path(root, "S0", "reference") / "snapshot.bin"
        value = bytearray(path.read_bytes())
        value[1] ^= 1
        path.write_bytes(value)

    def _tamper_requested(self, root):
        path = self._state_path(root, "S0", "native") / "requested.bin"
        value = bytearray(path.read_bytes())
        value[1] = 2
        path.write_bytes(value)


if __name__ == "__main__":
    unittest.main()
