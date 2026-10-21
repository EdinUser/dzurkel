# Architecture

Dzurkel is one resident Qt 6 Widgets process with a reusable preview window.
CLI and session D-Bus are its entry points. Dolphin, Nautilus and Thunar have
separate integration paths; rendering does not depend on file-manager internals.
An optional KDE chooser adapter runs inside compatible Qt application processes
and forwards requests to the same resident service.

## Modules

| Area | Responsibility |
| --- | --- |
| `src/main.cpp` | Application identity, headless command handling, startup and shutdown. |
| `src/application/` | CLI parsing, caller-relative paths, single-instance ownership and forwarding. |
| `src/ipc/` | Public D-Bus methods, options, resource errors and preview-state properties. |
| `src/controller/` | Request generations, bounded worker queue, content replacement and visibility. |
| `src/resources/` | URL admission/policy, isolated preparation, desktop remote resolution and direct-source identity. |
| `src/preview/` | MIME/reader selection, bounded decoding, metadata and GUI-widget construction. |
| `src/ui/` | Preview geometry, dismissal, file information and Settings dialogs. |
| `src/desktop/` | Activation support, Dolphin shortcut backup/restore and explicit restart lifecycle. |
| `src/integrations/kde/` | Optional Qt generic plugin: KDE chooser input, selection, scoped requests and focus return. |
| `integrations/` | Desktop templates and file-manager integration scripts. |

There is no separate application facade class or D-Bus adaptor layer: startup
is in `main.cpp`, and `PreviewService` is explicitly exported on the bus.

## Required runtime and optional adapters

The core links Qt base, Qt PDF/PDF Widgets and libzip. Audio/video, syntax highlighting
and specialised desktop activation are application-relative Qt plugins. The core
uses small interfaces rather than linking their Qt Multimedia or KF6 backends.
Successful modules remain loaded until exit; explicit status refresh retries
failed discovery/load. System packages are never installed by the application.

`PreviewLibrary` describes supported readers independently of installed optional
components. `LibraryAvailability` reports the actual installation and reviewed
package guidance. Missing media, antiword or an additional image reader produces
file information with a component ID and an Open Settings action. Missing
highlighting leaves text usable. Settings has Integration, Previews and Libraries
tabs; only highlighting has a usage checkbox. See [runtime baseline](runtime-baseline.md).

The build generates an offline compatibility report from reviewed distro package
data and ELF requirements. That calculation is independent of runtime capability
checks and is never performed on each end-user machine.

## Reuse requirement for progressive readers

Use OS/desktop facilities, public Qt/libzip APIs and existing extractors before
adding custom infrastructure. Inspect installed capabilities and record a
concrete gap before introducing a replacement. Continue using existing adapters,
readers, decoding and layout engines. Common source access, encoding, helper
lifecycle, request/cache policy and model/view window operations should be shared
across formats; parser-specific continuation remains with each reader. A shared
coordinator does not make a whole-document parser incremental.

## Preview scope and practical file-end access

Progressive direct-source previews provide a useful peek and ordinary scrolling.
Reader, memory and work budgets remain deliberate limits; exhaustive traversal,
full-file visual search and perfect formatting/context after arbitrary distant
jumps are outside this scope. A later budget failure keeps useful content and
shows an explanation. Comparative performance research and broader real-share/
distribution acceptance are separate follow-ups, not claims made by unit tests.

Text/log/code/SQL previews expose Beginning/End controls and Ctrl+Home/Ctrl+End.
The existing text reader resolves its own EOF token using file size and bounded
QFile seeks through TextSource/TextCodec, skipping an empty final nominal interval.
It reads the final useful portion without scanning intervening text. LazyWindow's
reader-neutral jump replaces its adjacent window after the current-generation
reply succeeds; failures retain the prior text. No new transport/index or source
copy is added. The editor, font zoom, selected language and native window persist;
explicit jumps reset the cursor/selection. Scroll back from EOF through ordinary
bounded portions. Reopen when the source changes.

KSyntaxHighlighting retains the language definition for the preview lifetime.
Comment/string/heredoc context that began outside the resident window can be lost;
this is an accepted best-effort colouring limitation, not file-type rediscovery.
No lexer checkpoint/replay implementation is required for preview completion.
Structured readers retain bounded style metadata and best-effort navigation;
fast logical EOF is not promised for compressed/structured documents.

