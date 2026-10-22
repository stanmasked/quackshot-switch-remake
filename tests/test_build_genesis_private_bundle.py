"""Stage203: numerical research bundle is complete, auditable, ROM-free."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_genesis_private_bundle as bundle


class GenesisBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qs-stage203-")
        self.addCleanup(self.temp.cleanup)
        self.dest = Path(self.temp.name) / "rom-free-emulator"
        self.dest.mkdir()
        self.binary = b"NOT_A_GAME_SYNTHETIC_CORE_TEST"
        (self.dest / bundle.CORE_NAME).write_bytes(self.binary)

    def create(self):
        return bundle.build(ROOT, self.dest)

    def test_transitive_modules_contain_private_entrypoints(self):
        modules = bundle.resolve_modules(ROOT)
        for module in bundle.ENTRYPOINTS:
            self.assertIn(module, modules)
        for dependency in (
            "genesis_blue_roi_numeric.py",
            "private_genesis_input_guard.py",
            "genesis_vdp_scroll_probe.py",
            "private_genesis_report_output.py",
            "genesis_libretro_probe.py",
            "reference_evidence_integrity.py",
        ):
            self.assertIn(dependency, modules)
        self.assertEqual(len(set(modules)), len(modules))
        self.assertGreater(len(modules), 10)

    def test_packaged_public_numeric_research_replays_without_original_media(self):
        manifest = self.create()
        self.assertEqual(manifest["schema"], bundle.SCHEMA)
        self.assertFalse(manifest["original_game_media_in_bundle"])
        self.assertFalse(manifest["historical_original_run_reproduced_by_packaging"])
        self.assertFalse(manifest["safe_to_modify_switch_physics"])
        self.assertFalse(manifest["packaged_core_matches_historical_binary"])
        self.assertEqual(bundle.verify(self.dest), manifest)
        self.assertIn("tools/genesis_city_video_vdp_bracket_probe.py",
                      manifest["files"])
        self.assertIn("src/qs_game.c", manifest["files"])
        self.assertIn("research/observations/user_rom_vdp_hscroll_2026-10-18.json",
                      manifest["files"])
        self.assertFalse(any(Path(p).suffix in bundle.FORBIDDEN
                             for p in manifest["files"]))

    def test_forged_game_rom_in_archive_is_blocked(self):
        self.create()
        (self.dest / "OriginalQuackShot.gen").write_bytes(b"fake")
        with self.assertRaisesRegex(ValueError, "game media"):
            bundle.verify(self.dest)

    def test_unknown_numeric_or_python_file_is_blocked(self):
        self.create()
        (self.dest / "tools" / "unapproved_file.json").write_text("{}")
        with self.assertRaisesRegex(ValueError, "extra managed"):
            bundle.verify(self.dest)

    def test_tampered_dependency_and_compiled_core_are_blocked(self):
        self.create()
        mod = self.dest / "tools/genesis_libretro_probe.py"
        mod.write_text(mod.read_text() + "# modification\n")
        with self.assertRaisesRegex(ValueError, "SHA mismatch"):
            bundle.verify(self.dest)
        mod.write_bytes((ROOT / "tools/genesis_libretro_probe.py").read_bytes())
        self.assertEqual(bundle.verify(self.dest)["schema"], bundle.SCHEMA)
        (self.dest / bundle.CORE_NAME).write_bytes(b"replaced")
        with self.assertRaisesRegex(ValueError, "SHA mismatch"):
            bundle.verify(self.dest)

    def test_manifest_cannot_claim_historical_core_match_without_same_hash(self):
        self.create()
        manifest_path = self.dest / bundle.MANIFEST
        forged = json.loads(manifest_path.read_text())
        forged["packaged_core_matches_historical_binary"] = True
        manifest_path.write_text(json.dumps(forged))
        with self.assertRaisesRegex(ValueError, "binary compatibility"):
            bundle.verify(self.dest)

    def test_rebuild_refuses_existing_outputs(self):
        self.create()
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.create()

    def test_missing_dependency_is_blocked(self):
        self.create()
        (self.dest / "tools/genesis_blue_roi_numeric.py").unlink()
        with self.assertRaisesRegex(ValueError, "missing or extra"):
            bundle.verify(self.dest)

    def test_existing_compiler_sha_receipt_is_verified(self):
        self.create()
        import hashlib
        receipt = self.dest / "emulator-sha256.txt"
        receipt.write_text(hashlib.sha256(self.binary).hexdigest() +
                           "  emulator-core/genesis_plus_gx_libretro.so\n")
        self.assertEqual(bundle.verify(self.dest)["schema"], bundle.SCHEMA)
        receipt.write_text("0" * 64 + "  binary.so\n")
        with self.assertRaisesRegex(ValueError, "core SHA summary"):
            bundle.verify(self.dest)

    def test_symlink_bundle_exfiltration_is_blocked(self):
        self.create()
        fake = Path(self.temp.name) / "outside.txt"
        fake.write_text("untrusted")
        (self.dest / "tools" / "untrusted.py").symlink_to(fake)
        with self.assertRaisesRegex(ValueError, "symlink"):
            bundle.verify(self.dest)


if __name__ == "__main__":
    unittest.main()
