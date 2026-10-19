#!/usr/bin/env python3
"""Stage 94: independently verify three live NeXium emulator runs.

A directory named nexium-emulation-evidence-run-N must come from the
corresponding CI matrix job. Each was produced by a separate emulator
process with a fresh, isolated virtual SD. The scripted Stage 88 guest
self-test is NOT evidence of live controller input or full completion.
"""
import json
import pathlib
import re
import sys

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from tools.verify_obstacle_route import verify as verify_obstacle
from tools.verify_postcheck import verify as verify_postcheck, load as load_postcheck
from tools.verify_finish_route import verify as verify_finish, load as load_finish, FIELDS as FINISH_COLUMNS
from tools.verify_live_control import check as verify_live, load as load_live
from tools.verify_live_timeline import validate_timeline, read_csv, MOTION_COLUMNS
from tools.verify_input_latency import verify_latency_profile, PROFILES
from tools.verify_guest_edge_response import verify_edge_responses

EXPECTED_IDS=(1,2,3)
EVENT_KEYS=("pit_jump_tick","pit_cross_tick","roof_jump_tick",
            "wall_cross_tick","checkpoint_tick","shot_tick",
            "crate_broken_tick","spring_tick","heart_tick",
            "ledge_tick","best_clear_frames")
LIMITS={
  "pit_jump_tick":80,"pit_cross_tick":80,"roof_jump_tick":80,
  "wall_cross_tick":80,"checkpoint_tick":80,"shot_tick":100,
  "crate_broken_tick":100,"spring_tick":100,"heart_tick":100,
  "ledge_tick":100,"best_clear_frames":120
}

def _metadata(path):
    lines=path.read_text(encoding="utf-8").splitlines()
    data={}
    for line in lines:
        if "=" not in line:
            raise ValueError("corrupted CI run metadata")
        k,v=line.split("=",1)
        if k in data or not k or not v:
            raise ValueError("duplicate/empty CI metadata")
        data[k]=v
    return data

def _guest_rows(path):
    import csv
    with path.open(encoding="utf-8",newline="") as fp:
        return list(csv.DictReader(fp))

