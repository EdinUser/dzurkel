# KDE file-dialog previews

Dzurkel can optionally preview a highlighted item with Space inside KDE file-open
and upload dialogs. This covers compatible Qt 6 applications using KDE's
KFileWidget and applications using the KDE desktop portal file chooser.
Dolphin and Nautilus integrations keep their existing behavior.

Status (2026-10-20): implemented and tested in the current source, with a
user-reported smoke test on Fedora. Native KDE portal and kdialog checks passed
on Fedora KDE Wayland. The adapter is not included in the published 0.8.0
installers. The smoke report does not specify individual applications or
login/logout coverage; the targeted acceptance checks below remain open.

## Enable or disable

Update Dzurkel and its optional KDE chooser adapter together. In **Settings →
Integration**, select **Space previews in KDE file dialogs**, then sign out of
Plasma and sign back in. The setting is off by default. Disable it in the same
place and sign out/in again to remove the adapter from new application processes.
Existing applications and already-running portal services keep their startup
environment until restarted; Dzurkel does not restart them or end the session.

The setting creates one managed shell file under
`~/.config/plasma-workspace/env/dzurkel-chooser.sh`. It adds Dzurkel's private Qt
plugin directory and generic plugin to the session environment, preserving
existing plugin entries. It does not modify system Qt plugins or replace the
portal service. Disabling writes a small cleanup version of the file to remove
only Dzurkel's entries, including environments retained by a persistent user
manager. Unrelated or modified files and symbolic links are not overwritten.

If the adapter or its libraries are missing/incompatible, ordinary Dzurkel and
Qt application startup remain available. Settings disables enabling an adapter
that cannot be loaded; an existing opt-in can still be disabled.

## Controls and scope

- Select one file or folder in the chooser's file list and press unmodified Space.
- The chooser remains open while Dzurkel displays the preview. Open/Upload is not
  activated and no file is submitted to the calling application.
- Space/Escape in Dzurkel dismisses the preview and requests focus back to the
  originating chooser using the compositor's activation token.
- Cancelling/closing the chooser closes only its current owned preview. A newer
  preview opened from Dolphin, Nautilus, the CLI or another dialog is preserved.
- Filename, filter, search and inline-editor fields retain normal text input.
  Multiple selections, modified Space and Save dialogs keep their normal behavior.
- This initial adapter previews on Space; it does not follow subsequent chooser
  selection changes automatically. Preview-side file navigation is disabled so
  it cannot browse outside the chooser's order or filters.

Existing resource policies apply, including supported local/mounted/SMB paths,
SFTP opt-in, cancellation and format limits. Folder previews show basic
information and optional [desktop-backed contents size](folder-previews.md).

This is an opt-in module inside compatible Qt application processes, scoped to
KDE file chooser widgets. GTK-only/custom pickers and incompatible Qt versions
need separate integration. The existing working Ubuntu GNOME Firefox/Nautilus
route does not need this KDE adapter. Cross-distribution KDE acceptance on
Kubuntu/Arch and broader application combinations remain follow-up checks.

## Build and validation

`DZURKEL_KDE_CHOOSER=AUTO` builds the adapter when its SDK is available; `ON`
requires it and `OFF` omits it. It needs public KIO FileWidgets/Core and
CoreAddons headers/runtimes plus KF6 WindowSystem 6.19 or newer. Fedora packages
use `kf6-kio-devel`, `kf6-kcoreaddons-devel` and `kf6-kwindowsystem-devel`.
The core executable gains no KIO/WindowSystem startup dependency. The baseline
preset explicitly disables the chooser adapter.

The module installs under `<libdir>/dzurkel/qt-plugins/generic/`. Test-only input
drivers and smoke hosts are never installed. `dzurkel --serve` starts the usual
single-instance service without opening a window; the adapter uses it when no
resident is running, then calls the caller-scoped D-Bus methods.

The regular CTest suite checks automatic plugin loading, startup, cancellation,
caller/session ownership, ordinary input and missing/corrupt/incompatible runtime
fallback. `dzurkel_chooser_tests nativeDialogFocusAndCancellation` is a native
KDE Wayland check when run with the adapter's Qt environment and a private bus.

An optional native harness runs the installed KDE portal backend or kdialog
with generated files, a private bus and a test-only Qt input driver:

```sh
env DZURKEL_PRIVATE_TEST_BUS=1 dbus-run-session -- \
  python3 tests/integration/KdePortalNativeTest.py --build-dir build \
  --output /tmp/dzurkel-kde-portal-check
```

Use a new output directory for each run. Add `--host kdialog` for the native
application path. Requires the actual KDE Wayland session, PyGObject and built
test targets. The harness checks actual active-window state, preview dismissal,
focus return and cancellation. Its keys are Qt-delivered test events; this is
not physical-keyboard or all-browser acceptance. It never changes the live
portal registration or the normal resident Dzurkel instance.

Recorded validation includes 26/26 full-build CTest suites and 20/20 baseline
suites with optional adapters disabled. Native portal, kdialog and QFileDialog
checks exercised preview, dismissal, focus return and cancellation. See
[the testing report](testing.md) for the recorded environments and limitations.

Remaining desktop acceptance:

- Enable through Settings, sign out/in, and verify both opt-in and opt-out in a
  normal Plasma session, including already-running user services.
- Check Chrome and Firefox upload dialogs and Photocraft Open with generated
  fixtures; verify physical Space/Escape, focus return and cancellation without
  accepting or uploading a file.
- Validate compatible KDE configurations on Kubuntu and Arch KDE separately.
  Preserve the user-confirmed Ubuntu GNOME Firefox/Nautilus route; investigate
  additional chooser adapters only where a gap is demonstrated.
