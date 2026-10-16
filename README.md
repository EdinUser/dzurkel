# Dzurkel

**Peek-a-Dzurkel!**

Dzurkel (Дзъркел, Bulgarian slang for “eye”) is a standalone C++20 / Qt 6
Widgets file-preview application for Fedora KDE Wayland.

## Current scope: Milestone 1

Dzurkel displays the filename and absolute path for one readable local file or
`file://` URL. One primary process owns the session-bus service and reuses one
preview window. Later CLI invocations forward their commands to that process.

- `dzurkel <file>` or `dzurkel --show <file>` opens or replaces the preview.
- `dzurkel --hide` dismisses it while the service stays resident.
- `dzurkel --toggle <file>` hides a visible preview or opens the supplied file.
- `dzurkel --quit` gracefully stops the resident process.
- `dzurkel --help` and `dzurkel --version` work without starting a GUI.

Space, Escape, and ordinary window close hide the preview. Hide/Quit with no
running instance succeed without starting one. Relative paths resolve against
the invoking client's directory. Invalid requests leave the current preview
unchanged. Image, PDF, and text rendering remain planned for Milestone 2.

## Build and run

On Fedora:

```bash
sudo dnf install gcc-c++ cmake make qt6-qtbase-devel
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/dzurkel --show "README.md"
```

CMake 3.22+ and Qt 6.4+ Core, Gui, Widgets, DBus, and Test are required. The
minimum Qt version is a configured baseline, not a compatibility certification;
see the [test report](docs/testing.md) for the version actually tested. Tests also
require `dbus-run-session` (Fedora package `dbus-daemon`). No PDF or KDE Frameworks
development packages are needed for this milestone.

```bash
./build/dzurkel --help
./build/dzurkel --version
./build/dzurkel --diagnostics --show "/path/with spaces/пример.txt"
./build/dzurkel -- "./-filename.txt"
./build/dzurkel --hide
./build/dzurkel --toggle "README.md"
./build/dzurkel --quit
```

Remote URLs and directories are rejected. File contents are never executed or
modified. Diagnostic logging is opt-in for a new primary process and reports its
PID, Qt platform, and activation changes. It does not reconfigure an existing
service.

## Dolphin integration

After building, install into the prefix configured above:

```bash
cmake --install build
```

This installs `~/.local/bin/dzurkel` and an executable service menu at
`~/.local/share/kio/servicemenus/dzurkel.desktop` (assuming default XDG data
locations). Its generated command uses the absolute installed executable path,
so Dolphin does not depend on your shell's PATH. Configure a different prefix
before building rather than overriding it at install time. With a custom
`XDG_DATA_HOME`, place the generated `build/dzurkel.desktop` in that location's
`kio/servicemenus/` directory and mark it executable.

Right-click one local file in Dolphin and choose **Dzurkel**. If needed, enable
it under Dolphin's context-menu settings and reopen Dolphin. In Dolphin 26.04+,
open **Configure Keyboard Shortcuts**, search for **Dzurkel**, and assign
Space manually. Review any conflict before changing an existing binding. If
Space conflicts or does not work, test Shift+Space and record the result.

Follow [the integration checklist](docs/dolphin-integration.md) to validate
shortcut and focus behavior on your desktop. Installing the menu does not assign
any shortcut.

Before updating or removing an installed running service, run `dzurkel --quit`
(or `~/.local/bin/dzurkel --quit`). New CLI commands otherwise continue reaching
the old resident process.

To remove this user-local installation, remove only the two installed files
listed above; retain your source checkout and any unrelated menu entries.

## Documentation

- [Dolphin integration](docs/dolphin-integration.md): shortcut setup, desktop
  checks, and Wayland focus limitations.
- [D-Bus API](docs/dbus-api.md): control Dzurkel independently of Dolphin,
  inspect preview state, and handle errors.
- [Testing and validation](docs/testing.md): supported test environment,
  automated checks, memory measurements, and pending desktop verification.
