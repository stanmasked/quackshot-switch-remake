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
            "ROOFTOP_EXIT_ATTEMPT=", "ROOFTOP_EXIT_CONFIRMED",
            "ROOFTOP_EXIT_SENT=virtual_START",
            "xdotool keydown --clearmodifiers m", "xdotool keyup m",
            "BLOCKED: native ROOFTOP guest did not acknowledge START exit",
        ):
            self.assertIn(witness, self.gate)
        self.assertIn("ROOFTOP_EXIT_CONFIRMED ROOFTOP_EXIT_SENT; do", self.source)

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


if __name__ == "__main__":
    unittest.main()
