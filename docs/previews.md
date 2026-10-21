# File previews

Dzurkel renders content in one reusable window. Space or Escape dismisses the
preview; the process stays resident until `dzurkel --quit`. Mouse-wheel scrolling
works in documents and text. Previewing does not execute or edit file contents.
Version 1.0.0 loads text, tables and supported documents/e-books progressively
from direct sources, without whole-file temporary copies. See the
[release notes](releases/v1.0.0.md) for changes and validation.

The filename lives in the native title bar; navigation and supported zoom
controls share a compact toolbar. The bottom strip shows image dimensions,
text encoding and line count, PDF page counts, CSV/TSV row and column counts,
or the loaded row range and columns of the active XLSX sheet. Text counts describe the bounded preview,
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
for placement. Qt reads image dimensions and orientation in the preparation/image helpers,
but the window waits for decoded pixels before applying that size. An already
mapped preview retains its content, size and filename until the replacement is
ready; content and geometry are committed together with painting suspended during
the update. On KDE Wayland, a small desktop adapter uses
KWin's public scripting API to resize and reposition the existing mapped frame
within the actual work area. Navigation keeps the same native window and does
not request activation. If KWin scripting is unavailable, its visible size is
retained. A 150 ms fallback shows a compact loading window for slow first opens;
it does not impose a delay on fast previews. Initial loading and format selection
remain asynchronous, without GUI-thread source-file reads.

DOC/DOCX, RTF, EPUB, Markdown and plain text have a separate reading geometry:
the first preview uses a 700 × 900 logical-pixel portrait window, scaled down
proportionally to fit within 90% of the available screen. Manual resizing and
maximization are remembered within the resident process, independently of the
PDF and spreadsheet geometry. CSV and Excel grids have their own window size,
with each new table's initial width derived from its column count, fixed default
column widths, row header, scrollbar and window chrome. Width is bounded by a
400-pixel minimum and 85% of the available screen; wider tables scroll horizontally.
Height retains the table geometry, initially 85% of the screen. Manual resizing
and maximization remain independent of reading previews and fitted PDFs. Returning
to the same table through a reading preview preserves its manual size; switching
sheet tabs does not resize the window.
The aspect ratio is not locked. Content zoom never
resizes the window. Images and compact audio/information views keep their
existing sizing and restore the appropriate geometry on return.

## Supported content

| Content | Behavior |
| --- | --- |
| Images | PNG, JPEG, and BMP; additional formats depend on installed Qt image readers. |
| PDF | First page on open, fit-to-width display, page count, and pages rendered on demand. |
| Audio | MP3, FLAC, Ogg/Vorbis, Opus, WAV, AAC, M4A (AAC/ALAC) and audio-only MP4, subject to installed Qt decoding support. |
| Video | MP4/M4V, MKV, WebM, MOV, AVI, MPG/MPEG, OGV and WMV; streamed into an aspect-preserving view, subject to installed Qt decoding support. |
| CSV / TSV | Progressive read-only table grid; literal values, quoted fields and batches of complete records. |
| XLSX | Progressive table grid with sheet tabs, saved values, recognized dates/times and unavailable-result markers; no formula calculation or charts. |
| DOCX / RTF / EPUB | Progressive content previews with basic formatting and readable fallback for unsupported formatting. |
| FB2 / SFB / MOBI 6 | Progressive e-book previews with simplified formatting; only the variants described below are supported. |
| Markdown | Formatted MD/Markdown headings, lists, emphasis, quotes, code and tables; embedded HTML and external resources disabled. |
| Legacy DOC | Text only, using optional `antiword`; file information if the extractor is unavailable. |
| Text | Plain text, logs, JSON, XML, and source files identified as text by Qt; automatic syntax highlighting when the optional backend recognises the format. |
| Other files | Filename, type/MIME, size, modification time, icon, and explanation. |
| Directories | Folder information and optional asynchronous contents size/counts. |
| Links, special objects | Information only; targets are not opened for content. |

Image capability is checked through Qt readers on a bounded header, not just the extension.
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

Images decode in an isolated helper directly from the resolved source. The
existing Qt readers still supply scaling, orientation and GIF frames. The viewer
retains bounded decoded pixels for zoom and playback; it does not reread the
source while painting. Very large dimensions can still exceed a reader's decode
budget even when its source file is small. Each helper has a 30-second deadline,
with at most two image helpers live or awaiting reaping. Cancellation does not
wait for a blocked image plugin or mounted-file read. Source mutation/disappearance
can fail decoding; original file identity stays visible.

Plain text opens in a persistent helper and loads a small initial portion.
Scrolling near the top or bottom requests adjacent content, including previously
evicted content. The view retains three portions and a bounded lookahead/cache;
it does not append indefinitely. Whitespace and Unicode characters are preserved,
and loading/eviction keeps a visible text anchor and the window geometry.
Selection and copying cover the currently resident window. Source changes or
later decode errors leave existing content visible with an explanation.

