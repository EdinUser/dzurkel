#!/usr/bin/env python3
"""Verify the installed artifact without touching the user's live installation."""
import argparse
import os
from pathlib import Path
import subprocess
import selectors
import shlex
import socket
import struct
import sys
import tempfile
import time


def smoke(binary, launcher=None):
    """Run only inside the fresh bus created by the parent install check."""
    with tempfile.TemporaryDirectory(prefix="dzurkel smoke ") as temporary:
        sample = Path(temporary) / "пример space.txt"
        sample.write_text("Installed Dzurkel smoke test\n")
        # The installed binary must serve media ranges itself, without relying
        # on the uninstalled experiment helper or copying the source file.
        channel, helper_channel = socket.socketpair()
        channel.settimeout(5)
        helper = subprocess.Popen([binary, "--internal-media-source", str(sample), "0"],
                                  stdin=helper_channel, stdout=helper_channel,
                                  stderr=subprocess.PIPE)
        helper_channel.close()
        try:
            def receive(count):
                data = b""
                while len(data) < count:
                    chunk = channel.recv(count - len(data))
                    assert chunk, "Installed media helper closed its channel"
                    data += chunk
                return data

            expected = sample.read_bytes()
            assert struct.unpack(">qq", receive(16)) == (len(expected), 0)
            channel.sendall(struct.pack(">qq", 0, len(expected)))
            assert struct.unpack(">qq", receive(16)) == (len(expected), 0)
            assert receive(len(expected)) == expected
        finally:
            channel.close()
            try:
                helper.wait(timeout=5)
            except subprocess.TimeoutExpired:
                helper.kill()
                helper.wait()
        assert helper.returncode == 0, helper.stderr.read().decode(errors="replace")
        environment = {**os.environ, "QT_QPA_PLATFORM": "offscreen"}
        environment.pop("XDG_ACTIVATION_TOKEN", None)
        command = [binary, "--diagnostics", str(sample)]
        if launcher:
            entry = Path(launcher).read_text()
            executable = next(line[5:] for line in entry.splitlines() if line.startswith("Exec="))
            command = [str(sample) if arg == "%u" else arg for arg in shlex.split(executable)]
            assert command[0] == binary, command
            command.insert(1, "--diagnostics")
        primary = subprocess.Popen(command, env=environment,
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
            for command in (["--hide"], ["--settings"], ["--settings"], ["--show", str(sample)], ["--quit"]):
                subprocess.run([binary, *command], env=client_env, check=True, timeout=7)
            assert primary.wait(timeout=10) == 0, primary.stderr.read().decode(errors="replace")
        finally:
            if primary.poll() is None:
                primary.kill()
                primary.wait()


if len(sys.argv) == 3 and sys.argv[1] == "--smoke":
    smoke(sys.argv[2])
    sys.exit(0)

if len(sys.argv) == 3 and sys.argv[1] == "--smoke-launchers":
    for name in ("dzurkel.desktop", "dzurkel-settings.desktop"):
        smoke(sys.argv[2], f"/usr/share/applications/{name}")
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
    settings = root / args.datadir.lstrip("/") / "applications/dzurkel-settings.desktop"
    actual = {item for item in root.rglob("*") if item.is_file()}
    compatibility = root / args.datadir.lstrip("/") / "dzurkel/build-compatibility.json"
    import json
    report = json.loads(compatibility.read_text())
    assert report["dzurkel_version"] == args.version
    assert not any("Multimedia" in name or "KF6" in name for name in report["artifact"]["needed_sonames"])
    expected = {binary, menu, application, settings, compatibility}
    if plugin := os.environ.get("DZURKEL_TEST_HIGHLIGHTING_PLUGIN"):
        expected.add(root / plugin.lstrip("/"))
    for adapter in ("dzurkel_audio", "dzurkel_desktop"):
        if plugin := os.environ.get("DZURKEL_TEST_" + adapter):
            expected.add(root / plugin.lstrip("/"))
    assert actual == expected, f"Unexpected installation contents: {actual}"
    assert ' --settings' in settings.read_text()
    assert 'NoDisplay=true' not in settings.read_text()
    assert '@DZURKEL_EXEC@' not in settings.read_text()
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
    assert "X-KDE-Protocols=file,smb,sftp" in menu.read_text()
    assert "[Desktop Action dzurkel]" in menu.read_text()
    assert "NoDisplay=true" in application.read_text()
    env = {**os.environ, "QT_QPA_PLATFORM": "dzurkel_invalid_backend"}
    result = subprocess.run([str(binary), "--version"], env=env, text=True,
                            capture_output=True, check=True, timeout=5)
    assert result.stdout.strip() == f"dzurkel {args.version}", result.stdout
    subprocess.run(["dbus-run-session", "--", sys.executable, str(Path(__file__).resolve()),
                    "--smoke", str(binary)], check=True, timeout=25)
    print("Staged executable, desktop identity and service menu passed.")
