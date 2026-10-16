# Validation

## Environment

Validated on 2026-10-16 with Fedora 44 KDE, Plasma 6.7.5, Dolphin 26.08.1,
Qt 6.11.2, CMake 4.3.0, and GCC 16.2.1. The desktop session uses Wayland.

## Milestone 1 automated checks

Configure and build using the README, then run:

```bash
ctest --test-dir build --output-on-failure
```

Three suites cover:

- Qt Test: 19 checks including setup/cleanup, resource validation, special and
  Unicode filenames, localhost URLs, command parsing, window reuse, and
  Space/Escape dismissal with child focus.
- Isolated D-Bus: six checks including setup/cleanup, absent-instance no-ops,
  headless secondary forwarding, caller-relative paths, visibility-based Toggle,
  resource errors, ten show/hide cycles, eight simultaneous startup contenders,
  graceful Quit, and restart after a killed primary.
- Headless CLI: 12 cases covering help/version, conflicting commands, unexpected
  file arguments, missing arguments/files, and unsupported schemes/options.

D-Bus tests use `dbus-run-session` and Qt's offscreen backend. They require local
Unix sockets; restricted sandboxes may need permission to create a test bus.
Tests never use the desktop's normal session bus. Offscreen dismissal cannot
prove Wayland focus restoration to Dolphin.

## Native Wayland lifecycle and memory

An isolated-bus native Wayland smoke check successfully showed, hid, reopened,
and gracefully quit one resident process. Secondary commands used an invalid
GUI backend to confirm that forwarding did not initialize another GUI.
Diagnostics reported native Wayland and window activation.

One Debug-build sample measured 65,412 KiB RSS while hidden after initial display
and 65,428 KiB after reopening/hiding. These are process RSS readings including
Qt/platform resources, not private heap usage or a sustained leak/performance
benchmark. File decoding has not been implemented yet.

## Dolphin acceptance remains manual

The user-local executable and service menu are installed. Shortcut assignment,
selected-file delivery through Dolphin, keyboard focus restoration, display
scaling, multiple monitors, and themes still require the
[desktop acceptance checklist](dolphin-integration.md). No existing shortcut has
been changed by the implementation.

`desktop-file-validate` flags `MimeType` and `Actions` for KDE's documented
`Type=Service`: its freedesktop schema does not recognize that KDE combination.
The generated file follows the KDE service-menu format; actual discovery and
shortcut invocation must still be checked in Dolphin.

## Earlier Milestone 0

The original two suites passed (17 Qt checks and seven CLI cases), and native
Wayland startup reached an active filename preview. The full Dolphin
Select → Space → Preview → Space/Escape → Return flow has not been recorded as
verified. Milestone 1 changes dismissal from process exit to hiding a resident
window, so repeat desktop acceptance against the updated binary.
