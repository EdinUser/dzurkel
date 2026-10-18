#!/usr/bin/env python3
"""Exercise Nautilus argument delivery without a desktop or real application."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2] / "integrations/nautilus/install.py"
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("nautilus_install", SOURCE)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class NautilusTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="dzurkel nautilus ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.binary = self.root / 'bin with spaces/dzurkel'
        self.binary.parent.mkdir()
        self.binary.write_text('#!/usr/bin/env python3\nimport json, sys\nprint(json.dumps(sys.argv[1:]))\n')
        self.binary.chmod(0o755)
        self.script = installer.install(self.binary, self.root / 'data', self.root / 'state')

    def run_script(self, arguments=(), uris=''):
        env = {**os.environ, "NAUTILUS_SCRIPT_SELECTED_URIS": uris}
        # Avoid desktop notifications from intentionally rejected selections.
        bin_dir = self.root / 'test-bin'
        bin_dir.mkdir(exist_ok=True)
        notify = bin_dir / 'notify-send'
        notify.write_text('#!/bin/sh\nexit 0\n')
        notify.chmod(0o755)
        env['PATH'] = f"{bin_dir}:{os.environ['PATH']}"
        return subprocess.run([str(self.script), *arguments], cwd=self.root, env=env,
                              capture_output=True, text=True, timeout=5)

    def test_local_argument_keeps_special_characters(self):
        name = '-пример space # % " $ ` newline\n.txt'
        result = self.run_script([name])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), ['--show', '--', str(self.root / name)])

    def test_encoded_uri_is_forwarded_without_decoding_or_splitting(self):
        uri = (self.root / 'пример space # % newline\n.txt').as_uri()
        result = self.run_script(['ignored local argument'], uri + '\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), ['--show', '--', uri])

    def test_invalid_selection_does_not_launch_application(self):
        for arguments, uris in (([], ''), (['one', 'two'], ''),
                                ([], 'file:///one\nfile:///two\n'),
                                ([], 'smb://server/share/file.txt\n'),
                                ([], 'file://[invalid')):
            with self.subTest(arguments=arguments, uris=uris):
                result = self.run_script(arguments, uris)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, '')

    def test_unrelated_script_is_preserved(self):
        self.script.write_text('user script\n')
        with self.assertRaises(ValueError):
            installer.install(self.binary, self.root / 'data', self.root / 'state')
        self.assertEqual(self.script.read_text(), 'user script\n')

    def test_update_and_removal_keep_a_backup(self):
        installer.install(self.binary, self.root / 'data', self.root / 'state')
        installer.install(self.binary, self.root / 'data', self.root / 'state', remove=True)
        self.assertFalse(self.script.exists())
        self.assertEqual(len(list((self.root / 'state/dzurkel/nautilus-backups').glob('*.py'))), 2)

    def test_symbolic_link_is_preserved(self):
        self.script.unlink()
        self.script.symlink_to(self.binary)
        with self.assertRaises(ValueError):
            installer.install(self.binary, self.root / 'data', self.root / 'state')
        self.assertTrue(self.script.is_symlink())


if __name__ == '__main__':
    unittest.main()
