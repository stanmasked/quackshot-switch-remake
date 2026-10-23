"""Synthetic-only Stage260 tests. No private ROM, RAM or framebuffer."""
import copy
import importlib.util
from pathlib import Path
import unittest

FILE = Path(__file__).resolve().parents[1] / "tools" / "quackshot_sat_gap_numeric.py"
SPEC = importlib.util.spec_from_file_location("qgap", FILE)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def sequence(start, selector, counts, blues, writes):
    return [dict(frame=start + i, selector=selector, cursor=min(i, 1), flag=10,
                 candidate_sprites=counts[i], blue_pixels=blues[i],
                 animation_writes=writes[i]) for i in range(3)]


def fixture():
    right = sequence(31, 29, [4, 0, 3], [12, 0, 16], [0, 2, 2])
    return {
        "right": right, "right_repeat": copy.deepcopy(right),
        "right_shift": sequence(41, 29, [4, 0, 3], [12, 0, 16], [0, 2, 2]),
        "left": sequence(31, 28, [4, 3, 3], [12, 13, 14], [2, 2, 2]),
        "fire": sequence(31, 19, [4, 4, 4], [12, 14, 14], [2, 2, 2]),
    }


class NumericFrameGapTests(unittest.TestCase):
    def test_matched_synthetic_scenarios(self):
        result = AUDIT.audit_first_frame_gap(fixture())
        self.assertEqual(result["verified_numeric_scenarios"], 5)
        self.assertEqual(result["gap_relative_frame"], 1)
        self.assertFalse(result["original_rom_or_rendered_pixels_verified_by_this_function"])

    def test_all_negative_guards(self):
        for mutate in (
            lambda x: x.pop("left"),
            lambda x: x["right"][1].update(candidate_sprites=1),
            lambda x: x["right"][1].update(blue_pixels=1),
            lambda x: x["right"][0].update(animation_writes=2),
            lambda x: x["left"][1].update(candidate_sprites=0),
            lambda x: x["right_shift"][0].update(frame=40),
            lambda x: x["right_repeat"][2].update(blue_pixels=17),
            lambda x: x["fire"][2].update(cursor=True),
            lambda x: x["right"][1].update(raw_sprite_bytes="bad"),
        ):
            data = fixture()
            mutate(data)
            with self.subTest(mutate=mutate):
                with self.assertRaises(ValueError):
                    AUDIT.audit_first_frame_gap(data)


if __name__ == "__main__":
    unittest.main()
