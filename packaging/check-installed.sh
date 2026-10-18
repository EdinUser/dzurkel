#!/usr/bin/env bash
# Run inside an isolated package-test container, never on the live desktop.
set -euo pipefail
case "${1:-}" in
    installed)
        version=$(sed -n 's/^project(Dzurkel VERSION \([0-9][0-9.]*\) LANGUAGES CXX)/\1/p' CMakeLists.txt)
        test "$(/usr/bin/dzurkel --version)" = "dzurkel $version"
        test -x /usr/bin/dzurkel
        test -f /usr/share/kio/servicemenus/dzurkel.desktop
        desktop-file-validate /usr/share/applications/dzurkel{,-settings}.desktop
        grep -Fx 'Exec="/usr/bin/dzurkel" --show -- %u' /usr/share/applications/dzurkel.desktop
        grep -Fx 'Exec="/usr/bin/dzurkel" --settings' /usr/share/applications/dzurkel-settings.desktop
        grep -Fx 'Exec="/usr/bin/dzurkel" --show -- %u' /usr/share/kio/servicemenus/dzurkel.desktop
        python3 - <<'PY'
import pathlib
import shutil
import subprocess

command = ["dpkg-query", "-L", "dzurkel"] if shutil.which("dpkg-query") else ["pacman", "-Qlq", "dzurkel"]
paths = subprocess.check_output(command, text=True).splitlines()
runtime = {"/usr/bin/dzurkel", "/usr/share/applications/dzurkel.desktop",
           "/usr/share/applications/dzurkel-settings.desktop",
           "/usr/share/kio/servicemenus/dzurkel.desktop"}
files = {path for path in paths if pathlib.Path(path).is_file()}
assert runtime <= files, runtime - files
unexpected = {path for path in files - runtime
              if not path.startswith(("/usr/share/doc/dzurkel/", "/usr/share/licenses/dzurkel/"))}
assert not unexpected, unexpected
print("Installed package contains only the four runtime files and documentation/licenses.")
PY
        runuser -u preview-user -- dbus-run-session -- python3 tests/integration/InstallTest.py --smoke-launchers /usr/bin/dzurkel
        ;;
    removed)
        for path in /usr/bin/dzurkel /usr/share/applications/dzurkel{,-settings}.desktop /usr/share/kio/servicemenus/dzurkel.desktop; do
            test ! -e "$path"
        done
        ;;
    *) exit 2 ;;
esac
