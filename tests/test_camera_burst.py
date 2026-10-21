"""Negative/mutation tests for the independent NeXium camera oracle."""
import copy
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_camera_burst import verify_camera_bursts


def synthetic_guest():
    rows = []
    seq = 0

    def emit(frame, event, upper, lower, controller=1, stage=0):
        nonlocal seq
        rows.append(dict(seq=str(seq), frames=str(frame), event=str(event),
                         mode="1", stage=str(stage), controllers=str(controller),
                         detail=str((upper << 16) | lower)))
        seq += 1

    for begin, start, sign in ((101, 50, 1), (201, 103, -1)):
        for i in range(24):
            frame = begin + i
            current = start + sign * (i + 1)
            presented = current - sign
            emit(frame, 14, current, presented)
            if i == 6:
                emit(frame, 13, current, presented)
                emit(frame, 12, presented, presented // 2)
    # Ordered by global event seq, not by synthetic frame group only.
    return rows


class CameraWitnessProtocol(unittest.TestCase):
    def setUp(self):
        self.rows = synthetic_guest()

    def mutate(self, selector, key, value):
        rows = copy.deepcopy(self.rows)
        row = next(r for r in rows if selector(r))
        row[key] = str(value)
        with self.assertRaises(ValueError):
            verify_camera_bursts(rows)

    def test_valid_guest_protocol(self):
        proof = verify_camera_bursts(self.rows)
        self.assertEqual(proof["camera_burst_samples"], 48)
        self.assertEqual(proof["camera_forward_delta_px"], 23)
        self.assertEqual(proof["camera_reverse_delta_px"], -23)
        self.assertEqual(proof["camera_dense_sparse_matches"], 2)

    def test_reject_missing_burst(self):
        with self.assertRaises(ValueError):
            verify_camera_bursts(self.rows[:-1])

    def test_reject_fake_delayed_camera(self):
        self.mutate(lambda r: r["event"] == "14" and r["frames"] == "105",
                    "detail", (56 << 16) | 42)

    def test_reject_repeated_tick(self):
        self.mutate(lambda r: r["event"] == "14" and r["frames"] == "105",
                    "frames", 104)

    def test_reject_wrong_direction(self):
        self.mutate(lambda r: r["event"] == "14" and r["frames"] == "208",
                    "detail", (150 << 16) | 150)

    def test_reject_wrong_plane_ratio(self):
        self.mutate(lambda r: r["event"] == "12" and r["frames"] == "107",
                    "detail", (57 << 16) | 40)

    def test_reject_wrong_stage(self):
        self.mutate(lambda r: r["event"] == "14" and r["frames"] == "101",
                    "stage", 1)

    def test_reject_disconnected_sparse_witness(self):
        self.mutate(lambda r: r["event"] == "13" and r["frames"] == "107",
                    "detail", (100 << 16) | 98)

    def test_reject_wrong_controller(self):
        self.mutate(lambda r: r["event"] == "14" and r["frames"] == "101",
                    "controllers", 0)

    def test_reject_camera_jump(self):
        self.mutate(lambda r: r["event"] == "14" and r["frames"] == "112",
                    "detail", (1000 << 16) | 999)

    def test_reject_out_of_range_encoding(self):
        self.mutate(lambda r: r["event"] == "14" and r["frames"] == "112",
                    "detail", -1)


if __name__ == "__main__":
    unittest.main()
