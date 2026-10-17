# File previews

Dzurkel renders content in one reusable window. Space or Escape dismisses the
preview; the process stays resident until `dzurkel --quit`. Mouse-wheel scrolling
works in documents and text. Previewing does not execute or edit file contents.

Information-only views automatically shrink to fit their icon and text. This is
temporary: a supported content preview restores the previous preview dimensions
and maximized state, fitting the current display. Resizing an information window
does not replace the remembered content-preview size. Repeated information views
remain compact; image decoding retains bounded detail for zoom.
The icon and metadata appear without manual resizing, including on first open.
The first information-only preview is sized before mapping so Wayland can place
the compact window correctly. Initial provider selection runs asynchronously;
supported files show the normal loading window before decoding completes.
Closing an information view does not make the next PDF preview compact.
On reopening, normal dimensions are restored before the window is shown so
Wayland can place the full preview within the screen.

## Supported content

| Content | Behavior |
| --- | --- |
| Images | PNG, JPEG, and BMP; additional formats depend on installed Qt image readers. |
| PDF | First page on open, fit-to-width display, page count, and multipage scrolling. |
| Text | Plain text, logs, CSV, JSON, XML, and source files identified as text by Qt. |
| Other files | Filename, type/MIME, size, modification time, icon, and explanation. |
| Directories, links, special objects | Information only; contents/targets are not opened. |

