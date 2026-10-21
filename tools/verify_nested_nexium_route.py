#!/usr/bin/env python3
"""Validate the *inner* bash -c body of the real NeXium guest runner.

Plain 'bash -n outer.sh' doesn't parse a single-quoted inner program.
A prior red guest failure came from malformed quotes inside that body.
This check is pure host-side syntax validation; it never drives gameplay.
"""
import pathlib
import subprocess
import sys


class RouteSyntaxError(ValueError):
    pass


def embedded_program(source):
    start_marker = 'xvfb-run -a -s "-screen 0 1280x720x24" bash -c ' + "'\n"
    end_marker = '\n' + "' bash " + '"$emu" "$nro"'
    if source.count(start_marker) != 1 or source.count(end_marker) != 1:
        raise RouteSyntaxError("inner NeXium route boundaries missing/duplicated")
    start = source.index(start_marker) + len(start_marker)
    end = source.index(end_marker, start)
    body = source[start:end]
    if not body.strip() or "\x00" in body:
        raise RouteSyntaxError("inner program empty or corrupt")
    # A literal single quote would end the shell's outer quoting. A
    # standalone 'bash -n' on the inner text would not detect that.
    if "'" in body:
        raise RouteSyntaxError("single quote inside outer single-quoted route")
    return body


def check_route(path):
    p = pathlib.Path(path)
    text = p.read_text(encoding="utf-8")
    program = embedded_program(text)
    result = subprocess.run(
        ["bash", "-n"], input=program, text=True, capture_output=True,
        timeout=6, check=False)
    if result.returncode:
        raise RouteSyntaxError("inner bash -n failed: " + result.stderr.strip())
    return {"status": "PASS", "inner_script_lines":len(program.splitlines())}


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: verify_nested_neixum_route.py SCRIPT.sh")
    try:
        result = check_route(argv[1])
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        raise SystemExit("FAIL: nested NeXium runner: " + str(exc)) from exc
    print("NEXIUM_NESTED_BASH_ROUTE_PASS", result)


if __name__ == "__main__":
    main(sys.argv)
