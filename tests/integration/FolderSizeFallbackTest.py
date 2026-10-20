#!/usr/bin/env python3
"""A missing or broken desktop calculator must leave folder previews usable."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

probe, adapter = map(Path, sys.argv[1:])
needed = subprocess.check_output(["readelf", "-d", str(probe)], text=True)
assert "libKF6" not in needed and "libgio-2.0" not in needed
content = adapter.read_bytes()
dependency = b"libgio-2.0.so.0" if "gio" in adapter.name else b"libKF6KIOCore.so.6"
iid = b"org.dzurkel.FolderSizeBackend/2"
assert dependency in content and iid in content
with tempfile.TemporaryDirectory(prefix="dzurkel-folder-runtime-") as temporary:
    root = Path(temporary)
    binary = root / probe.name
    shutil.copy2(probe, binary)
    plugins = root / "dzurkel-plugins"
    plugins.mkdir()
    destination = plugins / adapter.name
    for name, data in {
        "missing": None,
        "corrupt": b"not an ELF module",
        "missing-library": content.replace(dependency, b"x" * len(dependency)),
        "incompatible-interface": content.replace(iid, b"org.dzurkel.FolderSizeBackend/1"),
    }.items():
        if data is not None:
            destination.write_bytes(data)
        result = subprocess.run([str(binary), "missingDesktopCalculatorKeepsFolderUsable"],
            env={**os.environ, "QT_QPA_PLATFORM": "offscreen"}, text=True, capture_output=True, timeout=10)
        assert result.returncode == 0 and "SKIP" not in result.stdout, (name, result.stdout, result.stderr)
        print(f"{name}: folder metadata remains usable, contents size unavailable")