def collect(root,run_id):
    directory=root/f"nexium-emulation-evidence-run-{run_id}"
    if not directory.is_dir():
        raise ValueError(f"missing independently uploaded NeXium run {run_id}")
    required=("metadata.txt","checksum.txt","guest-obstacle.csv",
              "guest-postcheck.csv","guest-finish.csv",
              "guest-runtime.csv","guest-motion.csv",
              "guest-live-control.csv","finish-summary.txt",
              "obstacle-summary.txt","postcheck-summary.txt",
              "input-injection.txt","result.txt","live-timeline-summary.txt",
              "guest-edge-response-summary.txt")
    if any(not (directory/name).is_file() for name in required):
        raise ValueError(f"incomplete evidence in independent run {run_id}")
    meta=_metadata(directory/"metadata.txt")
    if meta.get("ci_repeat_id")!=str(run_id):
        raise ValueError("matrix job ID does not match guest evidence")
    digest=meta.get("nro_sha256","")
    if not re.fullmatch(r"[0-9a-f]{64}",digest):
        raise ValueError("invalid NRO identity in guest metadata")
    check=(directory/"checksum.txt").read_text(encoding="utf-8")
    if "OK" not in check:
        raise ValueError("pinned NeXium binary checksum was not verified")
    if not (directory/"result.txt").read_text(encoding="utf-8").startswith("PASS: guest NRO"):
        raise ValueError("guest first-frame/controller stage verification failed")
    actions=(directory/"input-injection.txt").read_text(encoding="utf-8")
    if "OBSTACLE_INPUT_STEP=10" not in actions or "LIVE_STAGE_CLEAR_GUEST" not in actions:
        raise ValueError("virtual controller did not independently reach map clear")
    latency=verify_latency_profile(meta,actions,run_id)
    edge_summary=(directory/"guest-edge-response-summary.txt").read_text(encoding="utf-8")
    if not edge_summary.startswith("NEXIUM_GUEST_INPUT_EDGES_PASS "):
        raise ValueError("no verified first NRO B/Y input-recognition evidence")
    if not (directory/"finish-summary.txt").read_text(encoding="utf-8").startswith("LIVE_FULL_CITY_GUEST_PASS "):
        raise ValueError("missing guest-generated complete-city proof")
    if not (directory/"obstacle-summary.txt").read_text(encoding="utf-8").startswith("LIVE_OBSTACLE_GUEST_PASS "):
        raise ValueError("missing real-control pit/checkpoint proof")
    if not (directory/"postcheck-summary.txt").read_text(encoding="utf-8").startswith("LIVE_POSTCHECK_GUEST_PASS "):
        raise ValueError("missing live crate/spring proof")

    obstacle=_guest_rows(directory/"guest-obstacle.csv")
    post=load_postcheck(directory/"guest-postcheck.csv")
    finish=load_finish(directory/"guest-finish.csv",FINISH_COLUMNS)
    edges=verify_edge_responses(actions,obstacle)
    prev=verify_obstacle(obstacle)
    mid=verify_postcheck(post,obstacle)
    end=verify_finish(finish,post,obstacle)
    verify_live(directory/"guest-live-control.csv",
                directory/"guest-runtime.csv",
                directory/"guest-motion.csv")
    validate_timeline(read_csv(directory/"guest-motion.csv",MOTION_COLUMNS),
                      load_live(directory/"guest-live-control.csv"))
    edge_events=edges["events"]
    if (edge_events["pit"]["nro_stage_tick"]!=prev["pit_jump_tick"] or
        edge_events["roof"]["nro_stage_tick"]!=prev["roof_jump_tick"] or
        not 0<=mid["shot_tick"]-edge_events["crate"]["nro_stage_tick"]<=6):
        raise ValueError("NRO first-button frames contradict actual world events")
    if (mid["checkpoint_tick"]!=prev["checkpoint_tick"] or
        mid["spring_tick"]!=end["spring_tick"] or
        end["heart_tick"]>=end["ledge_tick"] or
        end["best_clear_frames"]<=end["ledge_tick"]):
        raise ValueError("gameplay milestones disagree within the same emulator run")
    return {
        "run_id":run_id,"nro_sha256":digest,
        "input_delay_ms":latency["input_delay_ms"],
        "poll_interval_ms":latency["poll_interval_ms"],
        "edge_elapsed_ms":latency["edge_elapsed_ms"],
        "guest_frame_windows":{name:event["guest_frame_window"]
                                for name,event in edge_events.items()},
        "pit_jump_tick":prev["pit_jump_tick"],
        "pit_cross_tick":prev["pit_cross_tick"],
        "roof_jump_tick":prev["roof_jump_tick"],
        "wall_cross_tick":prev["wall_cross_tick"],
        "checkpoint_tick":prev["checkpoint_tick"],
        "shot_tick":mid["shot_tick"],
        "crate_broken_tick":mid["crate_broken_tick"],
        "spring_tick":mid["spring_tick"],
        "heart_tick":end["heart_tick"],
        "ledge_tick":end["ledge_tick"],
        "best_clear_frames":end["best_clear_frames"],
        "heart_and_ledge_samples":end["guest_samples"],
        "checkpoint_x_px":prev["checkpoint_x_px"],
        "spring_x_px":mid["spring_x_px"],
        "ledge_x_px":end["ledge_x_px"],
        "source":"live NeXium controller; independent CI guest NRO"
    }

