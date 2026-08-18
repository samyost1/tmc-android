import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).parents[1]))
from compare_tmc_ra_snapshot import (  # noqa: E402
    CAPTURE_FILES,
    CaptureError,
    IWRAM_BYTES,
    POST_EVALUATION_PHASE,
    PROVENANCE_EXPLICIT,
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


class CompareTmcRaSnapshotTest(unittest.TestCase):
    def setUp(self):
        self.metadata = {
            "format": "tmc-ra-capture-v1",
            "manifest_sha256": "a" * 64,
            "snapshot_bytes": SNAPSHOT_BYTES,
            "frame": 120,
            "state": "S0",
            "checkpoint": "boot/title",
            "phase": POST_EVALUATION_PHASE,
        }
        self.requested = bytearray(SNAPSHOT_BYTES)
        self.requested[1] = self.requested[3] = 1
        self.requested[IWRAM_BYTES] = 1
        self.requested[SAVE_OFFSET] = 1

    def _capture(self, directory, metadata=None, role="native"):
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
                value = bytes(SNAPSHOT_BYTES)
            (directory / name).write_bytes(value)
        capture_metadata = dict(metadata or self.metadata)
        if role == "reference":
            capture_metadata["reference_emulator"] = REFERENCE_EMULATOR
        (directory / "metadata.json").write_text(
            json.dumps(capture_metadata), encoding="utf-8"
        )
        return read_capture(directory, directory.name)

    def test_compares_only_requested_valid_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = self._capture(root / "native")
            reference = self._capture(root / "reference", role="reference")
            result = compare(native, reference, self.requested)
            self.assertEqual(result["result"], "PASS")

            reference["snapshot.bin"] = bytes([1]) + reference["snapshot.bin"][1:]
            self.assertEqual(compare(native, reference, self.requested)["result"], "PASS")
            reference["snapshot.bin"] = (
                reference["snapshot.bin"][:3]
                + b"\x01"
                + reference["snapshot.bin"][4:]
            )
            self.assertEqual(compare(native, reference, self.requested)["result"], "UNVERIFIED")

    def test_unknown_and_metadata_mismatch_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = self._capture(root / "native")
            reference = self._capture(root / "reference", role="reference")
            native["validity.bin"] = bytes([1, 0]) + native["validity.bin"][2:]
            self.assertEqual(compare(native, reference, self.requested)["result"], "UNVERIFIED")
            native["validity.bin"] = bytes([0, 0]) + native["validity.bin"][2:]
            self.assertEqual(compare(native, reference, self.requested)["result"], "UNVERIFIED")
            mismatched = copy.deepcopy(self.metadata)
            mismatched["frame"] = 121
            with self.assertRaises(CaptureError):
                compare(
                    native,
                    self._capture(root / "other", mismatched, role="reference"),
                    self.requested,
                )

    def test_identity_phase_and_reference_marker_are_required(self):
        for field, value in (
            ("state", "SX"),
            ("checkpoint", "unknown"),
        ):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                native = self._capture(root / "native")
                metadata = copy.deepcopy(self.metadata)
                metadata[field] = value
                reference = self._capture(root / "reference", metadata, role="reference")
                with self.assertRaises(CaptureError):
                    compare(native, reference, self.requested)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(CaptureError):
                self._capture(
                    root / "reference",
                    {**self.metadata, "phase": "pre-evaluation"},
                    role="reference",
                )

        for field in ("state", "checkpoint", "phase"):
            with self.subTest(missing=field), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                metadata = copy.deepcopy(self.metadata)
                del metadata[field]
                with self.assertRaises(CaptureError):
                    self._capture(root / "native", metadata)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = self._capture(root / "native")
            reference = self._capture(root / "reference", role="reference")
            reference["metadata"]["reference_emulator"] = "other"
            with self.assertRaises(CaptureError):
                compare(native, reference, self.requested)

    def test_provenance_is_role_and_region_aware(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = self._capture(root / "native")
            reference = self._capture(root / "reference", role="reference")
            self.assertEqual(compare(native, reference, self.requested)["result"], "PASS")

            invalid_native = copy.deepcopy(native)
            native_provenance = bytearray(invalid_native["provenance.bin"])
            native_provenance[1] = 0
            invalid_native["provenance.bin"] = bytes(native_provenance)
            result = compare(invalid_native, reference, self.requested)
            self.assertEqual(result["result"], "UNVERIFIED")
            self.assertEqual(result["provenance_mismatches"], [(1, 1)])

            invalid_reference = copy.deepcopy(reference)
            reference_provenance = bytearray(invalid_reference["provenance.bin"])
            reference_provenance[1] = 0
            invalid_reference["provenance.bin"] = bytes(reference_provenance)
            result = compare(native, invalid_reference, self.requested)
            self.assertEqual(result["result"], "UNVERIFIED")
            self.assertEqual(result["provenance_mismatches"], [(1, 1)])

    def test_requested_bitmap_is_strict(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "requested.bin"
            path.write_bytes(bytes(SNAPSHOT_BYTES))
            with self.assertRaises(CaptureError):
                read_requested(path)
            values = bytearray(SNAPSHOT_BYTES)
            values[4] = 2
            path.write_bytes(values)
            with self.assertRaises(CaptureError):
                read_requested(path)


if __name__ == "__main__":
    unittest.main()
