# GNOME Wayland preview placement

Dzurkel uses Mutter's initial placement for adaptive preview changes on GNOME
Wayland. It retains its Qt window and native Wayland surface, prepares changed
dimensions while hidden, then maps a new top-level role. It does not request
absolute coordinates. The desktop's placement preferences determine the result;
centering was already enabled in the tested GNOME session.

The path requires a native Wayland Qt backend and a GNOME entry in
`XDG_CURRENT_DESKTOP` (including `ubuntu:GNOME`). An available KWin placement
adapter takes precedence. KDE/KWin keeps its existing mapped-window workflow;
other desktops and X11 do not enable GNOME remapping.

## Provider updates and lifecycle

The current preview stays visible while another file loads. Early image metadata
does not resize that old presentation on GNOME. A completed result replaces the
provider synchronously; if sizing or normal/maximized state changes, the window
hides internally before applying the change. Equal-size updates stay mapped.
Internal hiding cancels obsolete resize requests but does not emit dismissal or
cancel the controller's navigation and loading generation.

Cold previews retain the existing 150 ms loading fallback. If that fallback maps
before the result arrives, the final adaptive size uses the same internal remap.
Remembered reading size/state and explicit image maximization while browsing
remain in use. Dismissing an image ends that image session's maximization;
reopening an image starts fitted without changing the saved reading state.
For a normal remap, the initial maximum size temporarily bounds Mutter's first
configure so it cannot automatically maximize a remembered normal reading view.
The bound is released on exposure, allowing ordinary resizing and maximization.

Mutter can focus a new top-level role even when Qt makes no activation request.
Consequently completed GNOME results wait while the preview is inactive or
Settings is visible. The old view remains in place. Returning to the preview
commits the newest result; a new request or dismissal discards the held result.
Only one completed result is retained. The controller reports loading until that
result is presented. Folder-navigation commits do not request activation.

No GNOME Shell extension, compositor script, external window-control executable,
XWayland fallback, or desktop configuration change is required by this path.
Earlier suggestions to use an optional Shell extension are superseded.

## Validation and limits

Use the acceptance sequence in [GNOME testing](gnome-testing.md), testing visible
navigation separately from Escape followed by reopening. Automated offscreen
checks establish lifecycle and sizing behavior, not global placement or focus.
Native GNOME checks cover delayed results, cancellation, equal-size mapping,
retained Qt identity, remembered reading geometry, maximized-document navigation
and explicit image maximization.

The dedicated native CI gate now runs the GNOME regression without allowing a
skip, then exercises real Nautilus input and checks compositor framebuffer
placement on an isolated headless GNOME desktop. See the [native gate command
and coverage](gnome-testing.md#native-gnome-gate). This supplements offscreen
tests with actual Mutter focus/mapping behavior; it does not establish every
hardware/display configuration below.

The development VM is Ubuntu 26.04.1, GNOME Shell 50.1, Qt 6.10.2, one 1280×800
display at 100% scale. Its existing `center-new-windows` and `auto-maximize`
settings are true. Framebuffer measurements are required: native Wayland Qt
positions and frame geometry can be artificial. Initial placement experiments
and the implementation checks do not establish behavior with centering disabled,
multiple monitors, fractional scaling, tiling, or other GNOME versions.

Remapping can trigger Mutter's ordinary mapping animation. Settled placement and
focus do not establish frame-perfect continuity or absence of flicker. Do not
disable global animations to hide this limitation. The older rapid information
fallback/reopen stress case is intermittent on native GNOME; it must not be
reported as fixed by the placement change.
