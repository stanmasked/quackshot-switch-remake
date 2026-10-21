"""Regression check for per-workflow PR queue coalescing.

Keep the latest PR revision running without suppressing independent
core/NRO/NeXium jobs or cancelling default-branch verification.
Stdlib-only: suitable for hosted CI and local unittest discovery.
"""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = (
    "core-tests.yml",
    "emulator-runtime.yml",
    "switch-nro.yml",
    "nexium-blocked-roof.yml",
    "nexium-rooftop-guest.yml",
    "nexium-rooftop-touch.yml",
    "nexium-rooftop-reentry.yml",
    "nexium-rooftop-red.yml",
)
GROUP = (
    "group: quackshot-${{ github.workflow }}-"
    "${{ github.event.pull_request.number || github.ref }}"
)
CANCELLATION = (
    "cancel-in-progress: ${{ github.event_name == 'pull_request' }}"
)


class WorkflowConcurrencyTests(unittest.TestCase):
    def test_every_workflow_uses_the_latest_pr_revision(self):
        for filename in WORKFLOWS:
            with self.subTest(filename=filename):
                contents = (ROOT / ".github" / "workflows" / filename).read_text()
                self.assertEqual(contents.count("\nconcurrency:\n"), 1)
                self.assertEqual(contents.count(GROUP), 1)
                self.assertEqual(contents.count(CANCELLATION), 1)

    def test_concurrency_is_a_top_level_key_not_a_job_override(self):
        for filename in WORKFLOWS:
            with self.subTest(filename=filename):
                contents = (ROOT / ".github" / "workflows" / filename).read_text()
                block = contents.index("\nconcurrency:\n")
                jobs = contents.index("\njobs:\n")
                self.assertLess(block, jobs)
                self.assertNotIn("\n  concurrency:\n", contents)

    def test_independent_workflow_names_partition_groups(self):
        names = []
        for filename in WORKFLOWS:
            contents = (ROOT / ".github" / "workflows" / filename).read_text()
            name = re.search(r"^name: (.+)$", contents, re.MULTILINE)
            self.assertIsNotNone(name, filename)
            names.append(name.group(1))
        self.assertEqual(len(names), len(set(names)))

    def test_regular_main_pushes_are_not_cancelled(self):
        for filename in WORKFLOWS:
            with self.subTest(filename=filename):
                contents = (ROOT / ".github" / "workflows" / filename).read_text()
                self.assertIn(CANCELLATION, contents)
                self.assertNotIn("cancel-in-progress: true", contents)


if __name__ == "__main__":
    unittest.main()