## Request and presentation lifecycle

`ResourceAccessPolicy` stores the explicit per-user SFTP experiment preference.
CLI, resident service and resource helper consult it during URL admission;
Settings reads it without writing and persists only explicit checkbox changes.
File/mounted-path and SMB input remain enabled. SFTP uses the existing remote
adapter after opt-in rather than a separate broker or new preview provider.

URL syntax validation happens before accepting a Show request; filesystem
validation does not. A QProcess helper (an internal mode of the same installed
executable) resolves SMB via KIO-FUSE and inspects files. Plain text, CSV/TSV,
images, PDFs, XLSX and all document/book readers retain the resolved
seekable path and reader metadata for their decoder helper, with no whole-file
copy or application source-size cap. PDFs also have no page-count admission cap. Audio/video are identified here but
not copied. Preparation has a 30-second deadline.
The structured preparation response is capped at 4 MiB and drained incrementally;
original URL/metadata remain separate from the resolved source path.
Cancellation kills the helper without a synchronous GUI wait. Preparation no
longer creates a temporary source directory or snapshot. No new executable is
added to the installation manifest.

Resource preparation, folder navigation and streamed-media source helpers retain
their independent helper budgets and asynchronous completion/reaping during
normal operation. Cancellation does not free a slot until its process wrapper is
destroyed. At application `aboutToQuit`, each surviving wrapper first cancels
and disarms its active request, if any, then kills and detaches a still-running
process wrapper and disconnects its callbacks. The shutdown connection belongs
to the wrapper, so it also covers requests already cancelled or destroyed.
Later owner destruction cannot reparent the detached wrapper. Stopped wrappers
retain ordinary cleanup; live wrappers are deliberately left to operating-system
cleanup at process exit, avoiding a synchronous `QProcess` destructor wait after
the event loop stops. This teardown-only policy matches the decoder helpers;
it cannot force immediate termination of kernel-blocked I/O.

Plain text, CSV/TSV, XLSX and all supported document/book readers use `LazyCoordinator` with a persistent internal helper.
Reader-owned opaque tokens identify reversible portions without a growing offset index.
The coordinator owns one active request, a replaceable foreground target, one
lookahead target, request IDs, stale-publication guards, per-request deadlines,
and an LRU limited by both entries and charged bytes. Shutdown and cancellation
reuse the existing non-waiting process-group kill/reaping pattern. The helper
uses the existing record writer and validated regular-file opener; no custom
source transport is added. `LazyWindow` and `LazyBoundaryWatcher` share adjacent
window eviction, prefetch and boundary input handling between text and tables.
`LazyTextWindow` applies changes to the Qt text document around a live cursor;
`LazyTableWindow` uses the existing `SpreadsheetModel`, Qt row notifications and
persistent indexes to preserve the visible row and selection. Both retain three
portions. Text undo history is disabled. Cache and rendered content are bounded
separately; table portions can exceed a wire record using the existing record
writer's validated chunk framing.

`TextCodec` shares Qt-based encoding detection/conversion, Unicode validation and
newline normalization with bounded/fallback readers. `TextSource` shares the
validated descriptor, mutation checks, byte seeking and safe Unicode boundaries.
`CsvRecordParser` is the extracted existing CSV state machine, used by both the
bounded reader and progressive row batches. CSV backward reads use bounded OS
seeks from known complete-record boundaries rather than a growing index or a new
transport/parser. XLSX uses those same row batches/model operations and retains
one libzip entry/QXmlStreamReader cursor for forward reads. The existing XLSX
package, cell and number-format code serves both progressive and bounded callers.
Only the active sheet retains a three-batch window; other sheets retain names and
one resume token. Switching releases the old window and replaces the foreground
request. Compressed entries cannot use libzip's `zip_fseek`; older cache misses
replay through the same libraries within per-request time/read/expansion budgets.
Metadata, styles and shared strings retain their existing bounded eager parsing.
No whole worksheet buffer, disk extraction or ever-growing row index is added.