Image capability is checked against the actual reader, not just the extension.
Images retain their aspect ratio and supported orientation metadata. Animated GIFs play automatically, with a **Pause/Play** button (`P`) and **Replay**
for animations that finish. Playback follows the file's loop count. Frames decode
on the worker before playback; the GUI only presents retained frames. GIF decoding
is limited to 300 frames, 64 MiB of retained frame pixels, and five seconds of
cooperative decoding work. Exceeding a limit or a later-frame decode failure shows
the first frame with an explanation. One frame can transiently be decoded before
its retained-byte check; native Qt allocation/cancellation limits still apply.
Delays are bounded to 20 ms–60 seconds. Dismissal/replacement releases animation
frames and stops playback. Other animated image formats still show a static frame.
Optional formats such as WebP, GIF, and TIFF require the matching Qt runtime
plugins (Fedora's `qt6-qtimageformats` provides additional readers).

PDF search, annotations, editing, printing, and password entry are not supported.
Password-protected, damaged, oversized, or unsupported documents show an
explanation with file information instead of an empty preview.

Text is always shown in a read-only monospace editor with horizontal and vertical
scrollbars. Markup is displayed literally. UTF-8 is preferred; a byte-order mark
can identify UTF-16/UTF-32. Invalid UTF-8 may use a labeled Latin-1 fallback;
unknown legacy encodings are not reliably detected. Binary data receives an
information view rather than a text dump.

## Zoom and file navigation

- Wheel/touchpad scrolling moves within the current image, PDF, or text.
- Ctrl+wheel, `+`/`=` and `-` zoom images/PDFs in steps of 10 percentage points around the
  viewport center. `0` and the visible **Fit** button reset to fit-to-window for
  images and fit-to-width for PDF. Image fit shrinks large images but never enlarges
  small ones above 100%; deliberate zoom can still enlarge them. The displayed
  percentage reflects actual scale. From 121%, zoom out goes to 120%, 110%, 100%;
  zoom in goes to 130%. Fit percentages can fall between those steps.
- Manual zoom normally ranges from 10% to 800%. PDF manual zoom has a lower
  ceiling when necessary to keep a rendered page within 16 million physical
  pixels and an 8192-pixel edge, accounting for display scaling. The percentage
  tooltip explains that ceiling. Qt manages PDF page caching; this is a per-page
  manual-zoom limit, not a total process-memory guarantee or a new fit-mode limit.
- Image zoom scales the retained decoded pixels. Images larger than the bounded
  decode do not regain full source detail at higher zoom; the percentage tooltip
  explains this. No zoom-sized image bitmap is allocated. Text font zoom is deferred.
- Opening/replacing a file resets zoom to fit; resizing while fitted recomputes fit.
- **Previous**/**Next** and Alt+arrows browse sibling files for every provider.
  Plain Left/Right also browse images and information views. Plain Up/Down browse
  fitted images and information views; in manually zoomed images they scroll.
  Text and PDF keep their normal unmodified arrow behavior. Holding a navigation key does not skip
  through multiple files automatically. Space/Shift+Space/Escape still dismiss.

Sibling order uses case-insensitive natural filename comparison with numeric
ordering and a case-sensitive tie-break. It does not reproduce Dolphin's current
sort. Navigation includes non-hidden regular files and unsupported formats, which
show information views; it excludes directories, links, and special objects.
An explicitly opened hidden file is included as the current ordering anchor.
Navigation does not wrap, and buttons disable at the beginning/end. Browsing
changes Dzurkel’s preview; it does not move Dolphin’s selection.

Directory listing runs in a cancellable helper, using the same local/mounted or
KIO-FUSE route as resource preparation. It reads names and metadata without
opening sibling contents or prefetching. Each listing examines at most 10,000
entries (including excluded entries), allows five seconds of enumeration/sorting
work, limits its response to 2 MiB, and has a 30-second overall deadline. Exceeding
these limits disables navigation with an explanation; no partial order is shown.
At most two listing helpers can exist while cancellation is being reaped, separate
from the two content-preparation helpers. The kernel-stuck I/O limitation applies
to both. Dismissal/replacement cancels listing and discards obsolete results.

Navigation refreshes the listing before choosing the next file, skipping entries
that have disappeared and reporting folder changes. A file can still change after
the listing; normal preview validation then reports the failure. Current directory,
link, and special-object previews have navigation disabled. A listing failure does
not prevent the selected file from being previewed. SMB and opt-in SFTP use the
same ordering and limits; broader real-server acceptance remains separate.

## Resource limits

Limits are configured in the provider source files:

- Images: 64 MiB source-copy limit and 64 MiB Qt image-allocation budget; decoded previews are bounded to
  4,096 pixels per axis, retained for zoom without reopening the source on the UI thread. Readers without native scaled decoding reject originals
  exceeding the pixel budget before loading. Qt's `QT_IMAGEIO_MAXALLOC`
  environment setting can override its library allocation limit.
- Text: at most 2 MiB of source bytes, 20,000 lines, and 16,384 characters per line.
  A visible “Partial preview” notice identifies byte/line truncation or shortened
  lines. A multibyte character cut off at the byte limit is omitted safely.
- PDF: at most 64 MiB and 10,000 pages. File loading happens on a worker;
  `QPdfView` renders and scrolls the accepted document.
- Loading: one active worker and one pending request. New requests replace the
  pending one, and generation checks discard superseded results. Dismissal
  cancels display of unfinished work and releases the visible content.

An already-running native decoder may finish before cancellation takes effect.
On shutdown, Dzurkel waits for that worker before releasing application state.
These limits reduce resource use; in-process Qt decoding is not a security
sandbox. Dedicated decoder isolation remains future work.

## Filesystem policy

Local paths, `file://` URLs and `smb://` URLs are accepted by default; SFTP requires
the [experimental opt-in](compatibility.md#trying-experimental-sftp). Relative paths use
the caller's working directory. Local/mounted filesystem checks run in a
separate resource helper, keeping slow mounts away from the UI thread.
Every symbolic link, including broken/cyclic links, receives information only;
targets are not opened. Directory previews remain metadata-only; navigation lists
the parent of a selected regular file separately. FIFOs, sockets and
devices are never opened for content. Regular files are opened with
nonblocking/no-follow flags and checked by descriptor.

Supported content is copied into a private temporary directory before decoding.
Text copies contain at most 2 MiB plus one byte for truncation detection; image
and PDF copies are limited to 64 MiB. Unsupported types and oversized image/PDF
files receive information views without a content copy. Original metadata and
URLs remain visible. Temporary backing files are released when their resources
are no longer needed; the parent retains a cancelled helper's directory until
that helper exits. This is a bounded copy, not an atomic filesystem snapshot.

Malformed/unsupported URLs return CLI/D-Bus errors. Filesystem, connection and
decoding failures after acceptance display an information view instead. A CLI
exit status of zero acknowledges submission, not successful content rendering.

## SMB resources

Use `smb://server/share/path` (optionally `smb://user@server/share/path`). Passwords
in URLs, explicit ports, queries and fragments are rejected. Authenticate by
opening the location in Dolphin first; use the same username-qualified URL
Dolphin displays when applicable. Kernel CIFS mount credentials are not
assumed to be available to the desktop. Dzurkel does not read mount credential
files, edit `fstab`, install packages or unmount shares.

Direct SMB uses the session-bus `org.kde.KIOFuse.VFS.mountUrl` service. Fedora
requires the optional runtime packages `kio-fuse` and `kio-extras`; failures show
setup/authentication guidance. No KIO development library is required. This route
is available in Qt-only builds too when the service exists; it is not a claim of
GNOME integration. Transport caching and actual network transfer volume depend
on KIO-FUSE, so the snapshot limit is not a network-bandwidth guarantee.

### Experimental SFTP

Settings offers **Experimental SFTP previews**, disabled by default. Enabling it
allows `sftp://[user@]host[:port]/absolute/path` through the same resource helper,
readers and limits; it does not enable other direct protocols. Verify the host key
and authenticate in Dolphin first. New requests use the current preference
without restarting the app. See [compatibility and validation](compatibility.md)
for the evidence gaps, URL rules, controlled-server harness and tester checklist.

Preparation times out after 30 seconds (the mount call can time out earlier).
Hide/replacement kills the helper without waiting on the UI thread. At most two
helpers may exist while a cancelled helper is being reaped; another request gets
a retry explanation if both slots are still occupied. A kernel-stuck filesystem
operation may delay process exit and temporary-directory cleanup despite kill.
Native decoder cancellation/shutdown limitations still apply separately.

Fast information views retain compact first-map sizing. If preparation takes
more than 150 ms, a normal-sized loading window appears; it may shrink when an
information result arrives. File replacement keeps the current loading view
size, with normal preview dimensions restored when reopening a compact window.

## Examples

```bash
dzurkel --show "/path/to/photo.jpg"
dzurkel --show "/path/to/report.pdf"
dzurkel --show "/path/to/source.cpp"
dzurkel --show "/path/to/directory"
dzurkel --hide
dzurkel --quit
```

See [the D-Bus API](dbus-api.md) for preview state inspection and
[desktop integration](dolphin-integration.md) for Dolphin shortcuts.
