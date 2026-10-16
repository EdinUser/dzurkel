# Dolphin and Wayland desktop acceptance

## Setup

Build and install using the README. Select exactly one local regular file in
Dolphin. The service menu restricts selection to one URL and the `file` protocol;
`%u` passes that URL directly to the application, without a shell wrapper.

The app displays the resolved filename and path. Check these against your
selection, especially when using spaces, Unicode, `#`, `%`, quotes, or `$` in a
filename. Start a fresh primary with `--diagnostics` from a terminal to log the Qt
platform, PID, and window activation changes. The displayed path and D-Bus
`FileUrl` property identify the resolved file. Diagnostics do not prove that
Dolphin supplied the right URL: verify the path shown by the actual menu action.

## Desktop acceptance checklist

Run on the actual KDE Plasma Wayland desktop and record results in `testing.md`.

- [ ] One selected file exposes **Dzurkel**; multiple selections do not.
- [ ] Context-menu invocation displays the selected file's correct path.
- [ ] Paths containing spaces and Unicode survive menu invocation unchanged.
- [ ] The Dzurkel action is available in Dolphin's shortcut configuration.
- [ ] Assign Space manually, reviewing conflicts without erasing existing bindings.
- [ ] Select a file and press Space; preview opens and receives keyboard focus.
- [ ] Press Space again; the window closes and Dolphin is usable immediately.
- [ ] Repeat with Escape and Alt+F4 for dismissal.
- [ ] Repeat the open/dismiss cycle at least 20 times with different files.
- [ ] Test light/dark themes, scaling, and each monitor in a multi-screen setup.
- [ ] If Space is unreliable, test Shift+Space and record the exact limitation.

## Focus and placement limitations

The preview is an ordinary reusable top-level Qt window. Dismissal now hides
it while the primary process remains resident; `dzurkel --quit` ends the service.
It requests a position near the pointer's screen and limits its size to 80% of available geometry. The Wayland
compositor controls final placement and activation; positioning is best effort.
The app does not force focus, stay on top, inject global keys, inspect Dolphin,
or change its settings. Closing the window lets the compositor restore focus.

If a window opens without focus, Space/Escape cannot dismiss it until focused.
Record how it was launched, shortcut conflicts, activation diagnostic output,
monitor/scaling, and desktop versions. Do not report the interaction as verified
based only on an offscreen test or successful build.

Milestone 0 is accepted only when selected-file delivery and repeated
open/dismiss/return behavior are demonstrated on the target desktop. Milestone 1
reuses the existing window and process across invocations. Test both
fresh-process and resident-process launches; Wayland activation may differ.
Quit any old resident binary before testing an updated installation.

## References

- [KDE service-menu format and authorization](https://develop.kde.org/docs/apps/dolphin/service-menus/)
- [Dolphin 26.04 service-menu shortcut support](https://blogs.kde.org/2026/05/03/dolphin-26.04-release/)
- [Qt top-level window behavior](https://doc.qt.io/qt-6/qwidget.html)
