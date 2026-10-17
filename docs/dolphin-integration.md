# Set up Dolphin Space previews

After installing, open **Dzurkel Settings** from the application menu or run
`dzurkel --settings`. Choose **Enable Space shortcut…**, review the changes, and
allow Dolphin to restart if prompted. Installation does not assign shortcuts.
You can also right-click a file, choose **Dzurkel**, and open Settings from the
preview. This targets Dolphin 26.04 or newer on KDE Plasma.

## Setup

Build and install using the README. Select exactly one local or authenticated SMB file in
Dolphin. The service menu restricts selection to one URL and lists local, SMB
and experimental SFTP locations;
`%u` passes that URL directly to the application, without a shell wrapper.

The title bar displays the filename; **File information** shows the full path. Check these against your
selection, especially when using spaces, Unicode, `#`, `%`, quotes, or `$` in a
filename. Start a fresh primary with `--diagnostics` from a terminal to log the Qt
platform, PID, and window activation changes. The displayed path and D-Bus
`FileUrl` property identify the resolved file. Diagnostics do not prove that
Dolphin supplied the right URL: verify the path shown by the actual menu action.

## Desktop acceptance checklist

The preview's bottom-right **Settings** link opens desktop setup and the current
preview-support list. Information icons provide hover tooltips and clickable,
keyboard-accessible explanations. Choose **Enable Space shortcut…** to configure
Space. If Dolphin is running, confirm **Restart Dolphin and apply**; Dzurkel
requests a normal close for every detected window, waits up to 30 seconds for
all Dolphin processes to exit, then updates the shortcut file and reopens it.
Respond to any close prompts in Dolphin. Cancellation, a refused close, an
unreachable process, or a newly detected process leaves shortcut files unchanged.
No process is killed. Dolphin's startup settings control window/tab restoration;
the confirmation explains that all previous views may not return. A launch
failure is reported with instructions to open Dolphin manually.

After successful editing and reopening, Dzurkel waits asynchronously for a
Dolphin window to report active, with a ten-second readiness budget. It requests
Settings activation once. KDE Frameworks 6.19+ captures a Wayland token from the
confirmed restart action; Qt-only/older KDE builds use a best-effort Qt request.
Closing, minimizing, or destroying Settings, or starting another shortcut change,
cancels the pending handoff. Reopening Settings does not revive an old request.
There are no repeated activation requests or always-on-top flags. The compositor
may reject activation, including after intervening input in another application.
Automated tests verify the request lifecycle; native focus acceptance remains
a desktop check.

Restoring uses the same restart flow. This changes per-user bindings only;
it neither needs sudo nor runs automatically when Settings opens. It removes
unmodified Space (including sequences beginning with Space) from explicit
conflicting bindings and overrides Dolphin's default selection binding, keeping
alternate shortcuts. The Dzurkel action becomes `Space`.

A record of affected shortcut attributes is stored under
`$XDG_CONFIG_HOME/dzurkel/dolphin-space-backup.json` (normally `~/.config/`).
**Restore previous shortcuts** restores those attributes and keeps unrelated
toolbar or shortcut edits. If a managed shortcut was changed after setup,
restoration refuses to overwrite the newer choice. The integration must be
installed before the setup button is enabled; viewer-package installation
remains a deferred feature.

The helper edits the user-local `kxmlgui5/dolphin/dolphinui.rc` beneath
`XDG_DATA_HOME` (normally `~/.local/share/`), using the service action ID
`servicemenu_dzurkel.desktop::dzurkel`. Malformed or ambiguous XML and direct
symlinks are refused. End-to-end application of the changed bindings still needs
the real Dolphin checks below.

Automatic setup currently supports Dolphin's Default shortcut scheme. For a
custom scheme, use Dolphin's shortcut editor; setup refuses to alter custom or
ambiguous scheme sections.

Run on the actual KDE Plasma Wayland desktop and record results in `testing.md`.

