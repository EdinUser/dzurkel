# Dzurkel D-Bus API

Dzurkel owns one service on the user session bus:

- Bus: `io.github.EdinUser.Dzurkel.Previewer`
- Object: `/io/github/EdinUser/Dzurkel/Previewer`
- Interface: `io.github.EdinUser.Dzurkel.Previewer1`

These are the project-owned service identifiers; the separate Nautilus contract
is described below.

## Methods

| Method | Behavior |
| --- | --- |
| `Show(string url)` | Show or replace the current content preview. |
| `ShowWithOptions(string url, dict options)` | Show with optional launch context. |
| `Hide()` | Hide the window while keeping the service resident. |
| `Toggle(string url)` | Hide a visible window; otherwise show the supplied URL. |
| `ToggleWithOptions(string url, dict options)` | Toggle with optional launch context. |
| `Quit()` | Acknowledge the request and gracefully terminate the service. |
| `ShowSettings(dict options)` | Open or raise Settings without opening a preview; accepts the same activation option. |
| `StartNautilusPreviewer()` | Claim the Nautilus preview service if its user registration was enabled in Settings. Used by `--nautilus-preview-service` for D-Bus activation; opens no window. |

By default, `url` must be a fully encoded absolute `file://` or `smb://` URL. SMB URLs may
include a username, but not a password, explicit port, query or fragment.
With **Experimental SFTP previews** enabled in Settings, absolute `sftp://` URLs
are also accepted, with an optional username and port (1–65535). Passwords,
queries and fragments remain invalid. The per-user preference is checked on
each new request; a disabled SFTP request returns `UnsupportedScheme` and
preserves the existing preview. Visible Toggle still ignores its unused URL.
Resource arguments are limited to 16,384 characters. A visible Toggle ignores
its unused URL, including invalid URLs. Malformed/unsupported Show requests or
Toggle requests while hidden return an error and preserve the previous state.

A successful reply acknowledges the request before filesystem/network checks.
Missing, unreadable or unavailable resources become an `information` preview
with an explanation; they do not produce a delayed D-Bus error. The original
remote URL remains in `FileUrl`, never the temporary backing path.

`options` is an `a{sv}` dictionary. The optional `activation-token` string carries
the caller's Wayland launch token. The CLI forwards `XDG_ACTIVATION_TOKEN` through
these additive methods; the original methods remain supported. KDE builds pass
the token to the compositor when presenting the window. Tokens are never logged
or reused when content loading completes. The compositor decides whether to grant
focus. Unknown option keys are ignored; a non-string token or one exceeding
16,384 characters returns `org.freedesktop.DBus.Error.InvalidArgs`. Visible
Toggle ignores its unused options as well as its URL.

Read-only properties are available through standard D-Bus Properties.Get:

Settings visibility is independent of the preview properties below. The
`dzurkel --settings` command starts the resident service if needed or forwards
to the existing owner. Repeated requests reuse the Settings dialog. Closing
Settings leaves the service resident; `dzurkel --quit` stops it.

- `Visible` (boolean): whether a preview is requested, including loading and
  initial provider selection before the first window maps. Hide/Toggle cancels
  that pending presentation too.
- `FileUrl` (string): the last accepted URL; retained while hidden.
- `Loading` (boolean): whether the current preview is being prepared.
- `PreviewKind` (string): `loading`, `image`, `pdf`, `text`, `table`, `document`, `audio`, `video`, `information`, or
  `none` (hidden/cleared).

Decoder failure produces an `information` preview with an explanation.
For audio/video, `Loading` covers resource preparation; the media view can still be
loading or buffering playback. A later playback failure changes its kind to
`information` without changing the requested URL.
Directory, link, and special-object requests are accepted as metadata-only
previews. Properties do not currently emit change notifications.

## Errors

Errors use the prefix `io.github.EdinUser.Dzurkel.Error.`:

- `InvalidUrl`: malformed/relative URLs, remote file hosts, queries, or fragments.
- `UnsupportedScheme`: an unimplemented scheme or experimental SFTP while disabled.

`NotFound` and `NotReadable` were synchronous errors in earlier development
builds; filesystem failures now appear asynchronously in the information view.

