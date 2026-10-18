# File previews

Dzurkel renders content in one reusable window. Space or Escape dismisses the
preview; the process stays resident until `dzurkel --quit`. Mouse-wheel scrolling
works in documents and text. Previewing does not execute or edit file contents.

The filename lives in the native title bar; navigation and supported zoom
controls share a compact toolbar. The bottom strip shows image dimensions,
text encoding and line count, PDF page counts, CSV/TSV row and column counts,
or the active XLSX sheet's dimensions. Text counts describe the bounded preview,
normalize line endings, and exclude an extra empty paragraph after a final newline.
Spreadsheet zoom
and sheet-creation controls are not shown. File information is available by
hover, click or keyboard activation of the information button. It includes the
name, full original location, size, modification time and format details.
CSV delimiter and other secondary details stay in this interface. The metadata
dialog sizes itself to the content within screen bounds, wraps long locations,
and retains scrolling for larger descriptions.
Warnings remain visible in the footer, with complete explanations in tooltips
and file information. Settings uses a native icon button at the bottom right.

Images size the window around their natural 100% dimensions plus controls.
The minimum is 400 × 400 logical pixels (limited by the available desktop),
so small images remain centered at 100% against the native themed background.
Larger images fit proportionally within the desktop work area, including
window decorations. The compositor may further constrain their size to keep
controls clear of desktop panels. Manual
resize and zoom do not trigger another automatic fit for that file; explicitly
maximized/fullscreen windows retain their state. New images choose their own
natural size. GIF playback controls are included in the size calculation.

Information-only views shrink to fit their icon and text. Information, audio and
image windows temporarily replace the document reading size; returning to PDF,
text or a table restores that size and maximized state. Resizing these temporary
views does not replace the remembered document size. On opening a hidden window,
fast results are fitted before mapping, giving Wayland the initial size it needs
for placement. Image dimensions and orientation are read on the background
worker before pixel decoding. On KDE Wayland, a small desktop adapter uses
KWin's public scripting API to resize and reposition the existing mapped frame
within the actual work area. Navigation keeps the same native window and does
not request activation. If KWin scripting is unavailable, its visible size is
retained. A 150 ms fallback shows a loading window for slow operations;
it does not impose a delay on fast previews. Initial loading and format selection
remain asynchronous, without GUI-thread source-file reads.

## Supported content

| Content | Behavior |
| --- | --- |
| Images | PNG, JPEG, and BMP; additional formats depend on installed Qt image readers. |
| PDF | First page on open, fit-to-width display, page count, and multipage scrolling. |
| Audio | MP3, FLAC, Ogg/Vorbis, Opus, WAV, AAC, M4A (AAC/ALAC) and audio-only MP4, subject to installed Qt decoding support. |
| CSV / TSV | Read-only table grid; literal values, quoted fields and bounded complete records. |
| XLSX | Same table grid with sheet tabs, saved values, recognized dates/times and unavailable-result markers; no formula calculation or charts. |
| Text | Plain text, logs, JSON, XML, and source files identified as text by Qt. |
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

Audio starts playing when its preview opens. Its compact view shows available
cover art, title, artist and album, with filename/icon fallbacks. Play/Pause (`P`),
a seek bar, time, volume and mute are available; seeking is enabled only when the
backend supports it. Volume and mute are remembered within the resident process.
Clicking the seek-bar track jumps directly to the selected time; clicking the
volume track sets that level. Both sliders take keyboard focus on click, so
plain arrows adjust that control rather than browsing files. Dragging a handle
retains the normal slider behavior, and Alt+arrows still browse files.
Space/Shift+Space/Escape close the preview, including when a playback control has
focus. Playback stops on dismissal, replacement or end of track; files do not
automatically advance. Metadata is shown literally, and available codec/bitrate
details appear in the footer and file information.

Audio reads and seeks through an isolated helper without copying the complete
file. Unsupported decoding, invalid media and failed/timed-out reads stop playback
and show file information with an explanation. No codecs are installed, no
transcoding is attempted, and package advice is not guessed. Files with moving
video tracks currently show an explanation instead of playing only their audio.
See the shared [audio/video policy](preview-library.md#audio-and-video-playback-policy).

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
  Plain Left/Right also browse images, audio and information views. Plain Up/Down browse
  fitted images, audio and information views; in manually zoomed images they scroll.
  Focused audio sliders use plain arrows for seeking/volume; Alt+arrows still browse.
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

CSV/TSV use the shared spreadsheet view; regular arrows move between cells,
while Alt+arrows and Previous/Next browse files. Values are never recalculated or
coerced to numbers. See [CSV rules, fallback and limits](preview-library.md#csv--tsv).
XLSX uses the same grid; see [its scope and limits](preview-library.md#xlsx).
Legacy XLS and DOCX are not enabled.

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
- Audio: no whole-file size/copy limit; one outstanding read of at most 64 KiB,
  with a two-second source-read deadline. Opening/loading has a 30-second deadline
  per phase. At most two source helpers may exist while cancellation is reaped.
  Application-thread reads are rejected. Qt controls decoder buffering and
  metadata/artwork decoding; the source budget is not a whole-process memory cap.
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
