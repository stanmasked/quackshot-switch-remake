"""Stage190: actual libretro harness sampling order, with a fake core.

This test never loads a game, original ROM, real emulator, sprites, audio
or source RAM. It validates our Python callback ordering using synthetic
4-byte observations in a deliberately fake 64 KiB memory buffer.
"""
import ctypes as C
import sys
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import genesis_libretro_probe as libretro
import genesis_city_fractional_fall_probe as fall


class FakeLibretroCore:
    """Minimal deterministic fake implementing just the tested API."""
    def __init__(self, video_counts=None, video_before_load=False):
        self.video_counts = video_counts or {}
        self.video_before_load = video_before_load
        self.memory = C.create_string_buffer(65536)
        self.frame = 0
        self.events = []
        self.initialized = False

    def retro_set_environment(self, cb): self.env = cb
    def retro_set_video_refresh(self, cb): self.video = cb
    def retro_set_audio_sample(self, cb): self.audio = cb
    def retro_set_audio_sample_batch(self, cb): self.audio_batch = cb
    def retro_set_input_poll(self, cb): self.poll = cb
    def retro_set_input_state(self, cb): self.input = cb

    def retro_init(self):
        self.initialized = True

    def retro_load_game(self, game_info):
        self.events.append("load")
        if self.video_before_load:
            self.video(None, 0, 0, 0)
        return True

    def retro_set_controller_port_device(self, port, device):
        self.events.append("port")

    def retro_get_system_av_info(self, address):
        av = C.cast(address, C.POINTER(libretro.AvInfo))
        av[0].timing.fps = 60.0

    def retro_get_memory_size(self, kind):
        if kind != libretro.RETRO_MEMORY_SYSTEM_RAM:
            raise AssertionError("wrong memory type")
        return 65536

    def retro_get_memory_data(self, kind):
        self.events.append(("RAM_read", self.frame))
        return C.addressof(self.memory)

    def retro_run(self):
        self.events.append(("run_started", self.frame))
        self.poll()
        pressed = int(self.input(0, libretro.RETRO_DEVICE_JOYPAD, 0,
                                 libretro.BUTTONS["RIGHT"]))
        self.memory[0] = bytes([self.frame + 1])
        self.memory[1] = bytes([pressed])
        for _ in range(self.video_counts.get(self.frame, 1)):
            self.video(None, 0, 0, 0)
        self.events.append(("run_completed", self.frame))
        self.frame += 1

    def retro_unload_game(self):
        self.events.append("unload")

    def retro_deinit(self):
        self.initialized = False
        self.events.append("deinit")


