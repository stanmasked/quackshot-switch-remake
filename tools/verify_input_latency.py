#!/usr/bin/env python3
"""Stage 95: prove virtual-controller edge delay profiles REALLY ran.

Host-side sleeps are monotonic measured windows before virtual xdotool
key-down calls. Guest-authored records separately establish world effects.
These timings are NOT measurements of NeXium's internal frame rate.
"""
import re

PROFILES={1:(0,85),2:(25,90),3:(45,95),4:(25,90),5:(45,95)}
EVENTS=("pit","roof","crate")
TIMING=re.compile(r"^INPUT_TIMING_PROFILE profile=(\d+) edge_delay_ms=(\d+) poll_ms=(\d+)$")
EDGE=re.compile(r"^INPUT_EDGE_DELAY event=(pit|roof|crate) requested_ms=(\d+) measured_ms=(\d+)$")


def verify_latency_profile(metadata,actions,run_id):
    if run_id not in PROFILES:
        raise ValueError("invalid independent runner identity")
    expected_delay,expected_poll=PROFILES[run_id]
    if metadata.get("ci_repeat_id")!=str(run_id):
        raise ValueError("runner metadata identity differs")
    if metadata.get("ci_input_edge_delay_ms")!=str(expected_delay) or \
       metadata.get("ci_poll_ms")!=str(expected_poll):
        raise ValueError("runner latency profile mismatch")
    lines=actions.splitlines()
    configs=[TIMING.fullmatch(line) for line in lines]
    configs=[m for m in configs if m]
    if len(configs)!=1 or tuple(map(int,configs[0].groups()))!=\
           (run_id,expected_delay,expected_poll):
        raise ValueError("missing/misrepresented host delay profile marker")
    # A wall-clock step can produce a negative measured delay even though
    # the virtual button and NRO completed. Never relax elapsed bounds:
    # require the CI host to declare monotonic clock instrumentation.
    if lines.count("INPUT_TIMER_SOURCE=perl_clock_monotonic")!=1:
        raise ValueError("missing or duplicate monotonic host clock marker")
    edge={}
    for line in lines:
        m=EDGE.fullmatch(line)
        if not m:
            continue
        event,delay,elapsed=m.groups()
        if event in edge:
            raise ValueError("duplicate simulated controller button edge")
        if int(delay)!=expected_delay:
            raise ValueError("inconsistent requested virtual-button delay")
        value=int(elapsed)
        if expected_delay==0:
            if value!=0:
                raise ValueError("baseline route claims injected delay")
        elif value<expected_delay or value>expected_delay+750:
            raise ValueError("requested controller latency did not actually elapse")
        edge[event]=value
    if set(edge)!=set(EVENTS):
        raise ValueError("missing measured B/Y input-delay evidence")
    # Strongly bind each injected edge to the existing guest-specific
    # host-observed event; actual world effects are checked by Stage 91-93.
    for event,proof in (("pit","PIT_LAUNCH_GUEST"),
                        ("roof","ROOF_LAUNCH_GUEST"),
                        ("crate","CRATE_FIRE_GUEST")):
        if len([l for l in lines if l.startswith(proof+" ")])!=1:
            raise ValueError("latency marker not linked to one controller action")
        first=next(i for i,l in enumerate(lines) if l.startswith(proof+" "))
        second=next(i for i,l in enumerate(lines)
                    if l.startswith("INPUT_EDGE_DELAY event="+event+" "))
        if second!=first+1:
            raise ValueError("input-delay marker does not precede its virtual keydown")
    return {"input_delay_ms":expected_delay,
            "poll_interval_ms":expected_poll,
            "edge_elapsed_ms":edge}


if __name__=="__main__":
    raise SystemExit("Use tools/verify_nexium_repeats.py to cross-check the guest world route")