EPUB reuses its existing Package/EpubReader and sanitized renderer. A persistent
helper retains only one bounded chapter tree and walks complete semantic blocks;
transparent containers provide continuation without splitting inline runs, lists
or tables. The old total text/128-chapter cutoffs become portion and spine-metadata
budgets. Qt text frames retain three adjacent rich portions; native QTextCursor
edits and layout geometry anchor insertion/eviction. Four reusable image-resource
slots bound the QTextDocument cache rather than adding unique keys forever.
Each inserted frame receives typography and theme-safe source colors at the
current zoom before its scroll anchor is restored; continuation does not rerun
those passes over retained frames. Explicit zoom or palette changes still
update all resident content. Occasional Qt format-history compaction reimports
and formats only the bounded resident window through the same path.
The document record writer and StructuredDecodeJob's document validator also
serve lazy portions, including raw decoded pixels and profiles. No host image
codec, replacement XML parser or second transport is introduced. Chapter and
fragment links use the existing EPUB anchor names as opaque reader jump tokens;
missing targets report a navigation error while retaining useful content.
EPUB chapter XML still
has an 8 MiB/200,000-node tree budget; an oversized indivisible block fails rather
than being silently truncated. This is progressive rendering within bounded
chapters, not arbitrary XML seeking or unlimited chapter parsing.

DOCX streams body blocks and notes through the same `ZipEntryDevice` used by
XLSX and Qt's persistent XML reader. `Context::element` shares bounded Node
construction with the existing package parser. WordReader retains styles,
relationships, numbering counters and heading identity between portions; each
portion renders complete paragraphs/tables/wrappers using the existing renderer.
There is no whole-main-part tree or lifetime main-part byte/character cutoff.
DOCX portions also stop on XML node/attribute/text cost, so invisible markup
cannot force a full 64-block accumulation.
Metadata remains bounded and eager, and a single large table/wrapper is still
indivisible. Older misses replay the same XML/renderer under per-request budgets.

RTF's existing parser now retains group, font/codepage, Unicode fallback and
formatting state over a small byte buffer. Its libc conversion keeps incomplete
multibyte sequences between drains. FB2 streams section boundaries through Qt
and renders complete bounded subtrees through the existing renderer. SFB retains
its line/title/heading state. `DocumentLines` shares TextCodec's encoding decision
and Qt's incremental conversion/newline handling with Markdown and DOC output.
MOBI retains PalmDOC record access/decompression, decoder carry and the existing
sanitized HTML stack, including partial tags/entities crossing records. Only
bounded source buffers, metadata, parser carry and the current portion survive.

DOC reuses the existing QProcess/antiword arguments and inherited source descriptor.
A kernel pipe supplies bounded stdout backpressure while the helper is idle;
QProcess does not accumulate extractor stdout in an unbounded Qt buffer.
Cancellation retains process-group/parent-death handling. Backward replay restarts
antiword against the same open source. Antiword's own internal allocations are
not controlled by the pipe or an OS memory sandbox.

Markdown still uses Qt's HTML-disabled importer, now in the isolated document
helper. Qt exposes complete-string imports, not a resumable Markdown parser.
A conservative blank-line adapter asks Qt's block/list/code formats whether a
boundary is independent; ambiguous lists/fences/indented structures remain whole.
The existing table preflight and table layout normalization are reused. When a
portion contains bracket syntax, a bounded source pass asks the same Qt importer
to resolve candidate reference definitions into a bounded dictionary. This
preserves forward/backward reference links without copying the complete source.
That pass can delay first content and can fail the read/time/metadata budget;
a large indivisible construct also fails explicitly. This is not arbitrary
Markdown slicing or an unlimited parser. Table-limit fallback uses progressive
plain text. Heading navigation and source resources retain the existing policy.

`RichDocumentPortionReader` owns these parsers and their numeric continuation/
replay policy; the coordinator remains format-neutral. Sequential portions target
8,192 text units or 64 units; per-portion 200,000 characters/5,000 blocks/2 MiB HTML
limits remain. XML subtree limits are 200,000 nodes, depth 64 and 4 Mi text/attribute
units. Sequential reads/replay have 64 MiB/five-second budgets. A unit failure
after useful complete content is deferred to the next request. Earlier content
stays visible with an explicit error. Three frames, four image slots, the existing
bounded cache, portion-local formatting and occasional Qt format-history refresh
serve every rich reader. Backward cache misses can exceed bounded replay work;
there is no growing source index. Selection/copy and discovered sections cover resident
content, with EPUB retaining its bounded spine navigation metadata.

