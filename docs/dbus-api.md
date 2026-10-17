# Dzurkel D-Bus API (Milestone 3)

Dzurkel owns one service on the user session bus:

- Bus: `io.github.EdinUser.Dzurkel.Previewer`
- Object: `/io/github/EdinUser/Dzurkel/Previewer`
- Interface: `io.github.EdinUser.Dzurkel.Previewer1`

These project identifiers remain provisional until the first public release.

## Methods

| Method | Behavior |
| --- | --- |
| `Show(string url)` | Show or replace the current content preview. |
| `ShowWithOptions(string url, dict options)` | Show with optional launch context. |
| `Hide()` | Hide the window while keeping the service resident. |
| `Toggle(string url)` | Hide a visible window; otherwise show the supplied URL. |
| `ToggleWithOptions(string url, dict options)` | Toggle with optional launch context. |
| `Quit()` | Acknowledge the request and gracefully terminate the service. |

`url` must be a fully encoded absolute `file://` or `smb://` URL. SMB URLs may
include a username, but not a password, explicit port, query or fragment.
Resource arguments are limited to 16,384 characters. A visible Toggle ignores
its unused URL, including invalid URLs. Malformed/unsupported Show requests or
Toggle requests while hidden return an error and preserve the previous state.

A successful reply acknowledges the request before filesystem/network checks.
Missing, unreadable or unavailable resources become an `information` preview
with an explanation; they do not produce a delayed D-Bus error. The original
SMB URL remains in `FileUrl`, never the temporary backing path.

`options` is an `a{sv}` dictionary. The optional `activation-token` string carries
the caller's Wayland launch token. The CLI forwards `XDG_ACTIVATION_TOKEN` through
these additive methods; the original methods remain supported. KDE builds pass
the token to the compositor when presenting the window. Tokens are never logged
or reused when content loading completes. The compositor decides whether to grant
focus. Unknown option keys are ignored; a non-string token or one exceeding
16,384 characters returns `org.freedesktop.DBus.Error.InvalidArgs`. Visible
Toggle ignores its unused options as well as its URL.

Read-only properties are available through standard D-Bus Properties.Get:

- `Visible` (boolean): whether a preview is requested, including loading and
  initial provider selection before the first window maps. Hide/Toggle cancels
  that pending presentation too.
- `FileUrl` (string): the last accepted URL; retained while hidden.
- `Loading` (boolean): whether the current preview is being prepared.
- `PreviewKind` (string): `loading`, `image`, `pdf`, `text`, `information`, or
  `none` (hidden/cleared).

Decoder failure produces an `information` preview with an explanation.
Directory, link, and special-object requests are accepted as metadata-only
previews. Properties do not currently emit change notifications.

## Errors

Errors use the prefix `io.github.EdinUser.Dzurkel.Error.`:

- `InvalidUrl`: malformed/relative URLs, remote file hosts, queries, or fragments.
- `UnsupportedScheme`: schemes other than file and smb.

`NotFound` and `NotReadable` were synchronous errors in earlier development
builds; filesystem failures now appear asynchronously in the information view.

Only URL syntax checks and loading-state setup happen before acknowledgement.
An isolated helper resolves and copies supported content into a bounded local
snapshot, with a 30-second preparation deadline. Image/text/PDF decoding then
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

There is no D-Bus auto-activation file yet. CLI Show/Toggle can create the
primary instance. Hide/Quit against an absent service are successful CLI no-ops.

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
