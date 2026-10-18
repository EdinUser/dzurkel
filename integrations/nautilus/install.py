#!/usr/bin/env python3
"""Opt-in, user-local Nautilus script installation; does not change shortcuts."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import tempfile

MARKER = '# Dzurkel-managed Nautilus preview script\n'


def install(executable, data_home, state_home, remove=False):
    target = data_home / "nautilus/scripts/Preview with Dzurkel"
    if target.is_symlink():
        raise ValueError(f"Refusing to replace a symbolic link: {target}")
    if target.exists():
        if not target.is_file() or MARKER not in target.read_text():
            raise ValueError(f"An unrelated script already exists: {target}")
        backup_dir = state_home / "dzurkel/nautilus-backups"
        backup_dir.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        shutil.copy2(target, backup_dir / f"preview-{stamp}.py")
    if remove:
        target.unlink(missing_ok=True)
        return target
    executable = executable.absolute()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise ValueError(f"Dzurkel executable is missing or not executable: {executable}")
    template = Path(__file__).with_name("preview.py.in").read_text()
    script = template.replace('@DZURKEL_EXECUTABLE@', json.dumps(str(executable)))
    script = script.replace('\n', '\n' + MARKER, 1)
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", dir=target.parent, delete=False) as output:
        temporary = Path(output.name)
        try:
            output.write(script)
            output.flush()
            temporary.chmod(0o755)
            os.replace(temporary, target)
        finally:
            temporary.unlink(missing_ok=True)
    return target


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=Path.home() / ".local/bin/dzurkel")
    parser.add_argument("--remove", action="store_true")
    args = parser.parse_args()
    try:
        target = install(args.executable,
                         Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")),
                         Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")),
                         args.remove)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Dzurkel: {error}\n")
    print(f"{'Removed' if args.remove else 'Installed'}: {target}")