- [ ] One selected file exposes **Dzurkel**; multiple selections do not.
- [ ] Context-menu invocation displays the selected file's correct path.
- [ ] Paths containing spaces and Unicode survive menu invocation unchanged.
- [ ] The Dzurkel action is available in Dolphin's shortcut configuration.
- [ ] Assign Space using Settings or Dolphin's shortcut editor; verify backup/restore.
- [ ] Cancel restart: no Dolphin window closes and no shortcut configuration changes.
- [ ] Confirm restart with multiple windows/tabs; respond to close prompts and check startup restoration.
- [ ] After apply and restore, Settings receives keyboard input once Dolphin reopens.
- [ ] Close/minimize Settings during reopening; it is not raised afterward, even if restored.
- [ ] Switch applications during restart; verify compositor focus policy is respected.
- [ ] Refuse a Dolphin close prompt; after the timeout, shortcut files remain unchanged.
- [ ] Select a file and press Space; preview opens and receives keyboard focus.
- [ ] Press Space again; the window closes and Dolphin is usable immediately.
- [ ] Repeat with Escape and Alt+F4 for dismissal.
- [ ] Repeat the open/dismiss cycle at least 20 times with different files.
- [ ] Resize from each border, maximize/restore, and reopen: chosen size persists.
- [ ] Quit Dzurkel, then preview an unsupported file: its first window is compact and placed correctly.
- [ ] Test light/dark themes, scaling, and each monitor in a multi-screen setup.
- [ ] If Space is unreliable, test Shift+Space and record the exact limitation.

## Focus and placement limitations

The preview is an ordinary reusable top-level Qt window. Dismissal now hides
it while the primary process remains resident; `dzurkel --quit` ends the service.
It targets the pointer's screen; content previews start at 85% of available
screen width and height. The first information-only view maps at its compact
size, after asynchronous provider selection, so it does not inherit a large
loading window's top-left position. Standard window borders allow resizing and maximizing;
the initial size is not a maximum. A resized window keeps its size across previews
within the running process, fitting a smaller screen on reopening. Screen and
available-geometry changes also fit a normal window to its current display. The Wayland
compositor controls final placement and activation; positioning is best effort.
The app does not force focus, stay on top, inject global keys, inspect Dolphin,
or change its settings unless shortcut setup is explicitly selected. Closing
the window lets the compositor restore focus.

KDE builds forward the launcher's `XDG_ACTIVATION_TOKEN` to the resident window
through KDE WindowSystem's public activation API. A hidden application desktop
entry supplies the Wayland identity `dzurkel`; it does not add a file association
or assign shortcuts. Existing service-menu filename and action IDs are retained
so user-assigned shortcuts keep their identity. Tokenless launches and Qt-only
builds have best-effort activation. The compositor can refuse any focus request.

An explicit Show request, including Dolphin's Space action, requests activation
even when the preview is already visible behind another window. A minimized
preview is restored while retaining its normal/maximized state. Activation for
an already-mapped window happens when the request arrives; content loading does
not replay its token later. Asynchronous folder navigation preserves focus
rather than requesting it again. Clicking elsewhere does not dismiss Dzurkel.

If a window opens without focus, Space/Escape cannot dismiss it until focused.
Use the title-bar close control in that case.
Record how it was launched, shortcut conflicts, activation diagnostic output,
monitor/scaling, and desktop versions. Do not report the interaction as verified
based only on an offscreen test or successful build.

Desktop integration is accepted only when selected-file delivery and repeated
open/dismiss/return behavior are demonstrated on the target desktop. The app
reuses the existing window and process across invocations. Test both
fresh-process and resident-process launches; Wayland activation may differ.
Quit any old resident binary before testing an updated installation.

## References

- [KDE service-menu format and authorization](https://develop.kde.org/docs/apps/dolphin/service-menus/)
- [Dolphin 26.04 service-menu shortcut support](https://blogs.kde.org/2026/05/03/dolphin-26.04-release/)
- [Qt top-level window behavior](https://doc.qt.io/qt-6/qwidget.html)
- [KDE activation API](https://api.kde.org/kwindowsystem.html)

## Direct SMB locations

The service menu lists `file`, `smb` and `sftp` URLs; SFTP is rejected until
**Experimental SFTP previews** is enabled in Dzurkel Settings. This static menu
listing is not a support/availability guarantee. See [compatibility](compatibility.md).
Open an `smb://` location in Dolphin
and authenticate first, then use the same preview action/shortcut. Dzurkel resolves
the original URL through KIO-FUSE rather than mapping it to an existing `/mnt`
configuration. See [SMB requirements and limits](previews.md#smb-resources).
Native authenticated-share acceptance remains recorded in [testing](testing.md).

## Space stops working while the context menu works

The user observed this on Dolphin 26.08.1 and confirmed that closing all Dolphin
windows and reopening restored Space. Dzurkel remained responsive on D-Bus and
its saved Space binding was present. This resembles the upstream report of
[service-menu shortcuts stopping during use](https://bugs.kde.org/show_bug.cgi?id=525749);
the precise trigger has not been reproduced here. Reinstallation or killing
Dzurkel is unnecessary for this observed failure. Settings says **Space shortcut
configured** because it checks saved configuration, not Dolphin's active binding.