For text, logs and source files such as SQL exports, **End** (Ctrl+End) opens the
actual final portion directly; **Beginning** (Ctrl+Home) returns to the start.
The status confirms which end was reached. Scroll upward from the final portion
to inspect preceding text. These jumps keep the current language, zoom and window
and reset the text selection. Reopen the preview after a source change.

Lazy loading supports ordinary inspection within resource budgets. Very distant
structured-document navigation may fail with an explanation while retaining useful
content. Fast logical EOF is provided for the text view; compressed document
containers keep their existing navigation facilities. Multiline highlighting
context outside the resident text is best effort, while the language stays fixed.

CSV/TSV share that coordinator and adjacent-window policy. They load small batches
of complete records near the visible boundary, retain three batches and reload
earlier rows when scrolling backward. Absolute row numbers and the range summary
identify the resident source rows. Qt model notifications preserve retained
selection, column widths and scroll anchors. Initial malformed CSV falls back to
progressive text; a later error keeps useful rows visible. Neither path creates
temporary source copies. Original URLs and desktop adapters remain intact.

DOCX, DOC, RTF, FB2, SFB, MOBI and Markdown now load further content as you scroll,
sharing the bounded rich-document window used by EPUB. Three adjacent portions
remain resident, and earlier content reloads when revisited. Formatting and reader
state continue across loads; selection, scroll anchors and geometry are retained.
Later errors keep useful visible content. Parser/metadata limits still apply:
a large indivisible table, list, fence or paragraph can exceed a portion budget,
and distant backward replay can exceed its work budget. Markdown reference lookup
may scan ahead within a bounded work budget before showing reference-bearing text.

Decoded images/GIF frames retain at most 64 MiB in total. The private transfer
also bounds colour profiles to 1 MiB per frame and frame metadata to 2 MiB per
result. Unsupported metadata receives an explanation rather than a partial or
miscoloured image. These are decoded/metadata budgets, not source-file limits.

PDF opens on page one. Its dimensions determine the initial window size; later
pages fit to their own dimensions inside that window. Scroll within a page
normally, then scroll again at the bottom/top to open the next/previous page.
A page that fits entirely can change pages directly. Touchpad momentum cannot
skip multiple pages. The toolbar page number and buttons, or PageUp/PageDown,
also change pages; navigation does not wrap. A requested page shows loading
feedback while retaining the displayed page. If rendering fails later, the
existing page remains visible with an explanation.

PDF search, annotations, editing, printing, and password entry are not supported.
Password-protected, damaged, or unsupported documents show an
explanation with file information instead of an empty preview.

Text is shown in a read-only monospace editor and wraps to the preview width by
default. The toolbar's **Wrap lines** toggle is remembered per user; turning it
off enables horizontal scrolling for long lines. Whitespace and line numbers
describe the initial resident text, not the entire file or visual wrap positions.
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
separator, also remain plain text. Highlighting after eviction may lose lexer
context that began before the resident text window. The language remains fixed;
perfect multiline-context recovery is outside the agreed quick-preview scope.

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
  tooltip explains the rendering budget. Both fit and manual rendering obey
  these bounds for the current page; this is not a total process-memory guarantee.
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
formats remain literal plain text. The isolated helper imports bounded structural
segments through Qt, retaining complete lists, fences and tables rather than
arbitrary character slices. Reference-bearing text can require a bounded
source scan before publication. A segment still has character/line/layout limits;
these are not a permanent source-prefix cutoff. The GUI inserts and formats only
the new bounded portion; explicit zoom/theme changes update all resident text.
Potentially excessive Markdown table layouts conservatively fall back to plain
text before rich-text parsing: more than 256 pipes in a line, or a maximum line pipe
count plus one multiplied by retained line count above 100,000. This estimate
can also simplify pipe-heavy code/prose; its contents remain available as text.

EPUB follows the package's declared reading order and opens into content,
skipping an image-only opening cover. The compact Contents menu includes bounded
spine chapter entries and discovered sections; internal chapter/fragment links
can load unread or evicted content. It is not a full-book search index. Title and author are
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

- Images: no source-file size cap or whole-file copy; a 64 MiB Qt image-allocation budget; decoded previews are bounded to
  4,096 pixels per axis, retained for zoom without reopening the source on the UI thread. Readers without native scaled decoding reject originals
  exceeding the pixel budget before loading. Qt's `QT_IMAGEIO_MAXALLOC`
  environment setting can override its library allocation limit.
- Plain text: nominal 4 KiB source portions with at most 1 KiB boundary lookahead,
  three rendered portions, and a five-entry/384 KiB charged reply cache. Source
  length and long lines have no permanent preview cutoff. Per-request protocol,
  timeout and concurrency budgets still apply. Synchronous in-memory/fallback
  callers retain the earlier 2 MiB/20,000-line/16,384-character limits.
