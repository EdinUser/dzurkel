#!/usr/bin/env python3
"""Qt generic-plugin failures must leave an ordinary Qt application usable."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

probe, adapter = map(Path, sys.argv[1:])
needed = subprocess.check_output(["readelf", "-d", str(probe)], text=True)
assert "libKF6" not in needed, "Smoke host must not depend on KDE libraries"
content = adapter.read_bytes()
dependency = b"libKF6KIOFileWidgets.so.6"
iid = b"QGenericPluginFactoryInterface"
assert dependency in content and iid in content
with tempfile.TemporaryDirectory(prefix="dzurkel-chooser-runtime-") as temporary:
    root = Path(temporary)
    (root / "generic").mkdir()
    destination = root / "generic" / adapter.name
    cases = {
        "missing": None,
        "corrupt": b"not an ELF module",
        "missing-kio": content.replace(dependency, b"libNA6KIOFileWidgets.so.6"),
        "incompatible-interface": content.replace(iid, b"QGenericPluginFactoryInterfac0"),
        "available": content,
    }
    for name, data in cases.items():
        if data is not None:
            destination.write_bytes(data)
        result = subprocess.run([str(probe)], env={**os.environ,
            "QT_QPA_PLATFORM": "offscreen", "QT_PLUGIN_PATH": str(root),
            "QT_QPA_GENERIC_PLUGINS": "dzurkelchooser"}, text=True, capture_output=True, timeout=5)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        print(f"{name}: Qt startup and text input preserved")
