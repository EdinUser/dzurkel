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
maximized/fullscreen image windows retain their state while browsing in that
preview session. After dismissal, reopening an image starts fitted again.
New images in a normal window choose their own
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

DOC/DOCX, RTF, EPUB, Markdown and plain text have a separate reading geometry:
the first preview uses a 700 × 900 logical-pixel portrait window, scaled down
proportionally to fit within 90% of the available screen. Manual resizing and
maximization are remembered within the resident process, independently of the
existing PDF/table geometry. The aspect ratio is not locked. Content zoom never
resizes the window. Images and compact audio/information views keep their
existing sizing and restore the appropriate geometry on return.

## Supported content

| Content | Behavior |
| --- | --- |
| Images | PNG, JPEG, and BMP; additional formats depend on installed Qt image readers. |
| PDF | First page on open, fit-to-width display, page count, and multipage scrolling. |
| Audio | MP3, FLAC, Ogg/Vorbis, Opus, WAV, AAC, M4A (AAC/ALAC) and audio-only MP4, subject to installed Qt decoding support. |
| Video | MP4/M4V, MKV, WebM, MOV, AVI, MPG/MPEG, OGV and WMV; streamed into an aspect-preserving view, subject to installed Qt decoding support. |
| CSV / TSV | Read-only table grid; literal values, quoted fields and bounded complete records. |
| XLSX | Same table grid with sheet tabs, saved values, recognized dates/times and unavailable-result markers; no formula calculation or charts. |
| DOCX / RTF / EPUB | Bounded content previews with basic formatting and plain-text fallback for unsupported formatting. |
| FB2 / SFB / MOBI 6 | Bounded e-book previews with simplified formatting; only the variants described below are supported. |
| Markdown | Formatted MD/Markdown headings, lists, emphasis, quotes, code and tables; embedded HTML and external resources disabled. |
| Legacy DOC | Text only, using optional `antiword`; file information if the extractor is unavailable. |
| Text | Plain text, logs, JSON, XML, and source files identified as text by Qt; automatic syntax highlighting when the optional backend recognises the format. |
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
backend supports it. Audio and video autoplay with sound and share volume/mute
choices within the resident process. Dismissal preserves those choices; restarting
Dzurkel resets them.
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
transcoding is attempted, and package advice is not guessed. Moving-video tracks
select the video view; audio-only containers select the music view without
reopening the source. Video uses the same controls and dismissal behavior,
preserves aspect ratio and can play silently without an audio output device.
Loading remains visible until a frame arrives. Slow sources show seeking or
buffering feedback; closing or changing files cancels source access independently
of read deadlines. The helper uses a bounded 512 KiB read-ahead window and
transfers ranges of at most 64 KiB, with no whole-file snapshot. Underlying mounts
may cache more data. See [video behavior and limits](video-preview.md).
See the shared [audio/video policy](preview-library.md#audio-and-video-playback-policy).

PDF search, annotations, editing, printing, and password entry are not supported.
Password-protected, damaged, oversized, or unsupported documents show an
explanation with file information instead of an empty preview.

Text is shown in a read-only monospace editor and wraps to the preview width by
default. The toolbar's **Wrap lines** toggle is remembered per user; turning it
off enables horizontal scrolling for long lines. Whitespace and line numbers
still describe the original bounded text, not the visual wrap positions.
Markup is displayed literally. UTF-8 is preferred; a byte-order mark
can identify UTF-16/UTF-32. Invalid UTF-8 may use a labeled Latin-1 fallback;
unknown legacy encodings are not reliably detected. Binary data receives an
information view rather than a text dump.

Syntax highlighting uses an optional KSyntaxHighlighting 6 adapter. Supported
source/configuration formats are detected from the original filename and MIME
type, including remote filenames. Colours follow the editor's light/dark palette;
the source text, indentation and line breaks are unchanged. No source code is
executed and no automatic pretty-printing is applied. Missing/unloadable adapters,
missing/incompatible libraries and unrecognised formats silently retain plain text.
For responsiveness, previews over 256 Ki UTF-16 code units (including the final
document separator), or with a paragraph over 4096 code units including its
separator, also remain plain text. Existing text loading limits still apply.

Source builds use `-DDZURKEL_HIGHLIGHTING=AUTO` by default: the adapter is built
when KSyntaxHighlighting >= 6.0 development files are available. `ON` requires
those files; `OFF` omits the adapter from the build. The core retains its loader:
a compatible adapter can be installed afterward without rebuilding the executable. With the adapter installed, a compatible
runtime library is detected on the first eligible preview or when opening Settings.
**Settings → Libraries** reports availability and provides a default-on syntax
highlighting toggle. Turning it off keeps subsequent previews plain; existing
previews retain their current appearance. The preference is saved per user.
After installing a missing adapter or runtime library, use **Refresh status** to
retry loading without restarting. A successfully loaded adapter stays loaded
until Dzurkel exits; replacing that backend requires a restart.
Typical runtime packages are `kf6-syntax-highlighting` (Fedora),
`libkf6syntaxhighlighting6` (Debian 13), and `syntax-highlighting` (Arch).
Development packages are respectively `kf6-syntax-highlighting-devel`,
`libkf6syntaxhighlighting-dev`, and `syntax-highlighting`. Availability and Qt
requirements depend on the distribution release; older RHEL releases need their
own compatibility check.

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
  explains this. No zoom-sized image bitmap is allocated. Plain text and formatted
  documents use 50–300% font zoom inside the existing window; Fit restores the
  default font size. Plain text keeps its whitespace and horizontal scrolling.
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
Legacy XLS is not enabled.

## Formatted documents and e-books

DOCX, RTF, Markdown, DRM-free EPUB 2/3, FB2, SFB and supported MOBI 6 books share
a simple reading view. Legacy DOC uses the same view with extracted plain text. Content reflows to
the window; there are no simulated paper pages or page counts. Selection and
copy work, and the existing zoom controls adjust text from 50% to 300%. Fit/`0`
restores the default text size. Plain text has the same font-zoom bounds while
retaining its monospace font and remembered wrapping choice.
Space/Escape dismiss and Alt+arrows browse files as usual.

DOCX preserves ordinary headings, paragraphs, emphasis, simple lists, tables,
notes and bounded embedded raster images. List numbering and style inheritance
are approximate; advanced layouts, merged-cell presentation and floating-object
placement are simplified. Inserted revision text is included; deleted and
explicitly hidden text is omitted. Stored field text is shown without updating
fields. Headers/footers and comment interfaces are omitted.

Titles/subtitles and section headings have distinct sizes and spacing, using
the same reading typography as EPUB/Markdown. Ordinary bullet/decimal lists
use hanging indentation and bounded nesting; unsupported numbering falls back
to bullets. Repeated empty DOCX paragraphs are reduced to one. Explicit RGB
text colors, including simple inherited style colors, are preserved only when
they have readable contrast against the current theme; otherwise normal theme
text is used. The original color is reconsidered when the theme changes.

RTF preserves basic emphasis, Unicode and common legacy code pages. Tables
fall back to text with cell/row separators. Images and embedded objects use
placeholders. Legacy binary DOC uses `antiword` to extract plain text, without
starting an office suite. This optional executable must already be installed;
Dzurkel does not install it. Missing or failed extraction shows file information
with an explanation.

The DOC footer reads **DOC · Text-only preview**; the full formatting explanation
stays in file information. Partial-preview and other warnings remain separate.

Markdown uses Qt's built-in GitHub-style parser for `.md`/`.markdown` files and
recognized Markdown MIME types. Code whitespace is preserved; raw HTML is
disabled and image/file/network resources are not loaded. Other text/source
formats remain literal plain text. A 2 MiB bounded source prefix feeds at most
200,000 UTF-16 text units and 5,000 physical lines into the reading view; stopped
input is labelled partial. Parsing and bounded typography happen on the GUI
thread after worker-side decoding, not through an external converter.
Potentially excessive Markdown table layouts conservatively fall back to plain
text before Qt parsing: more than 256 pipes in a line, or a maximum line pipe
count plus one multiplied by retained line count above 100,000. This estimate
can also simplify pipe-heavy code/prose; its contents remain available as text.

EPUB follows the package's declared reading order and opens into content,
skipping an image-only opening cover. The compact Contents menu lists only
sections retained in the preview, not the entire book. Title and author are
available through file information. Publisher fonts, colors, CSS and exact
pagination are omitted. Fixed-layout content is simplified with a notice;
scripts, remote resources and audio/video overlays are not loaded. Encrypted
books are unsupported. Obfuscated fonts do not prevent text previews because
embedded fonts are not used.

FB2 previews preserve chapter hierarchy, paragraphs and basic emphasis, with
additional note bodies after the main content. Images use placeholders and
tables reduce to readable text. SFB previews support Chitanka's UTF-8 text with
common title, chapter and emphasis markers; unknown formatting remains text.
MOBI previews support unencrypted MOBI 6 with PalmDOC compression or uncompressed
UTF-8/Windows-1252 text. Basic HTML structure is simplified, images use placeholders,
and source links/resources are ignored. HUFF/CDIC-compressed MOBI and standalone
KF8 files show an unsupported-format explanation. AZW/AZW3 and KFX are not enabled.

DRM-protected and encrypted content is not unlocked or converted. When protection
prevents extraction, Dzurkel shows a clear error in the information view. Ordinary
unsupported formatting falls back to readable text where extraction is possible;
unsupported binary compression or encoding shows an explanation instead.

Unsupported formatting wrappers retain their readable descendant text where
possible. Non-text objects get small placeholders. Damaged packages, invalid
markup or unavailable readable content show file information rather than raw
XML, RTF commands or archive bytes. A visible partial-preview notice identifies
content stopped by display limits. There are no editing, export, annotation,
bookmark or reading-progress features.

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
- DOCX/RTF/EPUB/DOC/FB2/SFB/MOBI: complete source snapshots up to 16 MiB; up to 200,000
  retained text characters and 5,000 block boundaries. See
  [document reader bounds](preview-library.md#formatted-documents-and-e-books).
- Audio/video: no whole-file size/copy limit; one outstanding IPC read of at most
  64 KiB and a fixed 512 KiB source read-ahead window. At most two source helpers
  may exist while cancellation is reaped. Application-thread
  reads are rejected. Qt controls decoder buffering and metadata/artwork decoding;
  the source budget is not a whole-process memory cap. See [media lifecycle and
  deadline details](video-preview.md#slow-sources-and-lifetime).
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

Non-media readers use bounded copies in a private temporary directory before
decoding. Text/Markdown/CSV prefixes contain at most 2 MiB plus one byte for
truncation detection; image/PDF copies are limited to 64 MiB, and XLSX/formatted
document/e-book copies to 16 MiB. Audio/video use isolated helper-backed range
reads with no whole-file temporary copy. Unsupported types and oversized complete
snapshots receive information views without a content copy. Original metadata
and URLs remain visible. Temporary backing files are released when their resources
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
