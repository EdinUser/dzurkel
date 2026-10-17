#!/usr/bin/env python3
"""Opt-in real SFTP check: loopback OpenSSH + private KIO-FUSE/session bus.

Run manually with --build-dir. Requires Linux bubblewrap, OpenSSH and KIO-FUSE.
The mount namespace hides the real home and supplies a disposable .ssh directory;
the HOME environment variable, personal SSH files and system services are untouched.
Not part of default CTest because these runtime tools/devices are optional.
"""
import argparse
import json
import os
from pathlib import Path
import pwd
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from urllib.parse import quote


class TestUnavailable(Exception):
    pass


def fixture_pdf(path):
    objects = [b"<< /Type /Catalog /Pages 2 0 R >>",
               b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
               b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R >>",
               b"<< /Length 0 >>\nstream\n\nendstream"]
    data = bytearray(b"%PDF-1.4\n")
    offsets = [0]
    for number, obj in enumerate(objects, 1):
        offsets.append(len(data))
        data.extend(f"{number} 0 obj\n".encode() + obj + b"\nendobj\n")
    xref = len(data)
    data.extend(f"xref\n0 {len(offsets)}\n0000000000 65535 f \n".encode())
    for offset in offsets[1:]:
        data.extend(f"{offset:010d} 00000 n \n".encode())
    data.extend(f"trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode())
    path.write_bytes(data)


def stop(process):
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def inside(root, build):
    os.environ.update({"XDG_CONFIG_HOME": str(root / "config"),
                       "XDG_CACHE_HOME": str(root / "cache"),
                       "XDG_DATA_HOME": str(root / "data"),
                       "XDG_RUNTIME_DIR": str(root / "runtime"),
                       "QT_QPA_PLATFORM": "offscreen", "TMPDIR": str(root / "tmp")})
    for directory in ["config/dzurkel", "cache", "data", "runtime", "tmp", "fuse"]:
        (root / directory).mkdir(parents=True, exist_ok=True, mode=0o700)
    configuration = root / "config/dzurkel/dzurkel.ini"
    binary = build / "dzurkel"
    port = int((root / "port").read_text())
    user = pwd.getpwuid(os.getuid()).pw_name
    service = "io.github.EdinUser.Dzurkel.Previewer"
    object_path = "/io/github/EdinUser/Dzurkel/Previewer"
    interface = "io.github.EdinUser.Dzurkel.Previewer1"

    def bus_call(destination, path, method, *arguments):
        return subprocess.check_output(["gdbus", "call", "--session", "--dest", destination,
            "--object-path", path, "--method", method, *arguments], text=True,
            stderr=subprocess.DEVNULL, timeout=3).strip()

    def wait_for(check, timeout=35):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                if check():
                    return
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                pass
            time.sleep(0.05)
        raise AssertionError("Timed out waiting for the isolated test service")

    def prop(name):
        return bus_call(service, object_path, "org.freedesktop.DBus.Properties.Get", interface, name)

    def preview(url, expected):
        started = time.monotonic()
        subprocess.run([str(binary), "--show", url], check=True, timeout=7,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        wait_for(lambda: "false" in prop("Loading"))
        actual = prop("PreviewKind")
        assert f"'{expected}'" in actual, f"Expected {expected}, got {actual}"
        return round((time.monotonic() - started) * 1000, 1)

    sshd = fuse = primary = None
    try:
        with (root / "sshd.log").open("w") as log:
            sshd = subprocess.Popen([shutil.which("sshd"), "-D", "-e", "-f", str(root / "sshd_config")],
                                    stdout=log, stderr=log)
        wait_for(lambda: sshd.poll() is not None or socket_ready(port), timeout=5)
        assert sshd.poll() is None, "Temporary sshd failed: " + (root / "sshd.log").read_text()[-2500:]
        with (root / "fuse.log").open("w") as log:
            fuse = subprocess.Popen(["/usr/libexec/kio-fuse", "-f", str(root / "fuse")], stdout=log, stderr=log)
        wait_for(lambda: fuse.poll() is not None or "true" in bus_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                 "org.freedesktop.DBus.NameHasOwner", "org.kde.KIOFuse"), timeout=5)
        if fuse.poll() is not None:
            diagnostics = (root / "fuse.log").read_text()[-2500:]
            if "mount failed: Operation not permitted" in diagnostics:
                raise TestUnavailable("This environment does not permit a private FUSE mount; real SFTP was not tested.")
            raise AssertionError("Private KIO-FUSE failed: " + diagnostics)
        fixtures = [("fixture.bmp", "image"), ("fixture.pdf", "pdf"), ("пример #%.txt", "text")]
        remote = lambda name: f"sftp://{user}@127.0.0.1:{port}" + quote(str(root / "fixtures" / name))
        # Default denial must happen before accessing the protocol backend.
        denied = subprocess.run([str(binary), "--show", remote(fixtures[0][0])], capture_output=True, text=True, timeout=5)
        assert denied.returncode == 1 and "experimental and disabled" in denied.stderr
        configuration.write_text("[resources]\nexperimentalSftp=true\n")
        with (root / "app.log").open("w") as log:
            primary = subprocess.Popen([str(binary), str(root / "fixtures" / fixtures[0][0])], stdout=log, stderr=log)
        wait_for(lambda: "false" in prop("Loading"), timeout=10)
        results = []
        for name, kind in fixtures:
            local_ms = preview(str(root / "fixtures" / name), kind)
            remote_ms = preview(remote(name), kind)
            assert "sftp://" in prop("FileUrl")
            results.append({"kind": kind, "local_ready_ms": local_ms, "sftp_ready_ms": remote_ms})
        # Policy updates apply to the running service too, not only new clients.
        configuration.write_text("[resources]\nexperimentalSftp=false\n")
        previous = prop("FileUrl")
        denied = subprocess.run(
            ["gdbus", "call", "--session", "--dest", service, "--object-path", object_path,
             "--method", interface + ".Show", remote(fixtures[0][0])], capture_output=True, text=True, timeout=5)
        assert denied.returncode != 0 and "experimental and disabled" in denied.stderr
        assert prop("FileUrl") == previous
        configuration.write_text("[resources]\nexperimentalSftp=true\n")
        stop(sshd)
        # Use a fresh endpoint so KIO cannot satisfy this request from a live
        # cached connection or file. A released listener supplies a closed port.
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            unavailable_port = probe.getsockname()[1]
        unavailable = f"sftp://{user}@127.0.0.1:{unavailable_port}/unavailable.txt"
        preview(unavailable, "information")
        subprocess.run([str(binary), "--quit"], check=True, timeout=5)
        primary.wait(timeout=10)
        wait_for(lambda: not list((root / "tmp").glob("dzurkel-resource-*")), timeout=5)
        print(json.dumps({"result": "passed", "server": "temporary loopback OpenSSH",
            "checks": ["default denial", "BMP/PDF/Unicode text", "live opt-out", "unavailable endpoint", "cleanup"],
            "ready_timings": results, "field_confirmed": False}), flush=True)
    finally:
        stop(primary)
        stop(fuse)
        stop(sshd)


def socket_ready(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
            return True
    except OSError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--inside", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if args.inside:
        inside(args.inside, build)
        return
    for tool in ["bwrap", "sshd", "ssh-keygen", "dbus-run-session", "gdbus"]:
        if not shutil.which(tool):
            raise SystemExit(f"Optional test prerequisite missing: {tool}")
    if not Path("/usr/libexec/kio-fuse").exists() or not Path("/dev/fuse").exists():
        raise SystemExit("Optional test requires KIO-FUSE and /dev/fuse")
    assert (build / "dzurkel").is_file(), "Build Dzurkel first"
    repository = Path(__file__).resolve().parents[2]
    user_home = Path(pwd.getpwuid(os.getuid()).pw_dir)
    user = pwd.getpwuid(os.getuid()).pw_name
    with tempfile.TemporaryDirectory(prefix="dzurkel-sftp-check-") as temporary:
        root = Path(temporary)
        (root / "ssh").mkdir(mode=0o700)
        (root / "fixtures").mkdir()
        for name in ["host", "client"]:
            subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(root / name)], check=True)
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        (root / "port").write_text(str(port))
        (root / "known_hosts").write_text(f"[127.0.0.1]:{port} " + (root / "host.pub").read_text())
        (root / "ssh/config").write_text(f"Host 127.0.0.1\n  IdentityFile {root}/client\n  UserKnownHostsFile {root}/known_hosts\n  StrictHostKeyChecking yes\n")
        (root / "sshd_config").write_text(f"""Port {port}
ListenAddress 127.0.0.1
HostKey {root}/host
PidFile {root}/sshd.pid
AuthorizedKeysFile {root}/client.pub
StrictModes no
UsePAM no
PasswordAuthentication no
KbdInteractiveAuthentication no
PubkeyAuthentication yes
AuthenticationMethods publickey
PermitRootLogin no
AllowUsers {user}
DisableForwarding yes
PermitTTY no
Subsystem sftp internal-sftp -R
ForceCommand internal-sftp -R
""")
        # 1x1 uncompressed 24-bit BMP; all fixtures are synthetic and disposable.
        bmp = struct.pack("<2sIHHI", b"BM", 58, 0, 0, 54) + struct.pack("<IiiHHIIiiII", 40, 1, 1, 1, 24, 0, 4, 0, 0, 0, 0) + b"\0\0\xff\0"
        (root / "fixtures/fixture.bmp").write_bytes(bmp)
        fixture_pdf(root / "fixtures/fixture.pdf")
        (root / "fixtures/пример #%.txt").write_text("Synthetic SFTP preview fixture\n")
        for directory in ["config", "cache", "data", "runtime", "tmp"]:
            (root / directory).mkdir(mode=0o700)
        environment = {**os.environ, "XDG_CONFIG_HOME": str(root / "config"),
                       "XDG_CACHE_HOME": str(root / "cache"), "XDG_DATA_HOME": str(root / "data"),
                       "XDG_RUNTIME_DIR": str(root / "runtime"), "TMPDIR": str(root / "tmp"),
                       "QT_QPA_PLATFORM": "offscreen"}
        command = ["bwrap", "--die-with-parent", "--unshare-pid", "--ro-bind", "/", "/", "--dev-bind", "/dev", "/dev",
                   "--proc", "/proc", "--tmpfs", "/tmp", "--bind", str(root), str(root), "--tmpfs", str(user_home),
                   "--dir", str(user_home / ".ssh"), "--ro-bind", str(root / "ssh"), str(user_home / ".ssh"),
                   "--ro-bind", str(repository), str(repository), "--chdir", str(repository),
                   "--", "dbus-run-session", "--", sys.executable, str(Path(__file__).resolve()),
                   "--inside", str(root), "--build-dir", str(build)]
        result = subprocess.run(command, env=environment, timeout=120)
        if result.returncode:
            raise SystemExit(result.returncode)


if __name__ == "__main__":
    try:
        main()
    except TestUnavailable as error:
        print(json.dumps({"result": "unavailable", "reason": str(error), "field_confirmed": False}), flush=True)
        raise SystemExit(77)
