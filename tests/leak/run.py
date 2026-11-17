#!/usr/bin/env python3
"""Build the production leak handler against stubs and execute isolated scenarios.

--source accepts the preserved baseline without modifying it. In baseline mode,
known contract failures must actually fail; other cases must pass.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys


def cases():
    result = []
    # All stable combinations and transitions; 1->2 and 2->1 are simultaneous
    # threshold handoffs, including both processing orders.
    for initial in range(4):
        for target in range(4):
            bug = ("coalescing" if (initial, target) in {(0, 3), (3, 0)} else
                   "false-clear" if (initial, target) in {(1, 2), (2, 1), (3, 1), (3, 2)} else False)
            result.append((["transition", initial, target], bug))
    for wet in (1, 2):
        result.append((["overlap", wet], "false-clear"))
        result.append((["latched-other", wet], "false-clear"))
        for pulse in (1, 15, 30):
            result.append((["partial-other", wet, pulse], "false-clear"))
            result.append((["wet-bounce", wet, pulse], False))
        result.extend((([name, wet], False) for name in
                       ("offline-wet", "offline-clear", "lost-delivery")))
        for wet_event in (0, 1):
            for online in (0, 1):
                result.append((["threshold-gate", wet, wet_event, online], False))
    for action4 in (0, 1, 2, 255):
        for action5 in (0, 1, 2, 255):
            result.append((["commands", action4, action5], False))
            result.append((["partial-dry", action4, action5], False))
    result.append((["unenrolled"], False))
    return result


def main():
    folder = Path(__file__).resolve().parent
    project = folder.parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"),
                        help="host compiler and optional arguments, split with shlex; no shell")
    parser.add_argument("--cflags", default="-std=c99 -Wall -Wextra -Werror -O2",
                        help="host compiler flags, split with shlex")
    parser.add_argument("--source", type=Path, default=project / "src/app_leak.c")
    parser.add_argument("--build-dir", type=Path, default=project / "build/or-fix/test-binaries")
    parser.add_argument("--expect-baseline-bugs", action="store_true")
    parser.add_argument("--normal-only", action="store_true")
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    if not compiler:
        parser.error("empty --cc")
    source = args.source.resolve(strict=True)
    build_dir = args.build_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)
    print(f"Source: {source}")
    print(f"SHA-256: {hashlib.sha256(source.read_bytes()).hexdigest()}")
    print(subprocess.check_output([*compiler, "--version"], text=True,
                                  stderr=subprocess.STDOUT).splitlines()[0])
    failed = expected_failures = passed = 0
    failure_kinds = {}
    configurations = ((0, 0),) if args.normal_only else ((0, 0), (1, 1), (1, 0), (0, 1))
    for uart, debug in configurations:
        production = uart == 0 and debug == 0
        executable = build_dir / (f"leak-tests-uart{uart}-debug{debug}" + (".exe" if os.name == "nt" else ""))
        command = [*compiler, *shlex.split(args.cflags),
                   f"-DUART_PRINTF_MODE={uart}", f"-DDEBUG_WATERLEAK={debug}",
                   "-I", str(folder / "stubs"), str(source), str(folder / "harness.c"),
                   "-o", str(executable)]
        print("Compile:", " ".join(command), flush=True)
        subprocess.run(command, check=True)
        for arguments, baseline_bug in cases():
            # Production configuration covers the complete matrix. Debug mode
            # specifically preserves the historical dry-command guards/traces.
            if not production and arguments[0] not in {"commands", "partial-dry", "threshold-gate"}:
                continue
            run = subprocess.run([str(executable), *map(str, arguments)],
                                 text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            label = " ".join(map(str, arguments)) + f" uart={uart} debug={debug}"
            should_fail = args.expect_baseline_bugs and baseline_bug
            first_error = run.stderr.splitlines()[0] if run.stderr else ""
            expected_reason = (
                any(message in first_error for message in
                    ("incorrect aggregate IAS attribute", "incorrect intermediate outgoing IAS alarm"))
                if baseline_bug == "false-clear" else
                "simultaneous boundaries must produce one aggregate IAS notification" in first_error
                if baseline_bug == "coalescing" else False)
            if should_fail and run.returncode == 1 and expected_reason:
                expected_failures += 1
                failure_kinds[baseline_bug] = failure_kinds.get(baseline_bug, 0) + 1
                print("EXPECTED_BASELINE_FAILURE", baseline_bug, label)
                print(run.stderr.rstrip())
            elif run.returncode == 0 and not should_fail:
                passed += 1
                print("PASS", label)
            else:
                failed += 1
                print("UNEXPECTED_RESULT", label, "exit=", run.returncode)
                print(run.stdout.rstrip())
                print(run.stderr.rstrip())
    print(f"RESULT passed={passed} expected_baseline_failures={expected_failures} unexpected={failed}")
    if failure_kinds:
        print("EXPECTED_FAILURE_CATEGORIES", failure_kinds)
    return int(failed != 0)


if __name__ == "__main__":
    sys.exit(main())