def compare(runs):
    ids=[r.get("run_id") for r in runs]
    if len(runs)!=3 or sorted(ids)!=list(EXPECTED_IDS) or len(set(ids))!=3:
        raise ValueError("three DISTINCT CI matrix runs are mandatory")
    for run in runs:
        expected_delay,expected_poll=PROFILES[run["run_id"]]
        if (run.get("input_delay_ms")!=expected_delay or
            run.get("poll_interval_ms")!=expected_poll):
            raise ValueError("replay timing profiles missing or interchanged")
    digests={r.get("nro_sha256") for r in runs}
    if len(digests)!=1 or not re.fullmatch(r"[0-9a-f]{64}",next(iter(digests)) or ""):
        raise ValueError("NRO digest differs across independent runs")
    for run in runs:
        ticks=[run[k] for k in EVENT_KEYS]
        if not all(isinstance(v,int) and not isinstance(v,bool) for v in ticks):
            raise ValueError("milestone not a numeric simulation tick")
        if not (0<run["pit_jump_tick"]<run["pit_cross_tick"]<
                run["roof_jump_tick"]<run["wall_cross_tick"]<
                run["checkpoint_tick"]<run["shot_tick"]<
                run["crate_broken_tick"]<run["spring_tick"]<
                run["heart_tick"]<run["ledge_tick"]<
                run["best_clear_frames"]<=1400):
            raise ValueError("impossible live controller milestone chronology")
        if not 600<=run["best_clear_frames"]<=1100:
            raise ValueError("finish tick outside plausible CITY TEST run")
        if not 628<=run["checkpoint_x_px"]<=660 or not 1120<=run["spring_x_px"]<=1180 or not 1164<=run["ledge_x_px"]<=1204:
            raise ValueError("physical checkpoint/spring/ledge position differs")
        windows=run.get("guest_frame_windows")
        if (not isinstance(windows,dict) or set(windows)!={"pit","roof","crate"} or
            any(type(v) is not int or not 1<=v<=24 for v in windows.values())):
            raise ValueError("missing NRO-observed button recognition frame windows")
        if run.get("source")!="live NeXium controller; independent CI guest NRO":
            raise ValueError("non-live or unverified guest source")
    spreads={}
    for key in EVENT_KEYS:
        values=[r[key] for r in runs]
        spread=max(values)-min(values)
        if spread>LIMITS[key]:
            raise ValueError(f"unstable {key}: spread={spread} frames, max={LIMITS[key]}")
        spreads[key]=spread
    return {
        "repetitions":3,
        "status":"PASS",
        "guest_input_frame_window_spreads":{
             name:max(r["guest_frame_windows"][name] for r in runs)-
                  min(r["guest_frame_windows"][name] for r in runs)
             for name in ("pit","roof","crate")},
        "profiles_ms":{str(r["run_id"]):{"input_delay":r["input_delay_ms"],
                     "poll_interval":r["poll_interval_ms"]}
                       for r in runs},
        "source":"three independent NeXium CI jobs with live controller inputs",
        "nro_sha256":next(iter(digests)),
        "max_completion_spread_frames":LIMITS["best_clear_frames"],
        "finish_ticks":[r["best_clear_frames"] for r in sorted(runs,key=lambda r:r["run_id"])],
        "milestone_spreads_frames":spreads,
        "runs":sorted(runs,key=lambda r:r["run_id"])
    }

def main(args):
    if len(args)!=3:
        raise SystemExit("usage: verify_nexium_repeats.py <artifact-root> <summary.txt>")
    destination=pathlib.Path(args[2])
    try:
        root=pathlib.Path(args[1])
        expected={f"nexium-emulation-evidence-run-{i}" for i in EXPECTED_IDS}
        actual={p.name for p in root.iterdir() if p.is_dir()}
        if actual!=expected:
            raise ValueError(f"expected three separate artifact folders; found {sorted(actual)}")
        report=compare([collect(root,i) for i in EXPECTED_IDS])
    except (OSError,ValueError,TypeError,KeyError) as exc:
        destination.write_text("FAIL: independent NeXium replay: "+str(exc)+"\n",encoding="utf-8")
        raise SystemExit("FAIL: independent NeXium replay: "+str(exc))
    output="NEXIUM_THREE_LIVE_REPLAYS_PASS "+json.dumps(report,sort_keys=True)+"\n"
    destination.write_text(output,encoding="utf-8")
    print(output,end="")

if __name__=="__main__":
    main(sys.argv)

