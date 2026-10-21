"""Regression cases for NeXium's single-quoted nested bash program."""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.verify_nested_nexium_route import (
    RouteSyntaxError, check_route, embedded_program,
)

PREFIX = 'xvfb-run -a -s "-screen 0 1280x720x24" bash -c ' + "'\n"
SUFFIX = '\n' + "' bash " + '"$emu" "$nro" "$out/log" "$out/inputs"'


class NestedGuestShellTests(unittest.TestCase):
    def test_actual_versioned_route_is_valid(self):
        path = ROOT / "tools/nexium_rooftop_red_probe.sh"
        verdict = check_route(path)
        self.assertEqual(verdict["status"], "PASS")
        self.assertGreater(verdict["inner_script_lines"], 75)

    def test_missing_embedded_route(self):
        with self.assertRaises(RouteSyntaxError):
            embedded_program("bash -n tools/runner.sh")

    def test_duplicate_route(self):
        body = PREFIX + "echo ok" + SUFFIX
        with self.assertRaises(RouteSyntaxError):
            embedded_program(body + "\n" + body)

    def test_invalid_awk_single_quote_is_rejected(self):
        body = PREFIX + "awk -F, 'NR>1 {print $2}' input.csv" + SUFFIX
        with self.assertRaisesRegex(RouteSyntaxError, "single quote"):
            embedded_program(body)

    def test_nested_syntax_rejects_unclosed_condition(self):
        import tempfile
        body = PREFIX + "if [ true ]; then echo ok" + SUFFIX
        with tempfile.TemporaryDirectory() as temporary:
            p = pathlib.Path(temporary) / "route.sh"
            p.write_text(body, encoding="utf-8")
            with self.assertRaisesRegex(RouteSyntaxError, "inner bash"):
                check_route(p)

    def test_inner_shell_body_is_extracted_unchanged(self):
        code = "set -euo pipefail\nprintf \"ok\\n\""
        self.assertEqual(embedded_program(PREFIX + code + SUFFIX), code)


class NativeGuestStartupGateTests(unittest.TestCase):
    """All real NeXium routes must wait for both X11 and native NRO frames."""
    ROUTES = (
        "nexium_rooftop_red_probe.sh",
        "nexium_rooftop_reentry_probe.sh",
        "nexium_rooftop_second_red_probe.sh",
        "nexium_rooftop_red_to_yellow_probe.sh",
        "nexium_rooftop_touch_probe.sh",
        "nexium_rooftop_yellow_rearm_probe.sh",
        "nexium_rooftop_yellow_reset_probe.sh",
    )

    def test_every_route_has_bounded_window_and_guest_readiness(self):
        for name in self.ROUTES:
            with self.subTest(route=name):
                path = ROOT / "tools" / name
                source = path.read_text(encoding="utf-8")
                inner = embedded_program(path)
                self.assertEqual(check_route(path)["status"], "PASS")
                self.assertIn("for startup_poll in $(seq 1 80)", inner)
                self.assertIn("for boot_poll in $(seq 1 100)", inner)
                self.assertIn('xdotool search --onlyvisible --name NeXium', inner)
                self.assertIn('grep -qF "[guest-present] queued=300"', inner)
                self.assertIn("NEXIUM_WINDOW_READY", inner)
                self.assertIn("NEXIUM_GUEST_FRAMES_READY", inner)
                self.assertIn(
                    "for event in NEXIUM_WINDOW_READY NEXIUM_GUEST_FRAMES_READY",
                    source,
                )
                self.assertLess(
                    inner.index("NEXIUM_GUEST_FRAMES_READY"),
                    inner.index('xdotool key --clearmodifiers c'),
                )
                self.assertNotIn(
                    'window="$(xdotool search --onlyvisible --name NeXium | tail -n1)"',
                    inner,
                )


if __name__ == "__main__":
    unittest.main()
