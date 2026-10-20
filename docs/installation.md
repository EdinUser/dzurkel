# Install Dzurkel on Linux

Dzurkel integrates with Dolphin, Nautilus and Thunar. Native package targets are
Fedora 44, Debian 13 and current Arch Linux; Fedora KDE Wayland is the primary
development environment. Dolphin Space setup requires 26.04 or newer. See
[testing](testing.md) for exact environments and remaining desktop checks.
The Fedora source installer builds the checked-out
source using installed Fedora packages. It does not download source, install
packages, assign shortcuts, or change file associations. The RPM route below
installs a prebuilt package and resolves runtime dependencies through DNF.

## Install an RPM

The first package target is Fedora 44 x86_64. Download the matching binary RPM
from [GitHub Releases](https://github.com/EdinUser/dzurkel/releases).
Packages are built only when a release or prerelease is published. Additional
build artifacts are available from successful
[release workflow runs](https://github.com/EdinUser/dzurkel/actions/workflows/ci.yml).

```bash
sudo dnf install ./dzurkel-<version>-1.fc44.x86_64.rpm
dzurkel --settings
```

Use the actual downloaded filename. **Dzurkel Settings** is also available in
the application menu. Choose **Enable Space shortcut…** to configure Dolphin
for your user. RPM installation does not open a GUI or change shortcut files.
The package uses system Qt/KDE libraries; optional KIO services enable direct
network URLs as described below.

The current source includes an optional KDE file-dialog adapter, separate from
Dolphin shortcut setup. It is not included in the published 0.8.0 installers.
With a build containing the adapter, enable **Space previews in KDE file
dialogs** in Settings and sign out/in. Disable it and sign out/in before
uninstalling to clean up the managed session environment. See
[KDE file-dialog setup and build requirements](file-dialog-previews.md).

Before migrating from a source installation, run `~/.local/bin/dzurkel --quit`
and remove its managed files listed below, including any built adapters.
User-local executables and
service menus can shadow the system package. Keep your configuration and backups.

To update, quit the resident service with `dzurkel --quit`, then install the newer
RPM using the same DNF command. There is no update repository yet. To remove:

```bash
dzurkel --quit
sudo dnf remove dzurkel
```

Package removal retains per-user settings and shortcut backups. Use **Restore
previous shortcuts** in Settings before uninstalling if you want to undo setup.

## Debian and Arch packages

The package name is `dzurkel` on all three distributions. Additional build targets
are **Debian 13 (trixie), amd64** and **current Arch Linux, x86_64**. These are
upstream packages, not entries in the Debian archive or Arch repositories/AUR.
Ubuntu and Arch derivatives are not release package targets. Debian 12 has a
separate baseline source/test gate; it has no release DEB or automatic desktop
support claim. Arch is rolling:
fully update the system before installing; older Qt/KDE libraries may not satisfy
a newly built binary.

Download your installer and `SHA256SUMS` from
[GitHub Releases](https://github.com/EdinUser/dzurkel/releases). Verify the
downloaded installer in the same directory:

```bash
sha256sum --check --ignore-missing SHA256SUMS
```

The release contains only the three runtime installers and one checksum manifest;
GitHub also provides its standard source archive links. Packages are unsigned and
checksums are not signatures. There is no automatic Dzurkel update repository.

On Debian 13, install the binary (not the optional debug package):

```bash
sudo apt install ./dzurkel_0.8.0-1_amd64.deb
dzurkel --settings
```

On current Arch Linux:

```bash
sudo pacman -Syu
sudo pacman -U ./dzurkel-0.8.0-1-x86_64.pkg.tar.zst
dzurkel --settings
```

Use the actual filenames downloaded. Developers can obtain build metadata,
debug packages and source packaging from successful
[Actions artifacts](https://github.com/EdinUser/dzurkel/actions/workflows/ci.yml).

The DEB includes the audio/video and highlighting adapters. Their development
SDKs are build dependencies, not end-user requirements. Normal APT installation
installs recommended media/highlighting runtimes; `--no-install-recommends` can
omit them, leaving baseline previews available. Arch lists those runtimes as
optional dependencies; Fedora recommends highlighting and suggests media support.
Missing runtimes are explained in Settings; installed codecs still determine
which media files can play.
For Actions artifacts, extract the ZIPs first and use their distribution-specific
checksum manifests; those manifests cover all files in each artifact.

Both packages install `/usr/bin/dzurkel`, the hidden application identity
`/usr/share/applications/dzurkel.desktop`, the visible **Dzurkel Settings**
launcher `/usr/share/applications/dzurkel-settings.desktop`, and the Dolphin
menu `/usr/share/kio/servicemenus/dzurkel.desktop`. The main application entry is
intentionally hidden: open a file using Dolphin's context menu or the CLI.

Debian shared-library dependencies are generated by `dpkg-shlibdeps`; Wayland
support additionally requires `qt6-wayland`. Arch runtime requirements are
`qt6-base`, `qt6-webengine` (which provides QtPdf), `qt6-wayland`,
`libzip`, `glibc` and `gcc-libs`. Qt Multimedia and KF6 WindowSystem are optional
runtime dependencies of the packaged adapters. Syntax highlighting, antiword and
extra image readers are optional too. See [the runtime baseline](runtime-baseline.md). Extra image plugins are
recommended/optional (`qt6-image-formats-plugins` on Debian,
`qt6-imageformats` on Arch). Optional network previews use `kio-fuse` and
`kio-extras` on either distribution.

**Debian 13 ships Dolphin 25.04, below the 26.04+ Space setup target.** Use CLI
previews and the context menu; Space setup is not supported with stock trixie
Dolphin. Do not upgrade the desktop from another Debian suite just for this
package. Debian and Arch KDE desktop acceptance remains pending: container
checks do not establish focus, menu discovery, shortcuts, scaling or themes.
See [testing](testing.md) and [known shortcut issue #4](https://github.com/EdinUser/dzurkel/issues/4).

Before switching from a user-local installation, follow the migration steps
above and preserve settings/shortcut backups. To update, quit the resident
service and repeat the appropriate install command with the new file. To remove:

```bash
dzurkel --quit
sudo apt remove dzurkel       # Debian
# Or:
sudo pacman -R dzurkel        # Arch
```

Removal keeps per-user preferences and shortcut backups. Restore shortcuts in
Settings first if desired. Package installation never launches Dzurkel or
changes shortcuts.

## Dependencies

Install the build and test dependencies explicitly:

```bash
sudo dnf install gcc-c++ cmake make pkgconf-pkg-config libzip-devel qt6-qtbase-devel qt6-qtpdf-devel qt6-qtmultimedia-devel \
  kf6-kwindowsystem-devel kf6-syntax-highlighting-devel dbus-daemon python3 desktop-file-utils
```

`packaging/fedora/install.sh --check` checks packages and tools without building
or installing. Missing dependencies produce a suggested command and a nonzero
exit. Optional audio/video, highlighting and desktop adapters require their development packages
only when building those adapters. The baseline preset needs no optional SDKs.
For missing-library workflow tests, keep build SDKs out of the runtime VM; the
[Arch VM workflow](arch-vm-testing.md) builds in an isolated host container.

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
and unused-section removal, then builds, runs the configured test suites, validates the
application desktop entry, and installs a stripped executable. A failed build or
test stops before the live installation is changed. If the target already has
Dzurkel installed, the script first requests shutdown through the current user's
session bus. Run inside your desktop session when updating a resident service.

The core installation contains:

```text
~/.local/bin/dzurkel
~/.local/share/applications/dzurkel.desktop
~/.local/share/applications/dzurkel-settings.desktop
~/.local/share/kio/servicemenus/dzurkel.desktop
~/.local/share/dzurkel/build-compatibility.json
~/.local/share/icons/hicolor/<size>x<size>/apps/dzurkel.png
~/.local/share/icons/hicolor/scalable/apps/dzurkel.svg
~/.local/share/icons/hicolor/scalable/apps/dzurkel-symbolic.svg
```

Color icons are installed at 16, 22, 24, 32, 48, 64, 128 and 256 px, along with
the scalable color and symbolic icons. Application windows also have embedded
color icons when run directly from the build directory.

Built optional adapters are installed below the configured library directory as
`dzurkel/plugins/libdzurkel_audio.so`, `libdzurkel_highlighting.so` and
`libdzurkel_desktop.so`. The library directory may be `lib`, `lib64` or a
distribution-specific multiarch path. The build's `install_manifest.txt` lists
the exact installed paths, including only the adapters built by that configuration.

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
`kio/servicemenus/` directories, and the installed `icons/hicolor/` tree into
that data directory's `icons/hicolor/`; keep the service menu executable. The installer
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

The install test creates a temporary staging root, checks the core files and
enabled-adapter manifest and permissions, then launches the staged binary on an isolated
bus with a Unicode filename. It verifies headless Show/Hide/Quit forwarding and
clean primary shutdown. It never changes your normal session's Dzurkel instance.

Repeatable here means building the same source with declared dependencies and
commands. Byte-identical builds across different Fedora/compiler/Qt releases
are not claimed; record package versions when comparing builds.

## Removal

Quit the resident service, then remove the core files and built adapters from
the prefix you installed, using `install_manifest.txt` to identify the exact
managed paths. Retain other service menus and shared Qt/KDE packages used by other
applications. Build directories and source files are development artifacts and
are not part of the installed application.
