#!/usr/bin/env python3
"""Exercise optional loading in fresh processes, without changing system libraries."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1])
plugin = Path(sys.argv[2])
with tempfile.TemporaryDirectory(prefix="dzurkel-highlighting-") as temporary:
    root = Path(temporary)
    probe = root / "bin" / binary.name
    probe.parent.mkdir()
    shutil.copy2(binary, probe)
    environment = {**os.environ, "QT_QPA_PLATFORM": "offscreen",
                   "DZURKEL_TEST_NO_HIGHLIGHTING": "1", "DZURKEL_TEST_ADAPTER_PRESENT": "0"}
    command = [str(probe)]
    # A valid plugin in the working directory must never be picked up.
    shutil.copy2(plugin, root / plugin.name)
    subprocess.run(command, cwd=root, env=environment, check=True, timeout=20)

    subprocess.run([str(probe), "recheckFindsNewlyInstalledAdapter"], cwd=root,
                   env={**environment, "DZURKEL_TEST_RECHECK_PLUGIN": str(plugin.resolve())},
                   check=True, timeout=20)

    destination = probe.parent / "dzurkel-plugins" / plugin.name
    destination.parent.mkdir(exist_ok=True)
    destination.write_bytes(b"not a loadable plugin")
    environment["DZURKEL_TEST_ADAPTER_PRESENT"] = "1"
    subprocess.run(command, cwd=root, env=environment, check=True, timeout=20)

    # Change only this copied adapter's NEEDED name to emulate a missing library.
    content = plugin.read_bytes()
    needed = b"libKF6SyntaxHighlighting.so.6"
    assert needed in content, "Adapter must link to the highlighting library"
    destination.write_bytes(content.replace(needed, b"libDZ6SyntaxHighlighting.so.6"))
    subprocess.run(command, cwd=root, env=environment, check=True, timeout=20)

    shutil.copy2(plugin, destination)
    environment.pop("DZURKEL_TEST_NO_HIGHLIGHTING")
    subprocess.run(command, cwd=root, env=environment, check=True, timeout=20)

    # Installed layout, including lib64 or Debian multiarch paths.
    destination.unlink()
    destination = probe.parent / sys.argv[3]
    destination.parent.mkdir(parents=True)
    shutil.copy2(plugin, destination)
    subprocess.run(command, cwd=root, env=environment, check=True, timeout=20)
