# Folder information and contents size

Folder previews show an icon, name, location, type and modification time.
Version 0.9.0 adds asynchronous contents size through optional KIO and GIO
adapters. The older 0.8.0 installers do not include these calculators.

The old directory-entry size (often 4 KiB) is no longer presented as contents
size. With the adapter available, the view initially says Calculating, then
updates bytes and file/subfolder counts. The information dialog, tooltip and
accessible description use the same result. Stop calculating keeps the count
obtained so far; closing or replacing the preview cancels the job.

## Meaning and limits

Dzurkel displays the desktop calculator's logical contents size, rather than
allocated disk space. The selected desktop library determines traversal, link,
hard-link and error semantics. With KIO 6.30 or newer, directory
metadata and symbolic-link targets are excluded from bytes, hard-linked file
identities are counted once, hidden files are included and sparse files use
their logical lengths. Counts follow KIO's definitions, including its handling
of special objects. This is not a filesystem snapshot or a guarantee that all
concurrently changing or inaccessible entries were observed.

GIO uses apparent file sizes, includes hidden files, omits directory metadata,
counts hard-link entries separately and counts a symlink's own text without
following its target. Its file count includes non-directory objects. Its root
directory count is removed from the displayed subfolder count. GIO stops at
filesystem boundaries, so mounted filesystems nested inside the selection are
excluded. A selected folder symlink is resolved once before measurement.

While running, values are labelled counted so far. Backend-reported errors
produce an explicitly partial result with the reported explanation. Calculation
stops after 30 seconds and keeps an incomplete total. There is no percentage
estimate, persistent size cache or automatic rescan when a folder changes;
reopen the preview to calculate again.

Only the selected folder is submitted. Local and mounted paths work through
the selected desktop library. Direct SMB and opted-in SFTP reuse Dzurkel's existing
preparation and KIO-FUSE resolution, then submit the resolved path to the same
calculator. No second authentication route or new URL scheme is enabled.
Remote metadata enumeration can take substantial time and generate server
requests; no file contents are read by Dzurkel for this calculation.

If the module or its dependencies are unavailable, contents size says
Unavailable with an explanation; basic folder information and other previews
remain usable. KDE prefers KIO and other desktops prefer GIO; if the preferred
module cannot load, the other is tried. Both remain outside the core executable's
dependencies. An older incompatible module is rejected by interface version.

## Build

`DZURKEL_FOLDER_SIZE=AUTO` builds available adapters using GIO >= 2.78 or public
KIOCore/CoreAddons >= 6.30 headers and libraries; `ON` requires at least one
compatible backend and `OFF` omits both. The
baseline preset disables it. For example:

```sh
cmake -S . -B build -DDZURKEL_FOLDER_SIZE=ON
cmake --build build
ctest --test-dir build --output-on-failure -R 'folder_size|resources|install'
```

Modules are installed as
`<libdir>/dzurkel/plugins/libdzurkel_folder_size.so` (KIO) and
`libdzurkel_folder_size_gio.so` (GIO). Each needs its matching runtime. The KIO
module references the 6.30 ABI so older libraries fail optional loading; older
KIO SDKs do not build it. GIO also checks its runtime floor, since older versions
include directory metadata in apparent totals. Fedora SDK packages are
glib2-devel, kf6-kio-devel and kf6-kcoreaddons-devel; Debian/Ubuntu use
libglib2.0-dev for GIO, and Arch uses glib2 with pkgconf.
Restart Dzurkel after replacing a loaded adapter. No login/session opt-in is
needed for folder calculation; the KDE file-dialog opt-in is independent.

The adapters use the public [KIO directory-size API](https://api.kde.org/kio-directorysizejob.html)
and [GIO measurement API](https://docs.gtk.org/gio/method.File.measure_disk_usage.html).
KIO owns an asynchronous job; GIO's synchronous API runs on a worker with a
cancellable token and GUI-thread progress polling. Cancellation does not wait
on slow filesystem calls, and at most two GIO measurements may remain outstanding.
Neither adapter implements a recursive scanner. See [testing evidence](testing.md).

## Cross-desktop validation and known gaps

Fedora KDE's installed build was smoke-tested successfully by the user.
On Arch XFCE with Qt 6.12.0 and an isolated KIO 6.31 runtime, generated-fixture
contents totals and native folder rendering passed. Without KIO, both Arch and
Ubuntu GNOME originally showed size as unavailable while preserving folder details.
Both VMs are now updated with GIO and display the exact 19-byte generated total,
3 files and 1 subfolder in native sessions, without installing KIO. Arch's normal
managed Thunar action was also upgraded and verified with actual folder
Space/Escape delivery through the installed application.

Ubuntu 26.04's KIO 6.24 counter includes directory-entry sizes. In a generated
19-byte tree it returned 179 bytes, so that older counter is incompatible with
contents-only sizing. The exact-total tests correctly
failed. The current build excludes that older KIO adapter and uses GIO instead;
upgrading the desktop libraries is unnecessary. The existing GIO API returned
19 bytes for the same generated fixture.

Native Nautilus folder Space/Escape dispatch works with the reinstalled Ubuntu
build. New Thunar actions include directories. Existing managed setups expose
**Update folder previews…** in Settings, preserving the original restore backup
and unrelated edits. Installation alone does not rewrite Thunar configuration.
