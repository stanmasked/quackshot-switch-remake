"""Stage 95: fail closed on missing or fictional virtual-pad input delays."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.verify_input_latency import verify_latency_profile

def make(run_id):
    delay,poll={1:(0,85),2:(25,90),3:(45,95),4:(25,90),5:(45,95)}[run_id]
    metadata={"ci_repeat_id":str(run_id),
              "ci_input_edge_delay_ms":str(delay),
              "ci_poll_ms":str(poll)}
    lines=[f"INPUT_TIMING_PROFILE profile={run_id} edge_delay_ms={delay} poll_ms={poll}",
           "INPUT_TIMER_SOURCE=perl_clock_monotonic",
           "INPUT_TIMER_SCOPE=single_process_sleep"]
    for event,proof in (("pit","PIT_LAUNCH_GUEST"),
                        ("roof","ROOF_LAUNCH_GUEST"),
                        ("crate","CRATE_FIRE_GUEST")):
        lines.append(f"{proof} frame=100 x_milli=280000")
        lines.append(f"INPUT_EDGE_DELAY event={event} requested_ms={delay} measured_ms={delay+1 if delay else 0}")
    return metadata,"\n".join(lines)

class InputLatencyTests(unittest.TestCase):
    def test_three_profiles(self):
        for i in (1,2,3,4,5):
            with self.subTest(i=i):
                meta,log=make(i)
                result=verify_latency_profile(meta,log,i)
                self.assertEqual(result["input_delay_ms"],int(meta["ci_input_edge_delay_ms"]))
    def test_monotonic_marker_required(self):
        meta,log=make(3)
        for altered in (
                log.replace("INPUT_TIMER_SOURCE=perl_clock_monotonic", ""),
                log.replace("INPUT_TIMER_SOURCE=perl_clock_monotonic",
                            "INPUT_TIMER_SOURCE=wall_clock_date"),
                log + "\nINPUT_TIMER_SOURCE=perl_clock_monotonic"):
            with self.subTest(altered=altered[-55:]):
                with self.assertRaisesRegex(ValueError,"monotonic host clock"):
                    verify_latency_profile(meta,altered,3)

    def test_single_process_scope_required(self):
        meta,log=make(3)
        for altered in (
                log.replace("INPUT_TIMER_SCOPE=single_process_sleep",""),
                log.replace("INPUT_TIMER_SCOPE=single_process_sleep",
                            "INPUT_TIMER_SCOPE=two_clock_subprocesses"),
                log+"\nINPUT_TIMER_SCOPE=single_process_sleep"):
            with self.subTest(altered=altered[-60:]):
                with self.assertRaisesRegex(ValueError,"single-process latency"):
                    verify_latency_profile(meta,altered,3)

    def test_stage111_backward_wall_clock_evidence_rejected(self):
        meta,log=make(3)
        # Archived run 39337903710/#3 recorded -765ms for pit B.
        # Do NOT allow it as a valid 45ms injected delay.
        log=log.replace("event=pit requested_ms=45 measured_ms=46",
                        "event=pit requested_ms=45 measured_ms=-765")
        with self.assertRaisesRegex(ValueError,"missing measured"):
            verify_latency_profile(meta,log,3)

    def test_shell_clock_is_monotonic_in_single_sleep_process(self):
        import re
        import subprocess
        script=(Path(__file__).resolve().parents[1] /
                "tools" / "nexium_ci_probe.sh").read_text(encoding="utf-8")
        helper=re.search(r"(?m)^      delayed_edge_ms\(\) \{\n(?:.*\n)*?      \}",script)
        self.assertIsNotNone(helper)
        self.assertIn("clock_gettime(CLOCK_MONOTONIC)",helper.group(0))
        self.assertIn("usleep($delay*1000)",helper.group(0))
        self.assertNotIn("date +%s%3N",script)
        self.assertNotIn("monotonic_ms()",script)
        command=(helper.group(0)+
                 '\nprintf "%s %s" "$(delayed_edge_ms 25)" "$(delayed_edge_ms 45)"')
        result=subprocess.run(["bash","-c",command],check=True,
                              text=True,capture_output=True)
        measured=list(map(int,result.stdout.strip().split()))
        self.assertEqual(len(measured),2)
        self.assertTrue(25<=measured[0]<=775,measured)
        self.assertTrue(45<=measured[1]<=795,measured)

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
