# Dzurkel

**Peek-a-Dzurkel!**

Dzurkel (Дзъркел, Bulgarian slang for “eye”) is a standalone C++20 / Qt 6
Widgets file-preview application for Fedora KDE Wayland.

## Current scope: local 0.6.0 build

Dzurkel previews images, PDFs, and plain text from local paths, `file://` URLs,
and direct `smb://` URLs through the desktop KIO-FUSE service. Mounted shares
continue to work as filesystem paths. Direct SMB requires desktop authentication;
see [remote resources](docs/previews.md#smb-resources).
SFTP URLs can be enabled through **Experimental SFTP previews** in Settings;
they are off by default and await real-server validation. See the
[compatibility matrix and testing guide](docs/compatibility.md).
Unsupported files, directories, links, and special objects get an information
view. One primary process owns the session-bus service and reuses one preview
window. Later CLI invocations forward their commands to that process.

- `dzurkel <file>` or `dzurkel --show <file>` opens or replaces the preview.
- `dzurkel --hide` dismisses it while the service stays resident.
- `dzurkel --toggle <file>` hides a visible preview or opens the supplied file.
- `dzurkel --quit` gracefully stops the resident process.
- `dzurkel --help` and `dzurkel --version` work without starting a GUI.

Space, Shift+Space, Escape, and ordinary window close hide the preview. Hide/Quit with no
running instance succeed without starting one. Relative paths resolve against
the invoking client's directory. Malformed or unsupported URLs leave the current preview unchanged. Missing,
unreadable, or unavailable resources produce an asynchronous information view. Content loads asynchronously, and newer requests supersede unfinished
previews. See [supported formats and limits](docs/previews.md).

Closing the window leaves one process resident for faster reopening. Process
monitors may list its Qt/D-Bus/Wayland threads as separate rows sharing the same
memory. Temporary worker threads can disappear after an idle timeout; this does
not indicate extra application instances. Use `dzurkel --quit` to exit fully.

Content previews initially fill 85% of the usable screen width and height. Drag the
window borders to resize, or maximize it using the KDE title bar. Your chosen
size is retained across previews until the process exits; reopening on a smaller
screen fits it to that screen. Images retain their proportions as the window
changes size, and PDF pages scale to the available width.
Files without a content preview temporarily use a compact information window.
On a fresh launch, that compact size is chosen before the first window appears.
Opening an image, PDF, or text preview restores your previous preview size and
maximized state.

Click **Settings** at the bottom right of the preview to open desktop setup and
see available preview support. Hover over an information icon or click it for
details. **Enable Space shortcut…** backs up the affected
bindings and reassigns Space; **Restore previous shortcuts** undoes that setup
while preserving unrelated settings. If Dolphin is running, Dzurkel asks to
restart it, waits for all its processes to close normally, applies the change,
and reopens it. If it stays open, no shortcut files are changed. Window and tab
restoration follows Dolphin's startup settings. After Dolphin becomes active,
Dzurkel requests Settings focus once; closing or minimizing Settings cancels
that handoff. The desktop controls whether activation is allowed. Opening
Settings alone changes nothing and needs no sudo.

## Build and run

On Fedora:

```bash
sudo dnf install gcc-c++ cmake make qt6-qtbase-devel qt6-qtpdf-devel \
  kf6-kwindowsystem-devel dbus-daemon python3 desktop-file-utils
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/dzurkel --show "README.md"
```

CMake 3.22+ and Qt 6.4+ Core, Gui, Widgets, DBus, Concurrent, Pdf, PdfWidgets,
and Test are required. The minimum Qt version is a configured baseline, not a
compatibility certification; see the [test report](docs/testing.md) for the
version actually tested. Tests also require `dbus-run-session` (Fedora package
`dbus-daemon`). KDE WindowSystem provides Wayland activation-token support. A
Qt-only build is available with `-DDZURKEL_WITH_KDE_ACTIVATION=OFF`; activation
of an existing window is then best effort without token forwarding to KDE. The
optional `qt6-qtimageformats` package enables additional image readers.

For a tested, size-focused Fedora installation, run
`./packaging/fedora/install.sh`. It builds and tests the checkout, then installs
a stripped executable into `~/.local`. See [installation and staging](docs/installation.md)
for dependency checks, custom prefixes, and removal. Development Debug builds
above retain symbols; optimized configurations are available as
`cmake --preset release` and `cmake --preset compact`.

```bash
./build/dzurkel --help
./build/dzurkel --version
./build/dzurkel --diagnostics --show "/path/with spaces/пример.txt"
./build/dzurkel -- "./-filename.txt"
./build/dzurkel --hide
./build/dzurkel --toggle "README.md"
./build/dzurkel --quit
```

Remote URLs are rejected. Directories and symbolic links show information only.
File contents are never executed or modified. Diagnostic logging is opt-in for a
new primary process and reports its PID, Qt platform, and activation changes. It
does not reconfigure an existing service.

## Dolphin integration

After building, install into the prefix configured above:

```bash
cmake --install build
```

This installs `~/.local/bin/dzurkel`, a hidden application identity at
`~/.local/share/applications/dzurkel.desktop`, and an executable service menu at
`~/.local/share/kio/servicemenus/dzurkel.desktop` (assuming default XDG data
locations). Its generated command uses the absolute installed executable path,
so Dolphin does not depend on your shell's PATH. Configure a different prefix
before building rather than overriding it at install time. With a custom
`XDG_DATA_HOME`, place the generated `build/dzurkel.desktop` in that location's
`kio/servicemenus/` directory and mark it executable.

Right-click one local file in Dolphin and choose **Dzurkel**. If needed, enable
it under Dolphin's context-menu settings and reopen Dolphin. Use Dzurkel's
**Settings** link for backed-up shortcut setup, or in Dolphin 26.04+ open
**Configure Keyboard Shortcuts**, search for **Dzurkel**, and assign Space
manually. Review any conflict before changing an existing binding. If Space
conflicts or does not work, test Shift+Space and record the result.

Follow [the integration checklist](docs/dolphin-integration.md) to validate
shortcut and focus behavior on your desktop. Installing the menu does not assign
any shortcut.

Before updating or removing an installed running service, run `dzurkel --quit`
(or `~/.local/bin/dzurkel --quit`). New CLI commands otherwise continue reaching
the old resident process.

To remove this user-local installation, remove only the three installed files
listed above; retain your source checkout and any unrelated menu entries.

## Documentation

- [Architecture](docs/architecture.md): module boundaries, asynchronous loading,
  resident lifecycle, and desktop setup.
- [Fedora installation](docs/installation.md): tested source installation,
  staging, dependency checks, and removal.
- [Performance and footprint](docs/performance.md): optimization settings,
  latency measurements, private memory, and stability checks.
- [File previews](docs/previews.md): supported content, scrolling, encoding,
  resource limits, and fallback behavior.
- [Dolphin integration](docs/dolphin-integration.md): shortcut setup, desktop
  checks, and Wayland focus limitations.
- [D-Bus API](docs/dbus-api.md): control Dzurkel independently of Dolphin,
  inspect preview state, and handle errors.
- [Testing and validation](docs/testing.md): supported test environment,
  automated checks, memory measurements, and pending desktop verification.

## Zoom and browsing files

Use Ctrl+wheel or `+`/`-` to zoom images and PDFs; `0` or **Fit** restores fit.
Plain wheel scrolls content. Zoom snaps to 10 percentage-point steps, and image
fit does not enlarge small images above 100%. GIFs play with Pause/Play controls.
**Previous**/**Next** and Alt+arrows browse files in natural filename order;
images/information views also accept plain Left/Right. Up/Down browse fitted
images/information views and scroll zoomed images, PDFs and text. See [preview controls and limits](docs/previews.md#zoom-and-file-navigation).
