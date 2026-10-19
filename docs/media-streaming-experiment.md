# Media streaming experiment

The source-access experiment has been integrated into normal audio/video previews.
This opt-in, uninstalled probe remains available to measure the shared source
implementation independently of the application UI. Existing snapshot readers
and file-manager adapters retain their behavior. Video uses the same seekable
access model; [video previews](video-preview.md) describes the integrated view.

Audio and video share the agreed [playback policy](preview-library.md#audio-and-video-playback-policy):
use the installed Qt backend's decoding support, attempt playback, and report
failures without adding decoders or guessing package-installation advice.

## Build and run

Install the usual build dependencies plus the matching Qt 6 Multimedia/Multimedia Widgets
development package (`qt6-qtmultimedia-devel` on Fedora), then:

```bash
cmake -S . -B build-media -DCMAKE_BUILD_TYPE=Debug \
  -DDZURKEL_BUILD_MEDIA_EXPERIMENT=ON
cmake --build build-media --parallel --target \
  dzurkel_media_experiment dzurkel_media_source_tests dzurkel_media_playback_tests
ctest --test-dir build-media -R '^dzurkel_media_' --output-on-failure
./build-media/dzurkel_media_experiment --duration-ms 6000 '/path/to/music.mp3'
```

The probe is muted by default; add `--audible` for listening. It seeks to 30
seconds after playback has advanced for approximately one second, if the media
supports that position. It prints JSON measurements and exits. `--seek-ms` and
`--read-delay-ms` control the target and deterministic source-delay injection.
The tests require process/socket access; playback additionally needs an audio
service. A sandbox that blocks desktop audio can stall PulseAudio startup before
the probe's event loop starts. Use an ordinary desktop terminal for these checks.

Generate synthetic audio for the agreed format matrix (requires `ffmpeg`):

```bash
python3 tests/experiments/run_media_matrix.py \
  --probe build-media/dzurkel_media_experiment \
  --output build-media/media-results \
  --music '/path/to/an/existing.mp3'
```

The optional music input is read-only. Generated fixtures, stderr logs and
`results.json` stay in the chosen output directory. The matrix covers MP3, FLAC,
Ogg/Vorbis (`.ogg` and `.oga`), Opus, PCM WAV, AAC/ALAC M4A, standalone AAC and
audio-only MP4. M4A/MP4 fixtures deliberately retain an end-of-file index.

## Access model

`StreamingMediaSource` presents an open, random-access `QIODevice` to
`QMediaPlayer::setSourceDevice()`. A separate helper resolves admitted URLs using
the existing resource policy and KIO-FUSE adapter, validates a regular file and
keeps a read-only descriptor. No media snapshot or persistent cache is created.

The private Unix socket carries fixed-size, big-endian headers. The helper
reports the initial file size, then accepts `(offset, count)` requests. Each read
is limited to 64 KiB; only one request is outstanding. The helper holds one
64 KiB payload buffer and socket buffers provide backpressure. Seeking changes
the offset of subsequent requests, without transferring intervening bytes.

Opening and URL resolution are asynchronous with a 30-second deadline. Media
reads block the decoder's calling thread on socket I/O, with a two-second
deadline per read transaction. They never read the source filesystem inside the
application process. The experiment counts application-thread reads and treats
them as a failed backend compatibility check.

Cancellation first shuts down the socket, waking a pending decoder read, then
requests helper termination. The player must release its source and be destroyed
before the device is destroyed. `QProcess` ownership transfers to the application
from the outset, allowing asynchronous reaping without a destructor wait on the
GUI thread. At most two helpers may exist; a kernel-stuck helper still counts
against that budget. A source instance is single-use.

The descriptor pins the opened inode through pathname replacement. It does not
freeze file contents. A read beyond newly truncated contents is an error; growth
does not extend the initial reported size. Directories, symbolic links and
special files are rejected. Existing remote admission rules remain in effect,
including opt-in SFTP. Playlists, HTTP streams and live sources are outside this
experiment.

## Verification and interpretation

The source suite checks reads at the beginning and end of a sparse 512 MiB file,
backward seeking, stalled-read deadlines, cancellation responsiveness, exact EOF,
truncation, pathname replacement, missing/special files, helper startup failures,
process limits and descriptor stability over twenty source lifecycles. An
isolated session bus supplies a fake KIO-FUSE service to check SMB resolution and
cancellation while resolution is pending; no file manager is launched or changed.

The playback suite runs eight start/forward-seek/backward-seek/stop cycles in one
process and cancels Qt while its decoder is waiting on a delayed read. It checks
that a 17 MiB WAV starts before the entire source is transferred, that source
reads stay off the application thread, and that helpers are reaped after each
cycle. Playback is muted. The format matrix records startup, bytes transferred,
seek progress, stop latency and event-loop heartbeat gaps.

The initial host is Fedora 44 with Qt 6.11.2 and its FFmpeg backend. All agreed
formats passed playback and forward seeking. Source reads stayed off the
application thread. The final twelve-case matrix (including a real MP3 and
stalled-read cancellation) passed. Playback advanced within 83–204 ms and stopped
within 4 ms; the longest application event-loop heartbeat gap was 13 ms. Every
playback fixture started before the whole source had been read. A delayed initial
read was cancelled without waiting for the injected five-second delay. Both new
suites also passed AddressSanitizer and UndefinedBehaviorSanitizer checks, with
leak detection disabled for external Qt runtime caches. The existing application's
fifteen regression suites passed. These are local observations, not universal
latency guarantees; muted playback checks do not assess audible quality.

Repeated reads can make transferred bytes exceed file size for small files:
probing, index inspection and seeking may revisit the same ranges. The source
does not preload the entire file, but the decoder can choose to inspect large
parts of it. Source payload bounds are not a hard limit on Qt/FFmpeg decoder
memory, metadata, cover-art decoding or operating-system filesystem caches.

## Integration and remaining validation

- Audio routes read failures, timeouts, truncation and invalid media to the same
  stop-and-show-error path. Automatic network recovery is outside scope; real
  disconnection testing remains deferred.
- Validate our source adapter on supported Qt/backend combinations. Unavailable
  codecs should produce an informative error, not require additional decoders
  to be shipped by Dzurkel. This experiment needs no
  Ubuntu or Arch VM; later package and backend checks remain necessary before
  promising support across distributions. The normal build now requires Qt
  Multimedia for integrated audio, independently of the probe option.
- Measure resident decoder memory and metadata/artwork limits. Current tests
  establish source bounds, descriptor/helper cleanup and repeated playback, not
  a strict whole-process memory ceiling.
- Source ownership and cancellation are integrated into the controller's request
  generations. Old audio stops when replacement is requested, even while old
  visual content is retained during loading.
- Separate initial probing, stalled reads and normal long playback in the UI;
  tune deadlines using real-share measurements rather than keeping experiment
  constants without evidence.
- Distinguish embedded cover-art streams from moving video. MP3 files can contain
  an attached image represented as a video stream; a generic video-stream flag
  alone is insufficient to classify a file as a video preview.
- The audio UI, candidate selection, runtime capability reporting, metadata,
  package dependencies and user-facing errors are integrated. Video can reuse
  the source model, but frame delivery, buffering, synchronization and rendering
  still require their own validation.

If another backend reads this device on the application thread or has teardown
behavior incompatible with cancellation, evaluate the loopback HTTP byte-range
adapter before adopting backend-specific workarounds. No private Qt APIs are
used by this experiment.
