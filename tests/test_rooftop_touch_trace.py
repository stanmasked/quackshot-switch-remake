"""Strict no-FIRE switch-contact verifier and two-profile regression tests."""
import csv
import json
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_guest_trace import COLUMNS,SOURCE
from tools.verify_rooftop_touch_trace import TouchError,verify_touch
from tools.verify_rooftop_touch_repeats import TouchRepeatError,verify_two

def example():
    rows=[]
    for i in range(320):
        # Native prototype movement, first dashing then slow walking:
        # 40px -> 629px -> 699.3px, then NO fire and no movement.
        x=40000+3100*min(i,190)+1900*min(max(i-190,0),37)
        vals=dict(seq=i,frame=1000+i,mode=1,stage=2,
            x_milli=x,y_milli=188000,grounded=1,
            input_bits=2 if i<=227 else 0,
            shot_x_before_milli=-1,shot_vx_before_milli=0,
            shot_red_before=0,shot_active_before=0,shot_active_after=0,
            bridge_before=0,bridge_after=0,bridge_cells=0,source=SOURCE)
        rows.append({k:str(vals[k]) for k in COLUMNS})
    return rows

class TouchSamples(unittest.TestCase):
    def reject(self,mutation):
        rows=example()
        mutation(rows)
        with self.assertRaises(TouchError):
            verify_touch(rows)
    def test_realistic_safe_contact(self):
        r=verify_touch(example())
        self.assertEqual(r["status"],"PASS")
        self.assertGreater(r["contact_samples"],50)
        self.assertEqual(r["bridge_solid_cells"],0)
    def test_fire_press_rejected(self):
        self.reject(lambda rows: rows[196].update(input_bits="10"))
    def test_projectile_rejected(self):
        self.reject(lambda rows: rows[199].update(shot_active_after="1"))
    def test_red_projectile_rejected(self):
        self.reject(lambda rows: rows[210].update(shot_red_before="1"))
    def test_bridge_activation_rejected(self):
        self.reject(lambda rows: rows[255].update(bridge_after="1",bridge_cells="6"))
    def test_partial_bridge_cells_rejected(self):
        self.reject(lambda rows: rows[255].update(bridge_cells="2"))
    def test_stage_forgery_rejected(self):
        self.reject(lambda rows: rows[100].update(stage="0"))
    def test_missing_frame_rejected(self):
        self.reject(lambda rows: rows[100].update(frame=rows[99]["frame"]))
    def test_teleport_to_target_rejected(self):
        self.reject(lambda rows: rows[200].update(x_milli="699300"))
    def test_respawn_after_contact_rejected(self):
        self.reject(lambda rows: rows[310].update(x_milli="40000"))
    def test_no_real_contact_rejected(self):
        self.reject(lambda rows: [row.update(x_milli="640000") for row in rows])
    def test_no_ground_contact_rejected(self):
        self.reject(lambda rows: [row.update(grounded="0") for row in rows[225:]])
    def test_corrupt_source_rejected(self):
        self.reject(lambda rows: rows[12].update(source="host_state_injected"))

class TouchRepeats(unittest.TestCase):
    def setUp(self):
        tmp=tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root=pathlib.Path(tmp.name)
        for p in (1,2):
            d=self.root/("rooftop-touch-evidence-"+str(p))
            d.mkdir()
            (d/"metadata.txt").write_text(
                f"profile={p}\nrunner_exit=0\n"+
                "source=real_virtual_controller_rooftop_touch_guest\n"+
                "emulator_sha256_expected=8a6e7a72da657255b0523071fc065ccd560712af0c6b9895aadcb9df65207642\n"+
                "emulator_sha256_observed=8a6e7a72da657255b0523071fc065ccd560712af0c6b9895aadcb9df65207642\n"+
                "nro_sha256="+"a"*64+"\n")
            (d/"checksum.txt").write_text("archive: OK\n")
            (d/"result.txt").write_text(
                "PASS: real virtual-controller touch without fire keeps bridge closed\n")
            (d/"input-injection.txt").write_text(
                "ROOFTOP_MENU_SENT\nROOFTOP_APPROACH_GUEST frame=1100\n"+
                "ROOFTOP_TOUCH_GUEST frame=1220\nROOFTOP_NO_BRIDGE_GUEST frame=1240\n"+
                "ROOFTOP_EXIT_SENT=virtual_START\n")
            rows=example()
            with (d/"guest-rooftop.csv").open("w",newline="") as fp:
                writer=csv.DictWriter(fp,fieldnames=COLUMNS)
                writer.writeheader()
                writer.writerows(rows)
            (d/"rooftop-touch-summary.json").write_text(json.dumps(verify_touch(rows)))
    def test_two_good_guests(self):
        self.assertEqual(verify_two(self.root)["status"],"PASS")
    def test_wrong_profile(self):
        p=self.root/"rooftop-touch-evidence-2"/"metadata.txt"
        p.write_text(p.read_text().replace("profile=2","profile=1"))
        with self.assertRaises(TouchRepeatError):verify_two(self.root)
    def test_mismatched_guest_binary(self):
        p=self.root/"rooftop-touch-evidence-2"/"metadata.txt"
        p.write_text(p.read_text().replace("nro_sha256="+"a"*64,"nro_sha256="+"b"*64))
        with self.assertRaises(TouchRepeatError):verify_two(self.root)
    def test_fake_host_fire(self):
        p=self.root/"rooftop-touch-evidence-1"/"input-injection.txt"
        p.write_text(p.read_text()+"ROOFTOP_FIRE_SENT\n")
        with self.assertRaises(TouchRepeatError):verify_two(self.root)
    def test_stale_emulator_pin_rejected(self):
        p=self.root/"rooftop-touch-evidence-1"/"metadata.txt"
        p.write_text(p.read_text().replace("emulator_sha256_expected=8a6e7a72da657255b0523071fc065ccd560712af0c6b9895aadcb9df65207642",
            "emulator_sha256_expected=c933fbbcf8d1b011de8d4a79c9d8eb55c0678a305947af9388ddb40ce5960a66"))
        with self.assertRaises(TouchRepeatError):verify_two(self.root)
    def test_mismatched_observed_emulator_rejected(self):
        p=self.root/"rooftop-touch-evidence-2"/"metadata.txt"
        p.write_text(p.read_text().replace("emulator_sha256_observed=8a6e7a72da657255b0523071fc065ccd560712af0c6b9895aadcb9df65207642",
            "emulator_sha256_observed="+"f"*64))
        with self.assertRaises(TouchRepeatError):verify_two(self.root)
    def test_absent_observed_emulator_rejected(self):
        p=self.root/"rooftop-touch-evidence-2"/"metadata.txt"
        p.write_text(p.read_text().replace("emulator_sha256_observed=8a6e7a72da657255b0523071fc065ccd560712af0c6b9895aadcb9df65207642\n",""))
        with self.assertRaises(TouchRepeatError):verify_two(self.root)
    def test_missing_emulator_integrity(self):
        p=self.root/"rooftop-touch-evidence-1"/"checksum.txt"
        p.write_text("bad digest")
        with self.assertRaises(TouchRepeatError):verify_two(self.root)
    def test_tampered_summary(self):
        p=self.root/"rooftop-touch-evidence-2"/"rooftop-touch-summary.json"
        p.write_text("{}")
        with self.assertRaises(TouchRepeatError):verify_two(self.root)

if __name__=="__main__":
    unittest.main()
