# Dzurkel D-Bus API (Milestone 2)

Dzurkel owns one service on the user session bus:

- Bus: `io.github.EdinUser.Dzurkel.Previewer`
- Object: `/io/github/EdinUser/Dzurkel/Previewer`
- Interface: `io.github.EdinUser.Dzurkel.Previewer1`

These project identifiers remain provisional until the first public release.

## Methods

| Method | Behavior |
| --- | --- |
| `Show(string url)` | Show or replace the current content preview. |
| `Hide()` | Hide the window while keeping the service resident. |
| `Toggle(string url)` | Hide a visible window; otherwise show the supplied URL. |
| `Quit()` | Acknowledge the request and gracefully terminate the service. |

`url` must be a fully encoded absolute local `file://` URL. A visible Toggle
ignores its unused URL, including invalid URLs. Invalid Show requests or Toggle
requests while hidden return an error and preserve the previous preview/state.

Read-only properties are available through standard D-Bus Properties.Get:

- `Visible` (boolean): whether the window is shown, including its loading state.
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
- `UnsupportedScheme`: non-file URL schemes.
- `NotFound`: the local file does not exist.
- `NotReadable`: permissions or file-open failure.

Only lightweight resource validation and the loading view happen before command
acknowledgement. Image/text/PDF loading runs on a bounded worker queue. New
requests and dismissal invalidate obsolete results. Decoding errors after
acceptance appear as a fallback view, rather than a delayed D-Bus error reply.

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
