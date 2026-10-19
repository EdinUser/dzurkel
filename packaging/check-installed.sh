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
           "/usr/share/kio/servicemenus/dzurkel.desktop", "/usr/share/dzurkel/build-compatibility.json"}
runtime.update(f"/usr/share/icons/hicolor/{size}x{size}/apps/dzurkel.png"
               for size in (16, 22, 24, 32, 48, 64, 128, 256))
runtime.update(f"/usr/share/icons/hicolor/scalable/apps/{name}.svg"
               for name in ("dzurkel", "dzurkel-symbolic"))
files = {path for path in paths if pathlib.Path(path).is_file()}
assert runtime <= files, runtime - files
adapters = {path for path in files
            if path.startswith(("/usr/lib/", "/usr/lib64/"))
            and path.endswith(tuple("/dzurkel/plugins/lib" + name + ".so" for name in ("dzurkel_highlighting", "dzurkel_audio", "dzurkel_desktop")))}
assert len(adapters) == 3, adapters
unexpected = {path for path in files - runtime
              if path not in adapters
              and not path.startswith(("/usr/share/doc/dzurkel/", "/usr/share/licenses/dzurkel/"))}
assert not unexpected, unexpected
print("Installed package contains the runtime files, optional adapters and documentation/licenses.")
PY
        runuser -u preview-user -- dbus-run-session -- python3 tests/integration/InstallTest.py --smoke-launchers /usr/bin/dzurkel
        ;;
    removed)
        for path in /usr/bin/dzurkel /usr/share/applications/dzurkel{,-settings}.desktop /usr/share/kio/servicemenus/dzurkel.desktop /usr/share/dzurkel/build-compatibility.json; do
            test ! -e "$path"
        done
        for size in 16 22 24 32 48 64 128 256; do
            test ! -e "/usr/share/icons/hicolor/${size}x${size}/apps/dzurkel.png"
        done
        test ! -e /usr/share/icons/hicolor/scalable/apps/dzurkel.svg
        test ! -e /usr/share/icons/hicolor/scalable/apps/dzurkel-symbolic.svg
        for directory in /usr/lib/dzurkel/plugins /usr/lib64/dzurkel/plugins /usr/lib/*/dzurkel/plugins; do
            for adapter in audio highlighting desktop; do
                test ! -e "$directory/libdzurkel_$adapter.so"
            done
        done
        ;;
    *) exit 2 ;;
esac
