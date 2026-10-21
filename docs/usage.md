# Using Dzurkel

## Open Settings

Open **Dzurkel Settings** from the application menu or run `dzurkel --settings`
to configure your supported file manager's Space shortcut without opening a file. Repeated requests
reuse the running service and Settings dialog. Closing Settings leaves the
service resident; use `dzurkel --quit` to exit fully.

Settings has **Integration**, **Previews** and **Libraries** tabs. Previews groups
formats into Audio, Video, Images, Documents, Books, Tables, and Text and source. Each
information button lists supported files and reader limitations. Libraries shows
a dependency list for highlighting, legacy DOC extraction, PDF, archive-based
Office/book formats and audio/video. Status icons use green for available, orange for
limited/unloadable and red for missing, with text labels. Click a status or its
information button for detection details and recovery guidance. Only highlighting
has a usage checkbox; its library status is independent of that preference.
**Refresh status** rechecks optional components and reports whether anything changed.
Known missing readers show an **Open Settings** action directly in the information
preview. Details include a copyable package-manager command for reviewed
Fedora/Debian/Arch combinations where applicable; Dzurkel never runs it.
Missing Dzurkel adapters need a compatible adapter package as well as the runtime
library. Required PDF/libzip components remain part of the useful minimum.
See [runtime requirements and release compatibility](runtime-baseline.md).

## Preview behavior

