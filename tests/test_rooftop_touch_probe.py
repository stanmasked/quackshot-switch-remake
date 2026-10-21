"""Regression checks for fail-closed NeXium startup in the no-fire touch probe."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.verify_nested_nexium_route import embedded_program, check_route

SCRIPT_PATH = ROOT / "tools/nexium_rooftop_touch_probe.sh"
SCRIPT = SCRIPT_PATH.read_text(encoding="utf-8")
INNER = embedded_program(SCRIPT)


class RooftopTouchStartupTest(unittest.TestCase):
    def test_window_appearing_late_is_polled_with_a_finite_budget(self):
        self.assertIn('for startup_poll in $(seq 1 80)', INNER)
        self.assertIn('xdotool search --onlyvisible --name NeXium 2>/dev/null', INNER)
        self.assertIn('kill -0 "$emu_pid"', INNER)
        self.assertIn('BLOCKED: no emulator window after bounded startup poll', INNER)
        self.assertNotIn("sleep 5" + chr(10) + "  window=", INNER)

    def test_menu_input_waits_for_authenticated_guest_frames(self):
        self.assertIn('for boot_poll in $(seq 1 100)', INNER)
        self.assertIn('[guest-present] queued=300', INNER)
        self.assertIn('NEXIUM_GUEST_FRAMES_READY', INNER)
        self.assertIn('BLOCKED: no authenticated NRO guest frames', INNER)
        self.assertLess(INNER.index('NEXIUM_GUEST_FRAMES_READY'),
                        INNER.index('xdotool key --clearmodifiers --delay 120 c'))

    def test_all_interactive_routes_share_bounded_guest_startup(self):
        scripts = (
            "tools/nexium_rooftop_probe.sh",
            "tools/nexium_rooftop_red_probe.sh",
            "tools/nexium_rooftop_reentry_probe.sh",
            "tools/nexium_rooftop_second_red_probe.sh",
            "tools/nexium_rooftop_yellow_rearm_probe.sh",
            "tools/nexium_rooftop_yellow_reset_probe.sh",
            "tools/nexium_rooftop_red_to_yellow_probe.sh",
            "tools/nexium_rooftop_touch_probe.sh",
        )
        for relative in scripts:
            with self.subTest(script=relative):
                path = ROOT / relative
                inner = embedded_program(path.read_text(encoding="utf-8"))
                self.assertIn("for startup_poll in $(seq 1 80)", inner)
                self.assertIn("[guest-present] queued=300", inner)
                self.assertIn("for boot_poll in $(seq 1 100)", inner)
                self.assertNotIn("sleep 5" + chr(10) + "  window=", inner)
                self.assertLess(inner.index("NEXIUM_GUEST_FRAMES_READY"),
                                inner.index("xdotool key"))
                self.assertEqual(check_route(path)["status"], "PASS")

    def test_nested_shell_remains_valid(self):
        verdict = check_route(SCRIPT_PATH)
        self.assertEqual(verdict["status"], "PASS")


if __name__ == "__main__":
    unittest.main()
