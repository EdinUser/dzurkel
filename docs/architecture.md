# Architecture

Dzurkel is one resident Qt 6 Widgets process with a reusable preview window.
CLI and session D-Bus are its entry points. Dolphin is the only implemented
file-manager integration; rendering does not depend on Dolphin internals.

## Modules

| Area | Responsibility |
| --- | --- |
| `src/main.cpp` | Application identity, headless command handling, startup and shutdown. |
| `src/application/` | CLI parsing, caller-relative paths, single-instance ownership and forwarding. |
| `src/ipc/` | Public D-Bus methods, options, resource errors and preview-state properties. |
| `src/controller/` | Request generations, bounded worker queue, content replacement and visibility. |
| `src/resources/` | URL admission/policy, isolated preparation, desktop remote resolution and bounded snapshots. |
| `src/preview/` | MIME/reader selection, bounded decoding, metadata and GUI-widget construction. |
| `src/ui/` | Preview geometry, dismissal, Settings link and setup dialog. |
| `src/desktop/` | Activation support, Dolphin shortcut backup/restore and explicit restart lifecycle. |
| `integrations/` | Dolphin service-menu and hidden application-identity desktop templates. |

There is no separate application facade class or D-Bus adaptor layer: startup
is in `main.cpp`, and `PreviewService` is explicitly exported on the bus.

## Request and presentation lifecycle

`ResourceAccessPolicy` stores the explicit per-user SFTP experiment preference.
CLI, resident service and resource helper consult it during URL admission;
Settings reads it without writing and persists only explicit checkbox changes.
File/mounted-path and SMB input remain enabled. SFTP uses the existing remote
adapter after opt-in rather than a separate broker or new preview provider.

URL syntax validation happens before accepting a Show request; filesystem
validation does not. A QProcess helper (an internal mode of the same installed
executable) resolves SMB via KIO-FUSE, inspects files and copies supported content
into an owned, bounded temporary resource. Preparation has a 30-second deadline.
Cancellation kills the helper without a synchronous GUI wait and retains its
directory until exit. No new file is added to the installation manifest.

Decoding runs on
one controller-owned thread pool with one active job and one replaceable pending
job. Each request gets a generation identifier and cancellation flag. A result
can update the UI only if it still belongs to the current requested preview.
Widgets are created and replaced on the GUI thread under Qt parent ownership.

The first request prepares the target screen and normal dimensions while the
window is hidden. Provider selection runs on the worker. Selecting image, PDF
or text queues presentation of the normal loading window before decoding.
An information-only result instead builds and fits its widget before the first
map. This lets Wayland place a compact first window using its actual size.
A 150 ms grace timer maps a loading window if preparation/selection is still
pending. There is no GUI-thread source-file access or MIME sniffing.

`Visible` includes that pending first presentation, so Hide/Toggle can cancel it
before a native window exists. Generation checks prevent obsolete results or
queued selection notifications from mapping the window afterward. See
[the D-Bus contract](dbus-api.md).

Normal previews initially use 85% of the pointer screen's usable dimensions.
Standard decorations allow resize/maximize; user dimensions persist only within
the process. Information views temporarily save and replace the normal size and
state. A subsequent supported preview restores them, including before mapping
a hidden window. The compositor decides final Wayland placement and activation.

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

Future managers need their own verified detection, shortcut, restart and
readiness semantics. They may reuse CLI/D-Bus and the desktop activation layer;
their implementation and generic file-manager discovery remain deferred.