- CSV/TSV: batches target 64 complete rows or 16,384 characters, with 256 columns
  and 16,384 characters per field. Three rendered batches and a five-entry/64 MiB
  charged reply cache remain bounded. Large rows use validated chunked replies
  up to 16 MiB; reverse reads have byte/work budgets. The production path has
  no lifetime 2 MiB or 10,000-row cutoff. Selection covers resident rows.
- XLSX: direct persistent ZIP/XML cursor, up to 64 rows per batch with a soft
  16,384-character target, three rendered batches and the same bounded reply
  cache/transport as CSV. Unselected sheets stay unloaded; switching releases
  resident rows and remembers one region token per sheet. Older uncached ranges
  replay compressed XML within per-request budgets. Metadata/styles/shared strings
  still have eager size/count limits; distant replay can hit a work limit. See
  [the XLSX limits](preview-library.md#xlsx). No complete archive/worksheet copy
  or lifetime worksheet row/cell cutoff is introduced.
- PDF: no application file-size or page-count cap. A separate process opens the
  original resolved source, checks the requested page's dimensions and renders bounded RGBA
  pixels. The GUI displays one page at a time. At most two PDF helpers may be
  live or awaiting reaping; a 30-second request deadline bounds ordinary stalls.
  Cancellation kills the helper without waiting on the GUI thread. This process
  boundary is not a decoder sandbox. PDF sources are not copied in full.
- EPUB: progressive semantic blocks and ordered chapters, with the shared lazy
  coordinator, three rich-text portions, bounded decoded images and reusable Qt
  resource slots. Contents/links can load unread chapters or fragments. Chapter
  XML and individual blocks retain parsing/content budgets; backward misses may
  replay blocks. See [document limits](preview-library.md#formatted-documents-and-e-books).
- DOCX/RTF/DOC/FB2/SFB/MOBI/Markdown: persistent direct-source parsers in the
  shared rich-document helper, three resident portions and a bounded reply cache.
  Loads target 8,192 text units or 64 complete units, with character/block/HTML,
  subtree, image, metadata and per-request read/time budgets. DOCX streams XML
  blocks with style/list state; RTF/FB2/SFB retain parser state; MOBI retains
  record/encoding/markup carry; DOC uses antiword with bounded pipe backpressure.
  Markdown uses Qt structural imports and bounded reference discovery. Earlier
  16 MiB/2 MiB source-prefix limits apply only to compatibility callers, not
  production scrolling. Large indivisible structures and distant replay can
  fail explicitly while keeping useful visible content. See
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

The shared worker retains bounded in-memory/fallback reader entry points; the
production text/table/document paths publish candidates for their persistent helper.
Source-consuming text, image, PDF, table and document readers run in isolated helpers.
Closing/switching stops them without waiting on filesystem/native decoder calls.
These bounds reduce resource use and are not an OS memory sandbox.

## Filesystem policy

Local paths, `file://` URLs and `smb://` URLs are accepted by default; SFTP requires
the [experimental opt-in](compatibility.md#trying-experimental-sftp). Relative paths use
the caller's working directory. Local/mounted filesystem checks run in a
separate resource helper, keeping slow mounts away from the UI thread.
Every symbolic link, including broken/cyclic links, receives information only;
targets are not opened for content. Directory previews can calculate contents
size through optional desktop libraries, without reading file contents;
navigation lists the parent of a selected regular file separately. FIFOs, sockets and
devices are never opened for content. Regular files are opened with
nonblocking/no-follow flags and checked by descriptor.

Plain text, CSV/TSV and all other implemented file readers use helper-backed direct access to the original local or
mounted file, including paths resolved through the existing KIO-FUSE adapter;
authentication and transport caching remain with the desktop/OS. Audio/video
use isolated helper-backed range
reads with no whole-file temporary copy. Unsupported types and exhausted decoding
budgets receive information views. Original metadata
and URLs remain visible. Preparation creates no temporary source files or
directories. Direct access does not promise an immutable snapshot if another
process modifies the source during loading.

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
on KIO-FUSE, so application read/copy limits are not a network-bandwidth guarantee.

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
operation may delay helper process exit despite kill.
Native decoder cancellation/shutdown limitations still apply separately.

Fast information views retain compact first-map sizing. If preparation takes
more than 150 ms, a compact loading window appears and transitions directly to
the completed preview. Navigating an already open preview retains the previous
content and geometry until its replacement is ready.

## Folder contents size

Folder entry metadata is not a contents total. The current source uses an
optional KIO/GIO desktop calculators for asynchronous contents size and counts, with explicit
partial/stopped/unavailable states. See [folder previews](folder-previews.md)
for behavior, dependencies and counting semantics.

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
