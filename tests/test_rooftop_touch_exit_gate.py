"""Host-only regression: held START must stop the real ROOFTOP witness stream.

The simulator writes synthetic CSV lines; production gameplay witnesses and
strict guest verifiers are unchanged. No ROM or game-state injection.
"""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.verify_nested_nexium_route import check_route, embedded_program

SCRIPT = ROOT / "tools" / "nexium_rooftop_touch_probe.sh"
BEGIN = "# BEGIN ROOFTOP START EXIT GATE"
END = "# END ROOFTOP START EXIT GATE"


class TouchExitAckTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text(encoding="utf-8")
        cls.inner = embedded_program(cls.source)
        assert cls.inner.count(BEGIN) == 1
        assert cls.inner.count(END) == 1
        cls.gate = cls.inner.split(BEGIN, 1)[1].split(END, 1)[0]

    def test_nested_shell_syntax_and_proof_hooks(self):
        self.assertEqual(check_route(SCRIPT)["status"], "PASS")
        for witness in (
            "ROOFTOP_GUEST_LIVE_BEFORE_EXIT",
            "ROOFTOP_EXIT_ATTEMPT=", "ROOFTOP_EXIT_CONFIRMED",
            "ROOFTOP_EXIT_SENT=virtual_START",
            "xdotool keydown --clearmodifiers m", "xdotool keyup m",
            "BLOCKED: native ROOFTOP guest did not acknowledge START exit",
        ):
            self.assertIn(witness, self.gate)
        self.assertIn("ROOFTOP_GUEST_LIVE_BEFORE_EXIT ROOFTOP_EXIT_CONFIRMED ROOFTOP_EXIT_SENT; do", self.source)

    def run_gate(self, acknowledge):
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp)
            guest = path / "guest-rooftop.csv"
            guest.write_text("header\n1\n2\n", encoding="utf-8")
            stop_file = path / "acknowledged"
            fake_bin = path / "bin"
            fake_bin.mkdir()
            xdotool = fake_bin / "xdotool"
            xdotool.write_text(
                "#!/bin/sh\n"
                'if [ "$1" = keyup ] && [ "$2" = m ] && '
                '[ "$SIMULATED_ACCEPT_START" = 1 ]; then\n'
                ' : > "$SIMULATED_STOP_FILE"\n'
                "fi\n",
                encoding="utf-8",
            )
            xdotool.chmod(0o755)
            writer = subprocess.Popen(
                [sys.executable, "-u", "-c", (
                    "import pathlib,sys,time\n"
                    "p=pathlib.Path(sys.argv[1]); stop=pathlib.Path(sys.argv[2])\n"
                    "for i in range(4000):\n"
                    " if stop.exists(): break\n"
                    " with p.open('a') as f: f.write(str(i)+'\\n')\n"
                    " time.sleep(0.02)\n"
                ), str(guest), str(stop_file)],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
            )
            env = dict(os.environ)
            env["PATH"] = str(fake_bin) + os.pathsep + env.get("PATH", "")
            env["SIMULATED_ACCEPT_START"] = "1" if acknowledge else "0"
            env["SIMULATED_STOP_FILE"] = str(stop_file)
            program = (
                "set -euo pipefail\n"
                "window=42\n"
                + "guest=" + shlex.quote(str(guest)) + "\n"
                + self.gate
            )
            try:
                result = subprocess.run(
                    ["bash", "-c", program], capture_output=True,
                    text=True, timeout=16, env=env,
                )
            finally:
                writer.terminate()
                writer.communicate(timeout=5)
            return result

    def test_start_exit_acknowledged_from_stopped_native_stream(self):
        result = self.run_gate(True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("ROOFTOP_EXIT_CONFIRMED", result.stdout)
        self.assertIn("ROOFTOP_EXIT_SENT=virtual_START", result.stdout)

    def test_start_not_received_must_fail_closed(self):
        result = self.run_gate(False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "BLOCKED: native ROOFTOP guest did not acknowledge START exit",
            result.stdout,
        )
        self.assertNotIn("ROOFTOP_EXIT_SENT=virtual_START", result.stdout)


class TouchWindowReadyTests(unittest.TestCase):
    """Only synthetic host stubs: no emulator, no ROM and no state injection."""
    @classmethod
    def setUpClass(cls):
        body = embedded_program(SCRIPT.read_text(encoding="utf-8"))
        begin = "# BEGIN ROOFTOP WINDOW AND GAME READY GATE"
        end = "# END ROOFTOP WINDOW AND GAME READY GATE"
        assert body.count(begin) == 1 and body.count(end) == 1
        cls.gate = body.split(begin, 1)[1].split(end, 1)[0]

    def run_window_gate(self, visible_after, guest_ready):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            bindir = root / "bin"
            bindir.mkdir()
            xdotool = bindir / "xdotool"
            xdotool.write_text(
                "#!/bin/sh\n"
                'if [ "$1" = search ]; then\n'
                ' n=0; [ -f "$SIM_WINDOW_COUNT" ] && n=$(cat "$SIM_WINDOW_COUNT")\n'
                ' n=$((n+1)); printf "%s" "$n" > "$SIM_WINDOW_COUNT"\n'
                ' [ "$SIM_VISIBLE_AFTER" -gt 0 ] && [ "$n" -ge "$SIM_VISIBLE_AFTER" ] && echo 42\n'
                ' exit 0\n'
                'fi\n'
                'exit 0\n',
                encoding="utf-8",
            )
            xdotool.chmod(0o755)
            logs = root / "NeXium" / "logs"
            logs.mkdir(parents=True)
            if guest_ready:
                (logs / "nexium-test.log").write_text(
                    "[guest-present] queued=300\n", encoding="utf-8",
                )
            env = dict(os.environ)
            env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
            env["SIM_WINDOW_COUNT"] = str(root / "count.txt")
            env["SIM_VISIBLE_AFTER"] = str(visible_after)
            program = (
                "set -euo pipefail\n"
                "/bin/sleep 60 & emu_pid=$!\n"
                "trap 'kill \"$emu_pid\" 2>/dev/null || true' EXIT\n"
                "sleep() { :; }\n"
                "XDG_CONFIG_HOME=" + shlex.quote(str(root)) + "\n"
                + self.gate
            )
            result = subprocess.run(
                ["bash", "-c", program],
                capture_output=True, text=True, timeout=12, env=env,
            )
            count_file = root / "count.txt"
            count = int(count_file.read_text()) if count_file.exists() else 0
            return result, count

    def test_window_delayed_and_guest_frames_ready(self):
        result, polls = self.run_window_gate(6, True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertEqual(polls, 6)
        self.assertIn("NEXIUM_WINDOW_READY", result.stdout)
        self.assertIn("NEXIUM_GUEST_FRAMES_READY", result.stdout)

    def test_no_window_never_sends_inputs(self):
        result, polls = self.run_window_gate(0, True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(polls, 80)
        self.assertIn(
            "BLOCKED: no emulator window after bounded startup poll",
            result.stdout,
        )
        self.assertNotIn("NEXIUM_GUEST_FRAMES_READY", result.stdout)

    def test_visible_window_without_nro_frames_blocks(self):
        result, polls = self.run_window_gate(1, False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(polls, 1)
        self.assertIn("NEXIUM_WINDOW_READY", result.stdout)
        self.assertIn(
            "BLOCKED: no authenticated NRO guest frames", result.stdout,
        )
        self.assertNotIn("NEXIUM_GUEST_FRAMES_READY", result.stdout)


if __name__ == "__main__":
    unittest.main()