Dzurkel previews images, PDFs, audio/video, CSV/TSV/XLSX tables, formatted documents,
EPUB/FB2/SFB/MOBI books, Markdown and plain text from local paths, `file://` URLs,
and direct `smb://` URLs through the desktop KIO-FUSE service. Mounted shares
continue to work as filesystem paths. Direct SMB requires desktop authentication;
see [remote resources](previews.md#smb-resources).
SFTP URLs can be enabled through **Experimental SFTP previews** in Settings;
they are off by default; broader server validation remains incomplete. See the
[compatibility matrix and testing guide](compatibility.md).
Unsupported files, directories, links, and special objects get an information
view. One primary process owns the session-bus service and reuses one preview
window. Later CLI invocations forward their commands to that process.

- `dzurkel <file>` or `dzurkel --show <file>` opens or replaces the preview.
- `dzurkel --hide` dismisses it while the service stays resident.
- `dzurkel --toggle <file>` hides a visible preview or opens the supplied file.
- `dzurkel --quit` gracefully stops the resident process.
- `dzurkel --help` and `dzurkel --version` work without starting a GUI.

Space, Shift+Space, Escape, and ordinary window close hide the preview. Hide/Quit with no
running instance succeed without starting one. Relative paths resolve against
the invoking client's directory. Malformed or unsupported URLs leave the current preview unchanged. Missing,
unreadable, or unavailable resources produce an asynchronous information view. Content loads asynchronously, and newer requests supersede unfinished
previews. See [supported formats and limits](previews.md).

Closing the window leaves one process resident for faster reopening. Process
monitors may list its Qt/D-Bus/Wayland threads as separate rows sharing the same
memory. Temporary worker threads can disappear after an idle timeout; this does
not indicate extra application instances. Use `dzurkel --quit` to exit fully.

PDF and table previews initially fill 85% of the usable screen and retain
their size across previews. Documents, EPUB, Markdown and plain text use a
separate portrait default of 700 × 900 logical pixels, scaled down for smaller
screens. Each group remembers manual size/maximization within the resident
process; content zoom never resizes the window. Images choose a window around their natural
100% size, with a minimum of 400 × 400 logical pixels; smaller pictures stay
centered without enlargement. Large images scale down within the usable desktop,
leaving a small margin. Manual resizing/zooming stays under your control, and a
maximized image window remains maximized while browsing. After closing the
preview, opening an image starts fitted again. Image and compact information windows
do not overwrite the remembered document size.

The filename appears once in the native title bar, followed by Dzurkel on KDE.
Compact navigation/zoom controls sit above the content. The bottom strip shows
dimensions, text encoding and line count, table dimensions, or PDF page count.
Hover over or click **File information** for the full location, size, modification
time and format details, including text encoding and CSV delimiter. Partial
previews and unavailable formula results remain visibly flagged.

Click **Settings** at the bottom right of the preview to open desktop setup and
see available preview support. Hover over an information icon or click it for
details. **Enable Space shortcut…** backs up the affected
bindings and reassigns Space; **Restore previous shortcuts** undoes that setup
while preserving unrelated settings. If Dolphin is running, Dzurkel asks to
restart it, waits for all its processes to close normally, applies the change,
and reopens it. If it stays open, no shortcut files are changed. Window and tab
restoration follows Dolphin's startup settings. After Dolphin becomes active,
Dzurkel requests Settings focus once; closing or minimizing Settings cancels
that handoff. The desktop controls whether activation is allowed. Opening
Settings alone changes nothing and needs no sudo.

## Zoom and browsing files

Text, CSV/TSV, XLSX and supported document/e-book previews load more content
near the scrolling boundary. Only adjacent portions remain resident; scrolling
back reloads earlier content within reader budgets. Selection/copy cover resident
content. Later loading errors keep useful content visible with an explanation.
No whole-file temporary copy is made. See [preview scope and limits](previews.md).

For plain text, logs and source code, **End** or Ctrl+End opens the actual final
portion directly, useful for checking the end of an SQL export. **Beginning** or
Ctrl+Home returns to the start. These jumps reset selection while retaining the
language, zoom and window. Structured documents retain their own navigation;
they do not promise direct access to an arbitrarily distant logical end.

Use Ctrl+wheel or `+`/`-` to zoom images and PDFs; `0` or **Fit** restores fit.
For documents, EPUB, Markdown and plain text these controls adjust font size
inside the same window; Fit restores the default text size. Plain text retains
its original whitespace. **Wrap lines** is on by default and remembers your
choice; turn it off to scroll long lines horizontally.
Plain wheel scrolls content. Zoom snaps to 10 percentage-point steps, and image
fit does not enlarge small images above 100%. GIFs play with Pause/Play controls.
**Previous**/**Next** and Alt+arrows browse files in natural filename order;
images/audio/video/information views also accept plain Left/Right. Up/Down browse fitted
images/audio/video/information views and scroll zoomed images, PDFs and text. See [preview controls and limits](previews.md#zoom-and-file-navigation).

Audio and video play automatically with Play/Pause (`P`), a seek bar, volume and
mute. Both start with sound and share your mute/volume choice for the running
session. Video fits the available area without changing its aspect ratio.
Click either slider to jump to that position and give it keyboard focus; dragging
the handle continues to adjust it normally.
Focused media sliders use plain arrows to adjust their value; Alt+arrows still
browse files. Space/Escape dismiss and stop playback. Supported decoding depends
on the installed Qt media backend. Damaged or unsupported media shows a clear
error instead of triggering codec installation. Slow sources show loading,
seeking or buffering feedback; closing/changing the preview cancels playback.
See [video previews](video-preview.md) for source policies and remaining limits.

## Tables

CSV and TSV use a read-only spreadsheet grid with literal text values,
quoted/multiline fields, delimiter detection and progressively loaded row batches.
XLSX uses the same grid with worksheet tabs and stored values only. Formulas are
never calculated; missing saved results show `[Result unavailable]`. Formatting and charts are
omitted. See [reader architecture and table behavior](preview-library.md).

## KDE file-open and upload dialogs

Enable **Space previews in KDE file dialogs** on the Integration tab, then sign
out of Plasma and sign back in. Select one item in a compatible KDE chooser's
file list and press Space. The chooser stays open; dismissing Dzurkel returns
focus, and cancelling the chooser closes only its owned preview. Filename
fields retain ordinary spaces. See [scope and setup](file-dialog-previews.md).

## Folder information

Select one folder and preview it through the CLI or your configured file-manager
action. Contents size and file/subfolder counts update asynchronously when a
desktop calculator is available. **Stop calculating** keeps an explicitly
incomplete count; closing or replacing the preview cancels it. Reopen to refresh.
Existing Thunar setups need **Update folder previews…** in Settings.
See [folder behavior and limits](folder-previews.md).
