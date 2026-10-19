#!/usr/bin/env python3
"""Run muted, read-only video probes; keep measurements in the output directory.

Delay injection models filesystem-read/cache-miss latency, not a network bandwidth limit or
disconnect. Remote results may benefit from filesystem/server caches.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--local", type=Path, required=True)
    parser.add_argument("--remote-directory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    remote = args.remote_directory / args.local.name
    other = next((p for p in sorted(args.remote_directory.glob("*.mkv")) if p != remote), remote)
    sources = {p: (p.stat().st_size, p.stat().st_mtime_ns) for p in (args.local, remote, other)}
    cases = [
        ("local", args.local, []),
        ("remote", remote, []),
        ("local-repeat", args.local, []),
        ("remote-repeat", remote, []),
        ("remote-other", other, []),
        ("local-delay-100", args.local, ["--read-delay-ms", "100", "--duration-ms", "15000"]),
        ("remote-delay-100", remote, ["--read-delay-ms", "100", "--duration-ms", "15000"]),
        ("remote-delay-250", remote, ["--read-delay-ms", "250", "--duration-ms", "20000"]),
        ("local-cancel-loading", args.local, ["--cancel-loading", "--read-delay-ms", "5000",
                                             "--read-timeout-ms", "10000", "--duration-ms", "1500"]),
        ("remote-cancel-loading", remote, ["--cancel-loading", "--read-delay-ms", "5000",
                                          "--read-timeout-ms", "10000", "--duration-ms", "1500"]),
    ]
    reports = []
    environment = dict(os.environ, QT_MEDIA_BACKEND="ffmpeg", QT_FORCE_STDERR_LOGGING="1")
    for name, path, options in cases:
        with (args.output / f"{name}.log").open("w") as errors:
            try:
                result = subprocess.run([str(args.probe.resolve()), *options, str(path)],
                                        env=environment, stdout=subprocess.PIPE, stderr=errors,
                                        text=True, timeout=65)
                lines = result.stdout.strip().splitlines()
                report = json.loads(lines[-1]) if lines else {"error": "Probe produced no JSON; inspect its log."}
                report.update(returncode=result.returncode, passed=result.returncode == 0 and not report.get("error"))
            except (subprocess.TimeoutExpired, ValueError, IndexError) as error:
                report = {"error": str(error), "passed": False}
        report.update(name=name)
        reports.append(report)
        print(json.dumps(report), flush=True)
        (args.output / "results.json").write_text(json.dumps(reports, indent=2) + "\n")
    unchanged = all((p.stat().st_size, p.stat().st_mtime_ns) == before for p, before in sources.items())
    (args.output / "source-check.json").write_text(json.dumps({"sizes_and_mtimes_unchanged": unchanged}) + "\n")
    return 0 if unchanged and all(report["passed"] for report in reports) else 1


if __name__ == "__main__":
    raise SystemExit(main())
