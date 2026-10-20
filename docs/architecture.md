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
| `src/resources/` | URL admission/policy, isolated preparation, desktop remote resolution and bounded snapshots. |
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

## Request and presentation lifecycle

`ResourceAccessPolicy` stores the explicit per-user SFTP experiment preference.
CLI, resident service and resource helper consult it during URL admission;
Settings reads it without writing and persists only explicit checkbox changes.
File/mounted-path and SMB input remain enabled. SFTP uses the existing remote
adapter after opt-in rather than a separate broker or new preview provider.

URL syntax validation happens before accepting a Show request; filesystem
validation does not. A QProcess helper (an internal mode of the same installed
executable) resolves SMB via KIO-FUSE, inspects files and copies document/image
content into an owned, bounded temporary resource. Audio/video are identified here but
not copied. Preparation has a 30-second deadline.
Cancellation kills the helper without a synchronous GUI wait and retains its
directory until exit. No new file is added to the installation manifest.

Decoding runs on
one controller-owned thread pool with one active job and one replaceable pending
job. Each request gets a generation identifier and cancellation flag. A result
can update the UI only if it still belongs to the current requested preview.
Widgets are created and replaced on the GUI thread under Qt parent ownership.

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
A 150 ms fallback maps a loading window if content is still pending. There is
no GUI-thread source-file access or MIME sniffing.

`Visible` includes pending presentation, so Hide/Toggle can cancel it before a
native window exists. Generation checks prevent obsolete results from mapping
the window afterward. See [the D-Bus contract](dbus-api.md).

PDF/table previews initially use 85% of the pointer screen's work area; text and
formatted documents use a separate portrait default. Both remember user
dimensions within the process. Images expose a natural content
size through `ZoomPreview`; the window adds its chrome, applies a 400 × 400
minimum and fits oversized content proportionally within the work area.
Information and image sizes temporarily replace the reading size; they do not
overwrite it. User resizing/zooming remains independent of initial fitting.
Dismissal ends an image session's maximization; a reopened image starts fitted.
The image worker publishes validated, orientation-correct dimensions before
decoding pixels. The controller checks the request generation before using them
to prepare the window size. During navigation, previous content remains visible
until its replacement is ready; no loading-widget flash or activation request
is needed for the already visible preview.

On KDE Wayland, `desktop/WindowPlacement` asks KWin to resize and reposition the
existing frame through its public scripting API. One-shot temporary scripts are
scoped to this process's main window and expected title, unloaded immediately,
and never change activation or persistent compositor settings. File creation and
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
files and viewer widgets are released. Shutdown waits for any native decoder
still finishing its job before releasing application state.

The process remains resident until Quit or session shutdown. Qt/platform caches
and helper threads can remain while hidden. After the first successful PDF,
one empty `QPdfDocument` keeps PDFium initialized; actual preview documents are
still released. Thread rows share process memory, and worker threads may expire
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

`PreviewLibrary` centralizes implemented reader IDs, selection, source-copy policy
and Settings descriptions. `PreviewRegistry` dispatches worker reads and creates
views. CSV and XLSX use separate readers and the same immutable sparse tabular
payload/Qt model-view grid. XLSX uses public libzip APIs for bounded package reads
and Qt XML parsing for saved values and number formats identifying dates/times;
visual styling, charts and formula evaluation are outside its scope. Neither
reader depends on QXlsx, LibreOffice or external
converters. See [the library contract](preview-library.md).

DOCX, RTF, EPUB, FB2, SFB, MOBI and text-only legacy DOC use the same immutable
`FormattedDocument` payload and reflowing `DocumentView`. Worker-side readers
retain basic structure, escape text and generate only supported markup. The
view offers selection/copy, existing zoom and a compact menu of retained
sections. It has no editing or persistence features. DOCX/EPUB use libzip/Qt XML;
RTF uses a small bounded parser and libc character conversion. Legacy DOC uses
optional `antiword` with bounded input/output and cancellation. Document and
widget resource loaders reject filesystem/network fallbacks.

Reading previews (formatted documents, Markdown and plain text) retain their
own portrait-default normal size and window state. PDF/tables retain the
previous geometry separately. Image and compact modes continue borrowing the
active normal geometry through the existing restoration path. Font zoom changes
content only. Markdown uses bounded worker-decoded source and Qt's HTML-disabled
Markdown parser on the GUI thread, followed by shared typography normalization.

The opt-in [media streaming probe](media-streaming-experiment.md) exercises the
same seekable source implementation used by audio and video. The probe is
uninstalled; the normal executable contains its own media helper mode. See
[video previews](video-preview.md) for frame readiness, source deadlines and
buffering policies.
