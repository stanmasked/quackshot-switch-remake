"""Stage197: invented numeric contact examples, no original ROM or collision data."""
import copy
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from city_original_contact_candidate import analyze, Q16
from reference_evidence_integrity import read_inputs, validate


class ContactCandidateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.obs, cls.source = read_inputs(ROOT)
        cls.provenance = validate(cls.obs, cls.source)

    def sample(self, offset=20*Q16):
        # Entirely fabricated: plane locations were NOT recovered from ROM.
        rows = []
        for site, original_ram in (("upper", 176), ("lower", 208)):
            for repeat in (1, 2):
                rows.append({
                    "site": site, "repeat": repeat,
                    "player_ram_y_q16": original_ram * Q16,
                    "independent_contact_plane_y_q16": original_ram*Q16+offset,
                })
        return {
            "schema": "quackshot_private_genesis_contact_planes_numeric_v1",
            "status": "PRIVATE_CONTACT_NUMERIC_UNATTESTED",
            "reference_rom_sha256": self.provenance["rom_sha256"],
            "genesis_core_sha256": self.provenance["genesis_core_sha256"],
            "ram_checkpoint_phase": "post_retro_run_before_next_input",
            "contact_plane_method": "independent_collision_boundary_experiment",
            "contains_original_media_or_ram": False,
            "samples": rows,
        }

    def test_absent_contact_planes_remain_explicitly_unknown(self):
        result = analyze(self.obs, self.source)
        self.assertEqual(result["status"],
                         "ORIGINAL_COLLISION_SURFACES_NOT_MEASURED")
        self.assertIsNone(result["contact_offset_candidate_q16"])
        self.assertIsNone(result["contact_offset_candidate_px"])
        self.assertFalse(result["independent_original_contact_planes_available"])
        self.assertFalse(result["safe_to_modify_switch_physics"])

    def test_synthetic_consistent_candidate_is_never_attested(self):
        data = self.sample()
        result = analyze(self.obs, self.source, data)
        self.assertEqual(result["contact_offset_candidate_q16"], 20*Q16)
        self.assertEqual(result["contact_offset_candidate_px"], 20)
        self.assertEqual(result["status"],
                         "EXTERNAL_CONTACT_NUMBERS_NOT_INDEPENDENTLY_ATTESTED")
        self.assertFalse(result["original_anchor_independently_attested"])
        self.assertFalse(result["original_hitbox_size_verified"])
        self.assertFalse(result["safe_to_modify_switch_physics"])
        self.assertNotIn("samples", result)

    def test_non_integer_q16_wrong_hash_phase_and_payload_rejected(self):
        modifications = (
            ("reference_rom_sha256", "0"*64),
            ("genesis_core_sha256", "1"*64),
            ("ram_checkpoint_phase", "pre_retro_run"),
            ("contains_original_media_or_ram", True),
        )
        for field, value in modifications:
            with self.subTest(field=field):
                witness = self.sample()
                witness[field] = value
                with self.assertRaises(ValueError):
                    analyze(self.obs, self.source, witness)
        for value in (False, 176*Q16+0.25, "11534336", None):
            with self.subTest(q16=value):
                witness = self.sample()
                witness["samples"][0]["player_ram_y_q16"] = value
                with self.assertRaises(ValueError):
                    analyze(self.obs, self.source, witness)

    def test_misaligned_repetitions_and_plane_delta_rejected(self):
        for index, column, delta in (
            (1, "independent_contact_plane_y_q16", 1),
            (2, "independent_contact_plane_y_q16", -Q16),
            (0, "player_ram_y_q16", -Q16),
        ):
            with self.subTest(index=index,column=column):
                witness = self.sample()
                witness["samples"][index][column] += delta
                with self.assertRaises(ValueError):
                    analyze(self.obs, self.source, witness)

    def test_duplicate_or_missing_rows_and_forbidden_fields_rejected(self):
        witness = self.sample()
        witness["samples"][3] = copy.deepcopy(witness["samples"][2])
        with self.assertRaises(ValueError):
            analyze(self.obs, self.source, witness)
        for field in ("raw_ram_bytes", "sprite_pixels", "screenshot"):
            witness = self.sample()
            witness[field] = "NOT_ALLOWED"
            with self.subTest(field=field), self.assertRaises(ValueError):
                analyze(self.obs, self.source, witness)
        witness = self.sample()
        witness["samples"][0]["raw_ram"] = "NOT_ALLOWED"
        with self.assertRaises(ValueError):
            analyze(self.obs, self.source, witness)

    def test_original_numeric_source_changes_fail(self):
        witness = self.sample()
        observations = copy.deepcopy(self.obs)
        observations["obstacles"]["ledge"]["lower_floor_world_xy"][1] = 209
        with self.assertRaises(ValueError):
            analyze(observations, self.source, witness)

    def test_cli_without_witness_outputs_no_claims(self):
        cmd = [sys.executable, str(ROOT/"tools/city_original_contact_candidate.py"),
               "--root", str(ROOT)]
        result = subprocess.run(cmd,capture_output=True,text=True,
                                check=True,timeout=20)
        out = json.loads(result.stdout)
        self.assertIsNone(out["contact_offset_candidate_q16"])
        self.assertFalse(out["safe_to_modify_switch_physics"])

    def test_private_output_permissions_exclusive_and_no_witness_stdout(self):
        with tempfile.TemporaryDirectory(prefix="qs-contact197-") as d:
            directory=Path(d)
            private_file=directory/"witness.json"
            result_file=directory/"report.json"
            private_file.write_text(json.dumps(self.sample()),encoding="utf-8")
            cmd=[sys.executable,str(ROOT/"tools/city_original_contact_candidate.py"),
                 "--root",str(ROOT),"--numeric-witness",str(private_file),
                 "--out",str(result_file)]
            result=subprocess.run(cmd,capture_output=True,text=True,
                                  check=True,timeout=20)
            self.assertEqual(result.stdout,"")
            self.assertEqual(stat.S_IMODE(result_file.stat().st_mode),0o600)
            report=json.loads(result_file.read_text(encoding="utf-8"))
            self.assertEqual(report["contact_offset_candidate_q16"],20*Q16)
            self.assertFalse(report["safe_to_modify_switch_physics"])
            second=subprocess.run(cmd,capture_output=True,text=True,timeout=20)
            self.assertNotEqual(second.returncode,0)
            self.assertNotIn(str(result_file),second.stderr)
            self.assertEqual(json.loads(result_file.read_text()),report)


if __name__=="__main__":
    unittest.main()