Published source sessions stop on replacement/dismissal, including when old content remains visible during a
replacement load. PDF, images and media retain their current lifecycles.

Non-media readers are selected on
one controller-owned thread pool with one active job and one replaceable pending
job. Each request gets a generation identifier and cancellation flag. A result
can update the UI only if it still belongs to the current requested preview.
Widgets are created and replaced on the GUI thread under Qt parent ownership.
Image/PDF/table/document candidates leave that pool before decoding; their first valid helper frame or complete bounded payload
is committed through the same generation and presentation guards.

`StructuredDecodeJob` provides one process lifecycle for bounded XLSX and formatted
document/book callers (`XlsxDecodeJob` is a compatibility alias). Private
`--internal-xlsx` mode uses QCoreApplication; `--internal-document` uses headless
QGuiApplication for embedded raster readers. The two modes share bounded record
writing, timeout, cancellation and asynchronous reaping. At most two structured
helpers may be live/reaping in total. Each helper has its own process group so
cancellation also stops antiword wrappers and descendants.

The existing values-only reader exposes a validated QFile to libzip's public
`SeekableZipSource` callback, also used by DOCX/EPUB. It bounds archive-open reads separately from later
source reads, then charges decompression only for required package parts. The
helper emits sheet/cell records with bounded record/total byte counts. The host
validates geometry, duplicate coordinates and aggregate cell/text budgets before
publishing the immutable table. Cancellation and timeouts kill an app-owned,
asynchronously reaped process; no worker/GUI thread reads a mounted XLSX source.
Document records preserve generated HTML, Markdown, text, sections, metadata,
and bounded native raster pixels/palettes/colour profiles. Compressed images are
decoded only in the child. Synchronous compatibility callers retain their bounded initial-preview contract;
production document sessions use the continuation readers above.
Antiword opens `/proc/self/fd/0` inherited from the validated DOC descriptor;
no private source copy is needed. The helper is an
isolation boundary, not an OS memory sandbox; direct-source mutation can fail.

`ImageDecodeJob` starts the same executable's private `--internal-image` mode
with a headless Qt GUI runtime, so font-using public image plugins also work.
The helper uses the existing image provider and returns bounded raw pixel buffers,
formats, palettes, colour profiles and animation timing. The host preserves the
existing image widget and does not decode compressed image files. Early oriented
dimensions do not resize the current preview. For built-in PNG/JPEG/BMP/GIF
headers, preparation already supplies dimensions using Qt before decoder startup;
font-using or other plugins publish them from the image helper. Completed helpers acknowledge
through stdin EOF and are reaped asynchronously; cancellation kills without a
GUI wait, with the same exit-time detach fallback used for PDF helpers.

The optional media adapter uses `MediaPreviewWidget`, `MediaSession` and Qt
Multimedia/Multimedia Widgets with a seekable
`StreamingMediaSource`. The same installed executable's internal media mode
holds the source descriptor and serves bounded byte ranges over a private Unix
socket. Qt's decoder threads read the device; application-thread reads fail
without waiting on filesystem I/O. Dismissal or replacement mutes the output,
cancels the source, then releases the player before deleting the device. Source
failures and Qt decode errors become information previews guarded by the current
request generation. Existing file-manager integrations are unchanged.

A hidden-window request prepares the target screen and remembered reading
size. Fast worker results are constructed and fitted before mapping, so Wayland
can place small image and information windows using their actual dimensions.
A 150 ms fallback maps a compact loading window if content is still pending. There is
no GUI-thread source-file access or MIME sniffing. Image-format selection gives
Qt plugins a 16 KiB header buffer rather than the full source; known image MIME
types still select their decoder. Actual image decoding retains the direct source.

`Visible` includes pending presentation, so Hide/Toggle can cancel it before a
native window exists. Generation checks prevent obsolete results from mapping
the window afterward. See [the D-Bus contract](dbus-api.md).

