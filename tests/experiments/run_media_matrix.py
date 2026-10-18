#!/usr/bin/env python3
"""Generate synthetic audio, exercise the opt-in probe, and save measurements.

Requires ffmpeg and a desktop audio session. Playback is muted. Outputs stay in
the requested directory; existing music files are never modified.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--music", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    probe = args.probe.resolve()
    formats = [
        ("mp3", "mp3", "libmp3lame"),
        ("flac", "flac", "flac"),
        ("vorbis", "ogg", "libvorbis"),
        ("vorbis-oga", "oga", "libvorbis"),
        ("opus", "opus", "libopus"),
        ("wav", "wav", "pcm_s16le"),
        ("m4a-aac", "m4a", "aac"),
        ("m4a-alac", "m4a", "alac"),
        ("aac", "aac", "aac"),
        ("mp4-audio", "mp4", "aac"),
    ]
    sources = []
    # Ordinary MP4/M4A muxing leaves the index at the end: deliberately do not
    # use +faststart. A time-varying signal also avoids unrealistic silence files.
    for name, extension, codec in formats:
        path = args.output / f"{name}.{extension}"
        command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                   "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=90",
                   "-ac", "2", "-c:a", codec, str(path)]
        subprocess.run(command, check=True, timeout=60)
        sources.append((name, path))
    if args.music:
        sources.append(("real-mp3", args.music))
    reports = []
    environment = dict(os.environ, QT_MEDIA_BACKEND="ffmpeg")
    for name, path in sources:
        log = args.output / f"{name}.log"
        with log.open("w") as errors:
            result = subprocess.run(
                [str(probe), "--duration-ms", "3000", "--seek-ms", "30000", str(path.resolve())],
                text=True, stdout=subprocess.PIPE, stderr=errors,
                timeout=45, env=environment)
        try:
            report = json.loads(result.stdout.strip().splitlines()[-1])
        except (ValueError, IndexError):
            report = {"error": "Probe did not produce a JSON report"}
        passed = (result.returncode == 0 and not report.get("error")
                  and report.get("seek_issued") and report.get("position_after_seek_ms", -1) >= 29500
                  and report.get("stop_ms", 10000) < 500
                  and report.get("owner_thread_reads", -1) == 0
                  and report.get("maximum_heartbeat_gap_ms", 10000) < 500
                  and report.get("active_helpers_after_stop", -1) == 0
                  and report.get("peak_read_bytes", 100000) <= 65536)
        report.update(name=name, passed=bool(passed), returncode=result.returncode)
        reports.append(report)
        print(json.dumps(report), flush=True)
    # Cancel while the very first decoder read is still waiting on the helper.
    with (args.output / "cancel-stalled.log").open("w") as errors:
        result = subprocess.run(
            [str(probe), "--duration-ms", "1500", "--read-delay-ms", "5000", str(sources[0][1].resolve())],
            text=True, stdout=subprocess.PIPE, stderr=errors, timeout=15, env=environment)
    report = json.loads(result.stdout.strip().splitlines()[-1])
    passed = (report.get("stop_ms", 10000) < 500 and report.get("gui_heartbeats", 0) > 50
              and report.get("maximum_heartbeat_gap_ms", 10000) < 500
              and report.get("owner_thread_reads", -1) == 0
              and report.get("active_helpers_after_stop", -1) == 0)
    report.update(name="cancel-stalled", passed=passed, returncode=result.returncode)
    reports.append(report)
    print(json.dumps(report), flush=True)
    (args.output / "results.json").write_text(json.dumps(reports, indent=2) + "\n")
    return 0 if all(report["passed"] for report in reports) else 1


if __name__ == "__main__":
    raise SystemExit(main())
