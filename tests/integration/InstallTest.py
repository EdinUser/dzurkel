#!/usr/bin/env python3
"""Verify the installed artifact without touching the user's live installation."""
import argparse
import os
from pathlib import Path
import subprocess
import selectors
import sys
import tempfile
import time


def smoke(binary):
    """Run only inside the fresh bus created by the parent install check."""
    with tempfile.TemporaryDirectory(prefix="dzurkel smoke ") as temporary:
        sample = Path(temporary) / "пример space.txt"
        sample.write_text("Installed Dzurkel smoke test\n")
        environment = {**os.environ, "QT_QPA_PLATFORM": "offscreen"}
        environment.pop("XDG_ACTIVATION_TOKEN", None)
        primary = subprocess.Popen([binary, "--diagnostics", str(sample)], env=environment,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(primary.stderr, selectors.EVENT_READ)
                deadline = time.monotonic() + 15
                log = b""
                while b"Dzurkel primary PID:" not in log:
                    assert primary.poll() is None, f"Installed primary exited: {log.decode(errors='replace')}"
                    assert time.monotonic() < deadline, f"Installed primary failed to become ready: {log!r}"
                    for key, _ in selector.select(timeout=0.1):
                        log += os.read(key.fileobj.fileno(), 4096)
            client_env = {**environment, "QT_QPA_PLATFORM": "dzurkel_invalid_backend"}
            for command in (["--hide"], ["--show", str(sample)], ["--quit"]):
                subprocess.run([binary, *command], env=client_env, check=True, timeout=7)
            assert primary.wait(timeout=10) == 0, primary.stderr.read().decode(errors="replace")
        finally:
            if primary.poll() is None:
                primary.kill()
                primary.wait()


if len(sys.argv) == 3 and sys.argv[1] == "--smoke":
    smoke(sys.argv[2])
    sys.exit(0)

parser = argparse.ArgumentParser()
parser.add_argument("--build-dir", required=True)
parser.add_argument("--cmake", required=True)
parser.add_argument("--bindir", required=True)
parser.add_argument("--datadir", required=True)
parser.add_argument("--version", required=True)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix="dzurkel install ") as temporary:
    root = Path(temporary)
    environment = {**os.environ, "DESTDIR": str(root)}
    subprocess.run([args.cmake, "--install", args.build_dir, "--strip"], env=environment, check=True, timeout=20)
    binary = root / args.bindir.lstrip("/") / "dzurkel"
    menu = root / args.datadir.lstrip("/") / "kio/servicemenus/dzurkel.desktop"
    application = root / args.datadir.lstrip("/") / "applications/dzurkel.desktop"
    actual = {item for item in root.rglob("*") if item.is_file()}
    assert actual == {binary, menu, application}, f"Unexpected installation contents: {actual}"
    assert os.access(binary, os.X_OK)
    assert os.access(menu, os.X_OK), "KDE user-local service menus must be executable"
    assert not os.access(application, os.X_OK)
    for entry in (menu, application):
        text = entry.read_text()
        assert "%u" in text and " --show -- %u" in text
        assert "@DZURKEL_EXEC@" not in text
        assert "StartupNotify=true" in text
        assert "helpers/" not in text
    assert "Actions=dzurkel;" in menu.read_text()
    assert "[Desktop Action dzurkel]" in menu.read_text()
    assert "NoDisplay=true" in application.read_text()
    env = {**os.environ, "QT_QPA_PLATFORM": "dzurkel_invalid_backend"}
    result = subprocess.run([str(binary), "--version"], env=env, text=True,
                            capture_output=True, check=True, timeout=5)
    assert result.stdout.strip() == f"dzurkel {args.version}", result.stdout
    subprocess.run(["dbus-run-session", "--", sys.executable, str(Path(__file__).resolve()),
                    "--smoke", str(binary)], check=True, timeout=25)
    print("Staged executable, desktop identity and service menu passed.")