Tables derive each new file's initial width from header column geometry and
viewport/window chrome, clamped between 400 logical pixels and 85% of the work
area. Height initially uses 85% of the work area. They retain horizontal scrolling,
manual resizing and stable dimensions across sheet tabs. Text and
formatted documents use a separate portrait default. PDF previews fit their
first page and keep that window size during page navigation. Images and PDFs expose a natural content
size through `ZoomPreview`; the window adds its chrome, applies a 400 × 400
minimum and fits oversized content proportionally within the work area.
Information and image sizes temporarily replace the reading size; they do not
overwrite it. User resizing/zooming remains independent of initial fitting.
Dismissal ends an image session's maximization; a reopened image starts fitted.
The image helper publishes validated, orientation-correct dimensions before
decoding pixels, but the controller applies image geometry only when pixels are
ready. During navigation, previous content, geometry and identity remain visible
until its replacement is ready; no loading-widget flash or activation request
is needed for the already visible preview. Paired window update calls suspend
painting while installing the new identity, widget and geometry.

On KDE Wayland, `desktop/WindowPlacement` asks KWin to resize and reposition the
existing frame through its public scripting API. One-shot temporary scripts are
scoped to this process's main window, unloaded immediately, and never change
activation or persistent compositor settings. The expected caption is preferred;
while a filename change is still travelling over Wayland, the sole eligible
normal window of this process is sufficient. Dialogs, other processes and
ambiguous windows are excluded. Requests made before first exposure retain the
latest target and run after Qt dispatches the native expose event, so a compact
loader cannot permanently replace the committed reading/table size. File creation and
bounded D-Bus calls run off the GUI thread; one active and one latest pending
request limit work during rapid navigation. Obsolete requests are cancelled.
KWin's actual work area and decorations determine proportional image fitting,
centering around the old frame, and edge clamping. No hide/show cycle is used.
If compositor scripting is unavailable, the visible geometry is retained.
Manual resizes and maximized/fullscreen windows do not use this path. The final
compositor configure remains authoritative. See
[Qt's Wayland geometry limitations](https://doc.qt.io/qt-6/application-windows.html#wayland-peculiarities).

The native title bar supplies the filename. A compact toolbar and footer keep
navigation, supported zoom, dimensions and warnings visible. `PreviewData`
supplies file metadata and secondary format details for an escaped information
tooltip and keyboard-accessible dialog. Encoding and delimiter metadata are not
repeated in the footer. The shared table view updates footer dimensions when
switching sheets and uses fixed initial section sizes without whole-sheet scans.

## Dismissal and resident resources

Space, Shift+Space, Escape and ordinary close hide the preview. The controller
cancels unfinished work, clears the pending request, and defers content deletion
until the current input event has finished. Actual images, documents, backing
files and viewer widgets are released. Shutdown waits for native decoders still
using the shared worker pool. PDF parsing and rendering instead run in the
same executable's private `--internal-pdf` mode. `PdfSession` receives bounded
RGBA frames asynchronously, with one active and one replaceable pending request.
It validates frame geometry, request identity and byte counts before publication.
The PDF helper keeps its source descriptor open for the session. Direct-source
metadata can become stale if a file changes after preparation; source-open/read
errors still flow through the existing fallback. Unlike a snapshot, direct access
does not promise stable bytes if another process modifies the file.
Normal child exits are reaped asynchronously. At final application exit, a
still-running killed PDF process is detached to avoid QProcess's waiting
destructor; the OS adopts the child. Real kernel-uninterruptible I/O remains
an external limitation.

The process remains resident until Quit or session shutdown. Qt/platform caches
and helper threads can remain while hidden. PDFium state lives only in PDF
helpers and is released with them. Thread rows share process memory, and worker threads may expire
when idle. See [memory accounting and measurements](performance.md).

## Settings and file-manager boundaries

The preview lazily owns a reusable Settings dialog. Opening it only reads setup
and preview-support status. Its native layout exposes applicable Enable/Restore
actions and accessible tooltip/persistent information controls.

An explicit shortcut operation checks current-user Dolphin processes and their
D-Bus windows. After confirmation it requests normal window quits, waits for
process exit, rechecks for new processes, then edits the per-user shortcut file
atomically with a backup of affected bindings. Cancellation or an unsafe close
leaves the shortcut file unchanged. Reopening follows Dolphin startup settings;
exact tabs/windows are not promised. No forced termination is used.

After successful editing/reopening, a temporary worker waits for an active
Dolphin window, with a ten-second readiness budget. Settings then requests
activation once, using a token captured from the restart action on KDE
Frameworks 6.19+, or Qt's best-effort request otherwise. Closing/minimizing
Settings, destruction or another change cancels the handoff. No always-on-top
flag, retry loop for activation, or resident file-manager monitor is added.

Additional managers need their own verified detection, shortcut, restart and
readiness semantics. They may reuse CLI/D-Bus and the desktop activation layer.

## KDE file-dialog boundary

`ChooserIntegration` manages the explicit, default-off Plasma session opt-in.
Its per-user login script preserves other Qt plugin entries and loads
`dzurkelchooser` through Qt's generic-plugin mechanism. Enable/disable takes
effect after sign-out/in; Settings does not restart applications or the portal.
The plugin contains the KIO/WindowSystem dependencies, so the core can start
without them. A missing or incompatible plugin also leaves ordinary Qt host
startup available. See [build and setup](file-dialog-previews.md).

Inside a visible opening-mode `KFileWidget`, the adapter intercepts unmodified
Space only in the file view with one selected item. It obtains an activation
token asynchronously and rechecks selection, visibility and request generation
before forwarding the original URL. It starts `dzurkel --serve` if necessary;
this uses normal single-instance ownership without opening a preview window.

Each request has a fresh session ID scoped to the caller's unique D-Bus name.
Chooser cancellation or caller disconnect can hide only its owned preview;
replacement by another caller or an ordinary Show/Toggle request transfers
ownership. Chooser previews disable independent file navigation, leaving order
and filtering with the chooser. A targeted close notification returns an
activation token to the originating adapter for focus restoration. Generation
checks prevent delayed notifications from activating an obsolete chooser after
preview replacement. The compositor still controls activation. See
[the caller-scoped D-Bus contract](dbus-api.md#caller-scoped-file-chooser-requests)
and [validation evidence](testing.md).

## Preview library foundation

`PreviewLibrary` centralizes implemented reader IDs, selection, direct-source policy
and Settings descriptions. `PreviewRegistry` dispatches worker reads and creates
views. CSV and XLSX use separate readers and the same immutable sparse tabular
payload/Qt model-view grid. XLSX uses public libzip APIs for bounded package reads
and Qt XML parsing for saved values and number formats identifying dates/times;
visual styling, charts and formula evaluation are outside its scope. Neither
reader depends on QXlsx, LibreOffice or external
converters. See [the library contract](preview-library.md).

DOCX, RTF, EPUB, FB2, SFB, MOBI, Markdown and text-only legacy DOC use the same immutable
`FormattedDocument` payload and reflowing `DocumentView`. Worker-side readers
retain basic structure, escape text and generate only supported markup. The
view offers selection/copy, existing zoom and a compact menu of retained
sections. It has no editing or persistence features. DOCX/EPUB use libzip/Qt XML;
RTF uses a small bounded parser and libc character conversion. Legacy DOC uses
optional `antiword` with bounded output draining, backpressure and cancellation. Document and
widget resource loaders reject filesystem/network fallbacks.

Reading previews (formatted documents, Markdown and plain text) retain their
own portrait-default normal size and window state. Tables retain the
previous general geometry separately; PDF first-page fitting uses that general window state. Image and compact modes continue borrowing the
active normal geometry through the existing restoration path. Font zoom changes
content only. Markdown uses bounded helper-decoded segments and Qt's HTML-disabled importer,
followed by shared table and typography normalization.

The opt-in [media streaming probe](media-streaming-experiment.md) exercises the
same seekable source implementation used by audio and video. The probe is
uninstalled; the normal executable contains its own media helper mode. See
[video previews](video-preview.md) for frame readiness, source deadlines and
buffering policies.

## Folder statistics

Folder previews use an optional FolderSizeBackend interface. The KDE module
wraps KIO::DirectorySizeJob; a second module uses GIO's apparent-size measurement
API. KDE prefers KIO and other desktops prefer GIO, with load-time fallback.
Resource preparation supplies the resolved directory path while retaining the
original URL identity; the backend therefore uses the existing remote access
and authentication route. KIO runs asynchronously; GIO runs on a cancellable
worker with bounded outstanding requests. Polling reads progress without listing
or reading source entries in the GUI thread. Both modules remain optional.

The controller owns the cancellable request and rejects obsolete callbacks.
Progress updates existing labels and shared information text without remapping
or activating the preview. Stop, replacement, dismissal and destruction cancel
the desktop job; a 30-second deadline yields an explicit incomplete result.
No custom scanner, additional helper process or persistent cache is introduced.
See [folder previews](folder-previews.md).
