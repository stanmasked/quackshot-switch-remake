"""Static regression checks for the real NeXium reentry host-input harness."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = (ROOT / "tools" / "nexium_rooftop_reentry_probe.sh").read_text(encoding="utf-8")


class RooftopReentryHostProbeTest(unittest.TestCase):
    def test_exit_and_reentry_inputs_have_bounded_retries(self):
        self.assertEqual(SCRIPT.count('for attempt in $(seq 1 3); do'), 2)
        self.assertGreaterEqual(SCRIPT.count('xdotool windowfocus "$window"'), 3)
        self.assertIn("BLOCKED: guest did not confirm START exit after 3 attempts", SCRIPT)
        self.assertIn("BLOCKED: fresh closed bridge not observed after 3 A attempts", SCRIPT)

    def test_reentry_requires_guest_confirmed_transition(self):
        self.assertIn(
            '[ "$seq" = 1 ] && [ "$om" = 1 ] && [ "$os" = 2 ] && [ "$nm" = 0 ] && [ "$ns" = 2 ]',
            SCRIPT,
        )
        self.assertIn(
            '[ "$seq" = 2 ] && [ "$om" = 0 ] && [ "$os" = 2 ] &&',
            SCRIPT,
        )
        # The guest guard is intentionally wrapped across shell lines;
        # assert each required state field instead of one formatting layout.
        self.assertIn('[ "$before" = 1 ] &&', SCRIPT)
        self.assertIn('[ "$after" = 0 ] && [ "$cells" = 0 ]; then', SCRIPT)

    def test_original_blocked_reason_is_not_masked(self):
        self.assertIn(
            'reason="$(grep \'^BLOCKED:\' "$out/input-injection.txt" | tail -n1 || true)"',
            SCRIPT,
        )
        self.assertLess(
            SCRIPT.index('if [ "$rc" != 0 ]; then'),
            SCRIPT.index("for event in ROOFTOP_MENU_SENT"),
        )


if __name__ == "__main__":
    unittest.main()
