# GNOME window placement investigation

Investigated on 2026-10-18, after fixing resident-window resizing. This document
records findings and possible approaches, not a new integration requirement.
No additional extension or window-management application was installed.

## Observed behavior and available interfaces

On Ubuntu 26.04.1, GNOME Shell 50.1 and native Qt Wayland, direct spreadsheet
opening was correctly placed. Growing an already-visible small image preview
into a spreadsheet could leave part of the larger window off-screen. The user
confirmed the separate small-image size restoration fix works.

Both `org.gnome.mutter center-new-windows` and `auto-maximize` were already true.
These are independent preferences. Centering places new windows; it does not
continuously re-center an existing window after content-driven resizing. GNOME's
[setting definitions](https://github.com/GNOME/mutter/blob/gnome-50/data/org.gnome.mutter.gschema.xml.in)
and [placement code](https://github.com/GNOME/mutter/blob/gnome-50/src/core/place.c)
describe initial placement. Dzurkel should respect the user's desktop settings.

A Qt connection-only probe inspected the VM's actual Wayland registry without
mapping a window. It advertised `xdg_wm_base` version 7, GTK shell, activation,
foreign-parenting, toplevel drag and toplevel tag protocols. It did not advertise
`xx_zone_manager_v1`, a layer-shell interface or wlroots foreign-toplevel
management. Merely installing a client tool cannot add these compositor APIs.

Shell D-Bus introspection exposed no built-in general MoveResize operation.
Ubuntu's enabled Tiling Assistant did not export a D-Bus window-control interface
in the installed sources. The Window Calls and Window Control object paths had
no exported interfaces in this session.

## Approaches considered

| Approach | What it provides | Limit for Dzurkel |
| --- | --- | --- |
| Mutter `center-new-windows` | Native placement of new windows | Already enabled; does not solve a mapped preview growing |
| Qt `resize()` and `move()` on native Wayland | Client-size requests | Absolute top-level positions are not generally available |
| Existing GNOME Shell window-control extension | Compositor-side frame movement over D-Bus | Optional extension installation and version validation |
| Qt `xcb` through XWayland | Existing X11 resize and placement path | Changes rendering/backend behavior; requires XWayland |
| Recreate or remap the preview | Lets Mutter perform initial placement again | Potential flicker, focus changes and dismissal/lifecycle regressions |
| Shell `Eval` scripts | Access to Shell internals | Guarded by unsafe mode; unsuitable as a default app dependency |
| Experimental zones protocol | Client-relative explicit placement | Not advertised by the tested compositor |

Qt explicitly documents that top-level positions may be unavailable or
artificial on some platforms; calling `move()` is not proof of movement.
See [QWindow positioning](https://doc.qt.io/qt-6/qwindow.html#position).
The [xdg-shell protocol](https://wayland.app/protocols/xdg-shell) provides
interactive movement rather than an absolute top-level positioning request.

Existing tools do exist. [Window Calls](https://extensions.gnome.org/extension/4724/window-calls/)
lists GNOME 50 support and exports List, Details and MoveResize operations.
Its [source](https://github.com/ickyicky/window-calls/blob/main/extension.js)
uses Mutter's frame APIs, and its move/resize handlers unmaximize windows.
The reviewed main-branch source also contains an apparent undefined-variable
check in `_get_window_by_wid`; validate a specific published artifact before
relying on it. Source review alone does not establish runtime compatibility.

[Window Control](https://extensions.gnome.org/extension/10886/window-control/)
also lists GNOME 45–50 support. Its optional `wctl` executable is unnecessary
for a Qt application: Dzurkel already uses Qt D-Bus. The
[implementation](https://github.com/carlo9890/gnome-window-control/blob/main/window-control%40carlo9890.github.io/extension.js)
exposes geometry operations and refuses maximized/tiled/fullscreen targets;
the [project documentation](https://github.com/carlo9890/gnome-window-control#d-bus-interface)
describes its interface. These are candidates for an opt-in backend, not yet
runtime-tested dependencies. Their general interfaces let session applications
inspect/control windows, a relevant tradeoff when choosing to enable them.

Mutter's [move_resize_frame API](https://gnome.pages.gitlab.gnome.org/mutter/meta/method.Window.move_resize_frame.html)
operates inside the compositor. Linking an ordinary client to a library does
not give it access to the running Shell's Meta.Window objects. GNOME's
[Shell Eval implementation](https://github.com/GNOME/gnome-shell/blob/main/js/ui/shellDBus.js)
checks unsafe mode; Dzurkel should not change that setting.

GNOME Sushi is a useful native comparison. Its current
[window implementation](https://github.com/GNOME/sushi/blob/main/src/core/mainWindow.js)
requests content-based sizes and limits natural content relative to half the
monitor dimensions. That code does not supply an absolute positioning helper
for a different application's mapped window. Conservative sizing reduces the
likelihood of overflow but cannot guarantee placement of a large reading view.

## Direction and validation still needed

Keep normal resizing inside Dzurkel and retain native Wayland as the default.
For precise mapped-window placement, evaluate an existing GNOME D-Bus extension
before considering custom Shell code. This would require an explicit opt-in,
not a mandatory second executable or a new package dependency. An alternative
to evaluate is running only Dzurkel through the existing XWayland backend.

The VM already runs XWayland. An isolated `QT_QPA_PLATFORM=xcb` run passed the
mapped small-image/table regression, including explicit assertions that both
frames stay inside the available desktop area. However, the broader image
sequence failed when restoring a reading window after dismissal (400×400
instead of 740×700). XWayland is therefore a demonstrated positioning route,
not a validated workaround for the complete workflow. No launcher or backend
default was changed. Qt documents the
[xcb backend](https://doc.qt.io/qt-6/linux-requirements.html) as its X11 platform
plugin; using it would also require separate focus and scaling acceptance.

A placement backend must identify Dzurkel's own preview by PID and window
identity, distinguish Settings, use the correct monitor's usable work area,
account for frame/client dimensions, preserve manual maximization and avoid
moving unrelated windows. Geometry requests must be cancelled when the file or
window changes. Verify the resulting frame after the asynchronous request.

Desktop acceptance must cover image↔CSV/XLSX navigation, dismissal/reopening,
focus/Escape, rapid requests, manual movement/resizing, tiling/maximization,
panels/docks, multiple monitors and fractional scaling. No extension behavior
or complete placement acceptance is established by the resize tests.
