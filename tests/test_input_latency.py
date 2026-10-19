"""Stage 95: fail closed on missing or fictional virtual-pad input delays."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_input_latency import verify_latency_profile

def make(run_id):
    delay,poll={1:(0,85),2:(25,90),3:(45,95)}[run_id]
    metadata={"ci_repeat_id":str(run_id),
              "ci_input_edge_delay_ms":str(delay),
              "ci_poll_ms":str(poll)}
    lines=[f"INPUT_TIMING_PROFILE profile={run_id} edge_delay_ms={delay} poll_ms={poll}"]
    for event,proof in (("pit","PIT_LAUNCH_GUEST"),
                        ("roof","ROOF_LAUNCH_GUEST"),
                        ("crate","CRATE_FIRE_GUEST")):
        lines.append(f"{proof} frame=100 x_milli=280000")
        lines.append(f"INPUT_EDGE_DELAY event={event} requested_ms={delay} measured_ms={delay+1 if delay else 0}")
    return metadata,"\n".join(lines)

class InputLatencyTests(unittest.TestCase):
    def test_three_profiles(self):
        for i in (1,2,3):
            with self.subTest(i=i):
                meta,log=make(i)
                result=verify_latency_profile(meta,log,i)
                self.assertEqual(result["input_delay_ms"],int(meta["ci_input_edge_delay_ms"]))
    def test_no_real_sleep_rejected(self):
        meta,log=make(3)
        log=log.replace("measured_ms=46","measured_ms=10")
        with self.assertRaisesRegex(ValueError,"actually elapse"):
            verify_latency_profile(meta,log,3)
    def test_claimed_delay_missing_rejected(self):
        meta,log=make(2)
        log=log.replace("ci_repeat_id=2","ci_repeat_id=2")
        meta["ci_input_edge_delay_ms"]="0"
        with self.assertRaisesRegex(ValueError,"profile mismatch"):
            verify_latency_profile(meta,log,2)
    def test_wrong_matrix_profile(self):
        meta,log=make(1)
        with self.assertRaisesRegex(ValueError,"runner metadata identity"):
            verify_latency_profile(meta,log,2)
    def test_duplicate_delay_log(self):
        meta,log=make(2)
        log+="\nINPUT_EDGE_DELAY event=pit requested_ms=25 measured_ms=26"
        with self.assertRaisesRegex(ValueError,"duplicate"):
            verify_latency_profile(meta,log,2)
    def test_missing_roof_edge(self):
        meta,log=make(3)
        log="\n".join(x for x in log.splitlines() if "event=roof" not in x)
        with self.assertRaisesRegex(ValueError,"missing measured"):
            verify_latency_profile(meta,log,3)
    def test_missing_key_event(self):
        meta,log=make(1)
        log=log.replace("PIT_LAUNCH_GUEST", "FAKE_PIT")
        with self.assertRaisesRegex(ValueError,"not linked"):
            verify_latency_profile(meta,log,1)
    def test_reordered_input_edge(self):
        meta,log=make(2)
        log=log.replace("ROOF_LAUNCH_GUEST frame=100 x_milli=280000\nINPUT_EDGE_DELAY event=roof requested_ms=25 measured_ms=26",
                        "INPUT_EDGE_DELAY event=roof requested_ms=25 measured_ms=26\nROOF_LAUNCH_GUEST frame=100 x_milli=280000")
        with self.assertRaisesRegex(ValueError,"does not precede"):
            verify_latency_profile(meta,log,2)

if __name__=="__main__":
    unittest.main()