class LibretroSamplingPhaseTests(unittest.TestCase):
    def test_post_run_checkpoint_and_input_on_same_tick(self):
        fake = FakeLibretroCore()
        checkpoints = []

        def on_ram(frame, ram):
            checkpoints.append((frame, ram[0], ram[1]))
            fake.events.append(("checkpoint", frame))

        report = libretro.run_core(
            fake, {"need_fullpath": False, "library_name": "FakeCore",
                   "library_version": "0"},
            b"SYNTHETIC_FAKE_CORE_ONLY", "synthetic-checksum", 4,
            [(1, 3, "RIGHT")], ram_window=(0, 4),
            checkpoint_sink=on_ram, video_stride=1,
        )
        self.assertEqual(checkpoints, [
            (0, 1, 0), (1, 2, 1), (2, 3, 1), (3, 4, 0)
        ])
        self.assertEqual(report["input_nonzero_reads"], 2)
        self.assertEqual(report["frames_video_callbacks"], 4)
        self.assertEqual(report["frames_without_video_callback"], 0)
        self.assertEqual(report["frames_with_multiple_video_callbacks"], 0)
        self.assertEqual(report["video_callbacks_outside_retro_run"], 0)
        self.assertEqual(report["ram_checkpoint_phase"],
                         "post_retro_run_before_next_input")
        self.assertEqual(report["video_callback_phase"], "during_retro_run")
        self.assertEqual([j["frame"] for j in report["ram_checkpoints"]],
                         [0, 1, 2, 3])
        for frame in range(4):
            self.assertLess(
                fake.events.index(("run_started", frame)),
                fake.events.index(("run_completed", frame)))
            self.assertLess(
                fake.events.index(("run_completed", frame)),
                fake.events.index(("RAM_read", frame + 1)))
            self.assertLess(
                fake.events.index(("RAM_read", frame + 1)),
                fake.events.index(("checkpoint", frame)))
        self.assertEqual(fake.events[-2:], ["unload", "deinit"])
        self.assertFalse(fake.initialized)

    def test_private_probe_rejects_wrong_callback_phase_early(self):
        # Stage188 must not accept a core whose sampling contract
        # differs even if it claims to have produced all frame callbacks.
        bad = {
            "frames_video_callbacks": fall.TOTAL,
            "ram_checkpoint_phase": "pre_retro_run",
            "video_callback_phase": "during_retro_run",
        }
        with mock.patch.object(fall, "run_core", return_value=bad):
            with self.assertRaisesRegex(ValueError, "phase contract"):
                fall.measure(None, {}, Path("/private/synthetic-not-used"),
                             b"fake", fall.EXPECTED_ROM_SHA256,
                             verify_inputs=lambda: None)

    def test_equal_total_video_callbacks_can_hide_bad_per_frame_cadence(self):
        # Video frame 1 is skipped; frame 2 sends two callbacks instead.
        # A naive total-count assertion would accept this trace.
        fake = FakeLibretroCore(video_counts={1: 0, 2: 2})
        report = libretro.run_core(
            fake, {"need_fullpath": False}, b"fake", "synthetic",
            4, [], ram_window=(0, 4), checkpoint_sink=lambda *_: None
        )
        self.assertEqual(report["frames_video_callbacks"], 4)
        self.assertEqual(report["frames_without_video_callback"], 1)
        self.assertEqual(report["frames_with_multiple_video_callbacks"], 1)
        self.assertEqual(report["video_callbacks_outside_retro_run"], 0)

    def test_load_time_video_callback_is_not_mislabeled_as_during_run(self):
        fake = FakeLibretroCore(video_before_load=True)
        report = libretro.run_core(
            fake, {"need_fullpath": False}, b"fake", "synthetic",
            2, [], ram_window=(0, 2), checkpoint_sink=lambda *_: None
        )
        self.assertEqual(report["frames_video_callbacks"], 3)
        self.assertEqual(report["frames_without_video_callback"], 0)
        self.assertEqual(report["frames_with_multiple_video_callbacks"], 0)
        self.assertEqual(report["video_callbacks_outside_retro_run"], 1)

    def test_private_probe_rejects_bad_video_cadence_before_using_ram(self):
        bad = {
            "frames_video_callbacks": fall.TOTAL,
            "ram_checkpoint_phase": "post_retro_run_before_next_input",
            "video_callback_phase": "during_retro_run",
            "frames_without_video_callback": 1,
            "frames_with_multiple_video_callbacks": 1,
            "video_callbacks_outside_retro_run": 0,
        }
        with mock.patch.object(fall, "run_core", return_value=bad):
            with self.assertRaisesRegex(ValueError, "video callback cadence"):
                fall.measure(None, {}, Path("/private/synthetic-not-used"),
                             b"fake", fall.EXPECTED_ROM_SHA256,
                             verify_inputs=lambda: None)

    def test_private_probe_rejects_video_callbacks_outside_run(self):
        bad = {
            "frames_video_callbacks": fall.TOTAL,
            "ram_checkpoint_phase": "post_retro_run_before_next_input",
            "video_callback_phase": "during_retro_run",
            "frames_without_video_callback": 0,
            "frames_with_multiple_video_callbacks": 0,
            "video_callbacks_outside_retro_run": 1,
        }
        with mock.patch.object(fall, "run_core", return_value=bad):
            with self.assertRaisesRegex(ValueError, "video callback cadence"):
                fall.measure(None, {}, Path("/private/synthetic-not-used"),
                             b"fake", fall.EXPECTED_ROM_SHA256,
                             verify_inputs=lambda: None)

    def test_phase_marker_not_original_cpu_cycle_assertion(self):
        # The harness constrains its own callbacks, not CPU or VDP timing.
        fake = FakeLibretroCore()
        report = libretro.run_core(
            fake, {"need_fullpath": False}, b"fake", "synthetic", 2, [],
            ram_window=(0, 2), checkpoint_sink=lambda *_: None)
        self.assertEqual(report["ram_checkpoint_phase"],
                         "post_retro_run_before_next_input")
        self.assertNotIn("original_cpu_instruction_phase", report)
        self.assertNotIn("original_vdp_scanout_phase", report)


if __name__ == "__main__":
    unittest.main()