Only URL syntax checks and loading-state setup happen before acknowledgement.
An isolated helper resolves resources with a 30-second preparation deadline.
Non-media readers use bounded local snapshots or prefixes; audio/video use
helper-backed streaming reads without a whole-file copy. Image/text/PDF decoding then
runs on a bounded worker queue. New requests and dismissal cancel preparation
and invalidate obsolete results. The first window maps after fast provider
selection or a 150 ms loading grace period, whichever comes first.

## Example

Start Dzurkel through the CLI first, then call the bus independently of Dolphin:

```bash
gdbus call --session \
  --dest io.github.EdinUser.Dzurkel.Previewer \
  --object-path /io/github/EdinUser/Dzurkel/Previewer \
  --method io.github.EdinUser.Dzurkel.Previewer1.Hide
```

There is no auto-activation file for the project-owned service. CLI Show/Toggle can create the
primary instance. Hide/Quit against an absent service are successful CLI no-ops.

## Nautilus adapter

Settings can install a reversible user registration for
`org.gnome.NautilusPreviewer`, object `/org/gnome/NautilusPreviewer`, interface
`org.gnome.NautilusPreviewer2`. It implements `ShowFile(s uri, s windowHandle,
b closeIfAlreadyShown, s activationToken)`, `Close()`, read-only `Visible` and
`ParentHandle`, `PropertiesChanged` notifications and `SelectionEvent(u direction)`
(GTK Up=2, Down=3, Left=4, Right=5). The Nautilus 50 contract is separate from the
project-owned API. Requests currently accept local file URLs only.

The generated registration runs `dzurkel --nautilus-preview-service`. This
forwards to an existing Dzurkel instance or starts a resident one without opening
a preview. It requires prior opt-in in Settings. The adapter never replaces or
queues behind an existing owner. It toggles only an already-visible matching
URI; a different URI replaces the content. Navigation delegates to Files rather
than enumerating the folder. Standalone CLI/Dolphin previews retain their own
navigation and do not appear as visible Nautilus previews. The parent handle
identifies the requesting window but is not used to attach the preview.

## Ownership and delivery

The object is exported before requesting the well-known name, with no queueing
or replacement. A startup loser forwards to the existing unique owner and exits.
A five-second forwarding timeout is reported as a failure without automatically
retrying a potentially applied Toggle. An owner disappearing before delivery can
trigger another ownership attempt. The resident process retains one window;
Space, Escape, and ordinary window close dismiss it without ending the service.

[Qt service-name
ownership](https://doc.qt.io/qt-6/qdbusconnectioninterface.html) and [D-Bus
error replies](https://doc.qt.io/qt-6/qdbuscontext.html) describe the public
APIs used here.

## Caller-scoped file chooser requests

`ShowFromChooser(string session, string url, dict options)` shows a preview
owned by the calling connection's unique D-Bus name and the supplied session ID.
Use a new ID for each preview request to distinguish late closure notifications.
IDs contain 1–128 ASCII letters, digits, hyphens or underscores. Options accept
`activation-token` with the same string/length constraints as ShowWithOptions.
Invalid resources preserve the existing preview and return
`io.github.EdinUser.Dzurkel.Error.InvalidChooserResource`; invalid session/options
return `org.freedesktop.DBus.Error.InvalidArgs`.

`HideFromChooser(string session)` hides only that caller's currently owned
preview. A mismatched/stale session is a successful no-op. Ordinary Show/Toggle
or another caller's accepted request replaces ownership. Disconnecting the
owning caller closes its preview; no per-dialog resident services are added.
Chooser previews disable independent file browsing.

`ChooserClosed(string session, string reason, string activationToken)` is sent
only to the owning caller. Reasons include `dismissed`, `hidden`, `closed` and
`superseded`. An activation token is supplied only for dismissal when the same
preview closed and no replacement took over while focus context was pending.
The caller must still check that its chooser is alive/visible and the session
is current before requesting focus. Tokens are never broadcast or logged.

`--serve` starts the normal service without mapping a window and reuses an
existing owner without changing its current preview.
