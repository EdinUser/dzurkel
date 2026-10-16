# File previews

Dzurkel renders content in one reusable window. Space or Escape dismisses the
preview; the process stays resident until `dzurkel --quit`. Mouse-wheel scrolling
works in documents and text. Previewing does not execute or edit file contents.

Information-only views automatically shrink to fit their icon and text. This is
temporary: a supported content preview restores the previous preview dimensions
and maximized state, fitting the current display. Resizing an information window
does not replace the remembered content-preview size. Repeated information views
remain compact, and image decoding still targets the larger preview size.

## Supported content

| Content | Behavior |
| --- | --- |
| Images | PNG, JPEG, and BMP; additional formats depend on installed Qt image readers. |
| PDF | First page on open, fit-to-width display, page count, and multipage scrolling. |
| Text | Plain text, logs, CSV, JSON, XML, and source files identified as text by Qt. |
| Other files | Filename, type/MIME, size, modification time, icon, and explanation. |
| Directories, links, special objects | Information only; contents/targets are not opened. |

Image capability is checked against the actual reader, not just the extension.
Images retain their aspect ratio and supported orientation metadata. Animated
formats show a static frame; animation playback is outside this version's scope.
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

## Resource limits

Limits are configured in the provider source files:

- Images: 64 MiB Qt image-allocation budget; decoded previews are bounded to
  2,048 pixels per axis. Readers without native scaled decoding reject originals
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

## Local filesystem policy

Only local paths and `file://` URLs are accepted. Relative paths use the caller's
working directory. Every symbolic link, including broken/cyclic links, receives
an information view without following its target for content preview. Directories
are not enumerated; FIFOs, sockets, and devices are never read for content.
Regular files are reopened with nonblocking/no-follow flags and checked by file
descriptor, preventing a replaced path from blocking on a FIFO or following a
new link. Missing/unreadable files return CLI or D-Bus errors; decoding failures
after a request is accepted display a fallback in the window.

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
