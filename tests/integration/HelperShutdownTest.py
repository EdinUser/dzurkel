#!/usr/bin/env python3
"""Run real lifecycle owners in disposable processes with external cleanup."""
import ctypes
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest


PROBE, SHIM = map(Path, sys.argv[1:3])
del sys.argv[1:3]


class HelperShutdownTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="dzurkel-helper-shutdown-")
        cls.root = Path(cls.directory.name)
        for name in ("working", "missing"):
            directory = cls.root / name
            directory.mkdir()
            shutil.copy2(PROBE, directory / "probe")
        (cls.root / "working" / "dzurkel").symlink_to("probe")
        # Adopt orphaned test helpers so cleanup also reaps them after a probe
        # exits without waiting. This setting affects only this test supervisor.
        if ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0) != 0:
            raise OSError(ctypes.get_errno(), "PR_SET_CHILD_SUBREAPER failed")

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_case(self, family, scenario, suppress=False):
        directory = self.root / ("missing" if scenario == "failed-start" else "working")
        pids = self.root / "helper-pids"
        pids.unlink(missing_ok=True)
        environment = os.environ.copy()
        environment.update(
            DZURKEL_TEST_HELPER_PIDS=str(pids),
            DZURKEL_TEST_HELPER_MODE="complete" if scenario == "complete" else "stall",
            XDG_CONFIG_HOME=str(self.root / "config"),
            QT_LOGGING_RULES="*.warning=true",
        )
        environment.pop("DZURKEL_TEST_SUPPRESS_KILL", None)
        if suppress:
            environment["DZURKEL_TEST_SUPPRESS_KILL"] = "1"
            environment["LD_PRELOAD"] = ":".join(filter(None, [environment.get("LD_PRELOAD"), str(SHIM)]))
        process = subprocess.Popen(
            [str(directory / "probe"), family, scenario], env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True,
        )
        try:
            try:
                stdout, stderr = process.communicate(timeout=8)
            except subprocess.TimeoutExpired:
                self.fail(f"{family}/{scenario}: process destruction delayed shutdown")
            output = (stdout + stderr).decode(errors="replace")
            self.assertEqual(process.returncode, 0, output)
            self.assertNotIn("Destroyed while process", output)
            if suppress:
                self.assertIn("SIMULATED_UNDELIVERED_SIGKILL", output)
        finally:
            # The supervisor is not preloaded: its real SIGKILL cleans up all
            # deliberately surviving children, including after assertion failure.
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.communicate(timeout=3)
            children = [int(value) for value in pids.read_text().split()] if pids.exists() else []
            for pid in children:
                deadline = time.monotonic() + 3
                while True:
                    try:
                        done, _ = os.waitpid(pid, os.WNOHANG)
                    except ChildProcessError:
                        break  # Already reaped by the probe.
                    if done:
                        break
                    # waitpid establishes that this is still our adopted child,
                    # rather than a PID already reaped/reused by another process.
                    os.kill(pid, signal.SIGKILL)
                    if time.monotonic() >= deadline:
                        self.fail(f"Disposable helper {pid} could not be reaped")
                    time.sleep(0.01)


def add_case(family, scenario, suppress=False):
    def test(self):
        self.run_case(family, scenario, suppress)
    name = f"test_{family}_{scenario.replace('-', '_')}_{'undelivered' if suppress else 'real'}"
    setattr(HelperShutdownTest, name, test)


for family in ("prepare", "folder", "media"):
    for scenario in ("complete", "failed-start"):
        add_case(family, scenario)
    for scenario in ("active", "cancelled", "destroyed"):
        add_case(family, scenario)
        add_case(family, scenario, suppress=True)
    add_case(family, "budget", suppress=True)
add_case("folder", "replaced", suppress=True)

if __name__ == "__main__":
    unittest.main()
