# Using Dzurkel

## Open Settings

Open **Dzurkel Settings** from the application menu or run `dzurkel --settings`
to configure Dolphin’s Space shortcut without opening a file. Repeated requests
reuse the running service and Settings dialog. Closing Settings leaves the
service resident; use `dzurkel --quit` to exit fully.

## Preview behavior

Dzurkel previews images, PDFs, audio, CSV/TSV/XLSX tables, and plain text from local paths, `file://` URLs,
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

PDF, text and table previews initially fill 85% of the usable screen and retain
your reading size across previews. Images choose a window around their natural
100% size, with a minimum of 400 × 400 logical pixels; smaller pictures stay
centered without enlargement. Large images scale down within the usable desktop,
leaving a small margin. Manual resizing/zooming stays under your control, and a
maximized image window remains maximized. Image and compact information windows
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

Use Ctrl+wheel or `+`/`-` to zoom images and PDFs; `0` or **Fit** restores fit.
Plain wheel scrolls content. Zoom snaps to 10 percentage-point steps, and image
fit does not enlarge small images above 100%. GIFs play with Pause/Play controls.
**Previous**/**Next** and Alt+arrows browse files in natural filename order;
images/audio/information views also accept plain Left/Right. Up/Down browse fitted
images/audio/information views and scroll zoomed images, PDFs and text. See [preview controls and limits](previews.md#zoom-and-file-navigation).

Audio plays automatically with Play/Pause (`P`), a seek bar, volume and mute.
Click either slider to jump to that position and give it keyboard focus; dragging
the handle continues to adjust it normally.
Focused audio sliders use plain arrows to adjust their value; Alt+arrows still
browse files. Space/Escape dismiss and stop playback. Supported decoding depends
on the installed Qt media backend. Damaged or unsupported audio shows a clear
error instead of triggering codec installation.

## Preview library foundation

CSV and TSV now use a read-only spreadsheet grid with literal text values,
quoted/multiline fields, delimiter detection and explicit partial-preview limits.
XLSX uses the same grid with worksheet tabs and stored values only. Formulas are
never calculated; missing saved results stay blank. Formatting and charts are
omitted. See [reader architecture and table behavior](preview-library.md).
