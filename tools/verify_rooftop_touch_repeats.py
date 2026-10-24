#!/usr/bin/env python3
"""Two independent live NeXium runs: hero body cannot activate ROOFTOP switch."""
import json
import pathlib
import sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_rooftop_guest_trace import read_trace
from tools.verify_rooftop_touch_trace import verify_touch

# GitHub release asset SHA-256, validated in two genuine emulator guest runs.
PIN = "d08e89d15632c2ba3ef0521635286793885ac1eaabed6ab93f2d0ec7d5c10180"

class TouchRepeatError(ValueError):
    pass

def verify_two(root):
    root=pathlib.Path(root)
    reports=[]
    fingerprints=set()
    for profile in (1,2):
        path=root/("rooftop-touch-evidence-"+str(profile))
        if not path.is_dir():
            raise TouchRepeatError("missing independent touch evidence")
        data=(path/"metadata.txt").read_text(encoding="utf-8").splitlines()
        meta=dict(line.split("=",1) for line in data if "=" in line)
        if meta.get("profile")!=str(profile) or meta.get("source")!="real_virtual_controller_rooftop_touch_guest":
            raise TouchRepeatError("wrong or reused profile/source")
        sha=meta.get("nro_sha256","")
        if len(sha)!=64 or any(c not in "0123456789abcdef" for c in sha):
            raise TouchRepeatError("invalid NRO fingerprint")
        if meta.get("runner_exit")!="0" or meta.get("emulator_sha256_expected")!=PIN or meta.get("emulator_sha256_observed")!=PIN:
            raise TouchRepeatError("host route or pinned emulator verification absent")
        if "OK" not in (path/"checksum.txt").read_text(encoding="utf-8"):
            raise TouchRepeatError("emulator download failed integrity check")
        if not (path/"result.txt").read_text(encoding="utf-8").startswith("PASS: real virtual-controller touch without fire"):
            raise TouchRepeatError("no authenticated negative input path")
        host=(path/"input-injection.txt").read_text(encoding="utf-8")
        events=["ROOFTOP_MENU_SENT","ROOFTOP_APPROACH_GUEST",
                "ROOFTOP_TOUCH_GUEST","ROOFTOP_NO_BRIDGE_GUEST",
                "ROOFTOP_EXIT_SENT"]
        indices=[host.find(x) for x in events]
        if min(indices)<0 or indices!=sorted(indices):
            raise TouchRepeatError("missing or reordered host real-button events")
        if "ROOFTOP_FIRE_SENT" in host:
            raise TouchRepeatError("host fired during no-FIRE trial")
        result=verify_touch(read_trace(path/"guest-rooftop.csv"))
        saved=json.loads((path/"rooftop-touch-summary.json").read_text(encoding="utf-8"))
        if result!=saved:
            raise TouchRepeatError("saved guest result does not match original CSV")
        reports.append(result)
        fingerprints.add(sha)
    if len(fingerprints)!=1:
        raise TouchRepeatError("guest sessions used different Nintendo Switch NROs")
    diff=abs(reports[0]["touch_frame"]-reports[1]["touch_frame"])
    if diff>180:
        raise TouchRepeatError("independent input profile timing spread excessive")
    return dict(status="PASS",profiles=[1,2],nro_sha256=next(iter(fingerprints)),
                touch_frame_spread=diff,runs=reports)

def main(args):
    if len(args)!=3:
        raise SystemExit("usage: verify_rooftop_touch_repeats.py EVIDENCE_ROOT SUMMARY_JSON")
    try: result=verify_two(args[1])
    except (ValueError,OSError) as exc:
        raise SystemExit("FAIL: NeXium no-fire touch: "+str(exc)) from exc
    pathlib.Path(args[2]).write_text(json.dumps(result,sort_keys=True)+"\n",encoding="utf-8")
    print("NEXIUM_TWO_NO_FIRE_ROOFTOP_TOUCHES_PASS",json.dumps(result,sort_keys=True))
if __name__=="__main__":
    main(sys.argv)
