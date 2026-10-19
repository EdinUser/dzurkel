#!/usr/bin/env python3
"""Verify missing/unloadable audio libraries without changing system packages."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

probe, application, plugin = map(lambda arg: Path(arg).resolve(), sys.argv[1:4])
with tempfile.TemporaryDirectory(prefix='dzurkel-audio-fallback-') as temporary:
    root = Path(temporary)
    shutil.copy2(probe, root / probe.name)
    shutil.copy2(application, root / 'dzurkel')
    environment = {**os.environ, 'QT_QPA_PLATFORM': 'offscreen', 'DZURKEL_TEST_AUDIO_AVAILABLE': '0'}
    command = [str(root / probe.name)]
    # Working-directory plugins are never searched.
    shutil.copy2(plugin, root / plugin.name)
    subprocess.run(command, cwd=root, env=environment, check=True, timeout=30)
    subprocess.run(command + ['refreshCanLoadAnAddedAdapter'], cwd=root,
                   env={**environment, 'DZURKEL_TEST_ADD_AUDIO_ADAPTER': str(plugin)}, check=True, timeout=15)
    destination = root / 'dzurkel-plugins' / plugin.name
    destination.write_bytes(b'not a loadable adapter')
    subprocess.run(command, cwd=root, env=environment, check=True, timeout=30)
    content = plugin.read_bytes()
    for needed in (b'libQt6Multimedia.so.6', b'libQt6MultimediaWidgets.so.6'):
        assert needed in content
        destination.write_bytes(content.replace(needed, needed.replace(b'Qt6', b'NA6')))
        subprocess.run(command, cwd=root, env=environment, check=True, timeout=30)
    iid = b'org.dzurkel.MediaBackend/1'
    assert iid in content
    destination.write_bytes(content.replace(iid, b'org.dzurkel.MediaBackend/0'))
    subprocess.run(command, cwd=root,
                   env={**environment, 'DZURKEL_TEST_MEDIA_INCOMPATIBLE': '1'}, check=True, timeout=30)
    shutil.copy2(plugin, destination)
    environment['DZURKEL_TEST_AUDIO_AVAILABLE'] = '1'
    subprocess.run(command, cwd=root, env=environment, check=True, timeout=30)
