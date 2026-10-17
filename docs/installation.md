# Install Dzurkel on Fedora KDE

Dzurkel targets Fedora KDE Plasma with Qt 6 and Dolphin 26.04 or newer. The
current validation environment is Fedora 44; see [testing](testing.md) for exact
versions and remaining desktop checks. The source installer builds the checked-out
source using installed Fedora packages. It does not download source, install
packages, assign shortcuts, or change file associations. The RPM route below
installs a prebuilt package and resolves runtime dependencies through DNF.

## Install an RPM

The first package target is Fedora 44 x86_64. Download the matching binary RPM
from [GitHub Releases](https://github.com/EdinUser/dzurkel/releases) when available.
Development RPMs are also available as artifacts of successful
[Fedora CI runs](https://github.com/EdinUser/dzurkel/actions/workflows/ci.yml);
these are testing builds, not published releases.

```bash
sudo dnf install ./dzurkel-<version>-1.fc44.x86_64.rpm
dzurkel --settings
```

Use the actual downloaded filename. **Dzurkel Settings** is also available in
the application menu. Choose **Enable Space shortcut…** to configure Dolphin
for your user. RPM installation does not open a GUI or change shortcut files.
The package uses system Qt/KDE libraries; optional KIO services enable direct
network URLs as described below.

Before migrating from a source installation, run `~/.local/bin/dzurkel --quit`
and remove its four installed files listed below. User-local executables and
service menus can shadow the system package. Keep your configuration and backups.

To update, quit the resident service with `dzurkel --quit`, then install the newer
RPM using the same DNF command. There is no update repository yet. To remove:

```bash
dzurkel --quit
sudo dnf remove dzurkel
```

Package removal retains per-user settings and shortcut backups. Use **Restore
previous shortcuts** in Settings before uninstalling if you want to undo setup.

## Dependencies

Install the build and test dependencies explicitly:

```bash
sudo dnf install gcc-c++ cmake make pkgconf-pkg-config libzip-devel qt6-qtbase-devel qt6-qtpdf-devel \
  kf6-kwindowsystem-devel dbus-daemon python3 desktop-file-utils
```

`packaging/fedora/install.sh --check` checks packages and tools without building
or installing. Missing dependencies produce a suggested command and a nonzero
exit. Optional image-reader plugins and the future viewer-dependency policy
are separate from this source-build procedure.

## Optional SMB runtime support

The same optional packages provide the experimental SFTP route, which must also
be enabled in Settings. See [compatibility](compatibility.md). OpenSSH server and
bubblewrap are optional testing tools, not application requirements.

Direct `smb://` previews use the desktop's KIO-FUSE service and SMB worker:

```bash
sudo dnf install kio-fuse kio-extras
```

Authenticate through Dolphin before previewing. Existing CIFS mounts remain
usable as filesystem paths without this route. These optional packages are not
installed or configured automatically. See [SMB behavior](previews.md#smb-resources).

## User-local installation

Run from a source checkout as your regular user:

```bash
./packaging/fedora/install.sh
```

The script configures `build-fedora/` with `MinSizeRel`, link-time optimization,
and unused-section removal, then builds, runs all ten test suites, validates the
application desktop entry, and installs a stripped executable. A failed build or
test stops before the live installation is changed. If the target already has
Dzurkel installed, the script first requests shutdown through the current user's
session bus. Run inside your desktop session when updating a resident service.

Only these four files are installed by default:

```text
~/.local/bin/dzurkel
~/.local/share/applications/dzurkel.desktop
~/.local/share/applications/dzurkel-settings.desktop
~/.local/share/kio/servicemenus/dzurkel.desktop
```

The executable service menu retains the action ID `dzurkel`; the application
entry is hidden from launcher menus and supplies the Wayland identity. A separate
**Dzurkel Settings** entry opens shortcut setup without a preview. Follow
[Dolphin setup](dolphin-integration.md) to enable the action and assign a shortcut.
If your shell does not include `~/.local/bin`, invoke the executable by its full
path; Dolphin's generated command already uses the absolute path.

Use `--jobs 4`, `--build-dir /path/to/build`, or `--prefix /absolute/prefix` to
override defaults. `--qt-only` omits KDE activation integration, with best-effort
focus behavior. Custom `XDG_DATA_HOME` layouts require copying the three generated
desktop entries into that data directory's `applications/` and
`kio/servicemenus/` directories; keep the service menu executable. The installer
uses the selected prefix's `share/` directory.

## Staging and repeatability

Validate a separate installation without changing the live one:

```bash
./packaging/fedora/install.sh --prefix /usr \
  --destdir "$PWD/build-install-root" --build-dir "$PWD/build-fedora-stage"
```

The example writes to `build-install-root/usr/`, not `/usr`. `DESTDIR` is a staging
root, while the configured prefix determines desktop commands: this staged menu
will invoke `/usr/bin/dzurkel`. Use a matching prefix when deploying elsewhere.
An inherited `DESTDIR` is ignored; select staging explicitly with `--destdir`.

The install test creates a temporary staging root, checks the exact four-file
manifest and permissions, then launches the stripped staged binary on an isolated
bus with a Unicode filename. It verifies headless Show/Hide/Quit forwarding and
clean primary shutdown. It never changes your normal session's Dzurkel instance.

Repeatable here means building the same source with declared dependencies and
commands. Byte-identical builds across different Fedora/compiler/Qt releases
are not claimed; record package versions when comparing builds.

## Removal

Quit the resident service, then remove the four files above from the prefix you
installed. Retain other service menus and shared Qt/KDE packages used by other
applications. Build directories and source files are development artifacts and
are not part of the installed application.
