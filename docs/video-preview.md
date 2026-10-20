# Video previews

Video uses the same helper-backed streaming source and optional media adapter
as audio. The executable keeps its baseline Qt dependencies; Qt Multimedia and
Multimedia Widgets are loaded through the adapter. The existing build option
`DZURKEL_AUDIO` and installed `libdzurkel_audio.so` filename cover both formats.
The versioned media interface rejects incompatible older audio adapters safely.

## Behavior

Recognized video MIME types and MP4/M4V, MKV, WebM, MOV, AVI, MPG/MPEG, OGV and
WMV names select streaming access. Containers and extensions do not establish
codec availability. The decoder inspects tracks and chooses audio or video
presentation without reopening the source; audio-only MP4 remains supported.
Attached music cover art is not treated as a moving-video track by the tested
Qt FFmpeg backend.

Audio and video autoplay with sound and share the running session's mute and
volume choices. A mute/unmute or volume change in either view carries over to
subsequent audio and video previews, including audio-only MP4. Restarting Dzurkel
resets these session choices. Video fits the available area while preserving
aspect ratio, and offers play/pause (P), a timeline, elapsed/total time, volume and mute. Seeking
is enabled only for a seekable source with known duration. Space/Escape dismiss
the preview. Plain arrows browse files except while a media slider has focus;
Alt+arrows retain existing file navigation behavior. Subtitles and track menus
are outside this initial scope. Video can play without audio or an output device.

Loading remains visible until a valid video frame arrives. A seek remains
pending until a frame near the target arrives; Qt position notifications alone
do not prove completion. Interruptions show buffering feedback based on both
backend state and playback/frame progress.

## Slow sources and lifetime

The existing desktop resource resolver handles local paths, mounted shares and
supported direct SMB/SFTP URLs, preserving the original URL for navigation.
File opening/reading stays in a helper process. The helper keeps a single
512 KiB read-ahead window; IPC responses remain at most 64 KiB. Nearby decoder
requests reuse the window rather than each requiring another filesystem read.
The cache validates file size before reusing data, so truncation invalidates it.
There is no whole-file snapshot or persistent media cache. Underlying mounts
and servers may buffer or download more data; IPC counters are not wire counters.

Current session policies allow 15 seconds for an outstanding source read,
30 seconds for helper startup, 60 seconds for initial media/frame loading,
45 seconds for a pending seek, and 30 seconds without playback progress while
playing. Pausing is not treated as an interruption. A source timeout retires
the session; late/partial IPC replies are never reused. Retry is explicit by
reopening the preview; automatic reconnect/resume is not implemented.

Close/replacement first mutes output and shuts down source IPC, waking pending
decoder reads, then detaches/destroys the player before destroying the device.
Helper termination/reaping is asynchronous, with at most two outstanding source
helpers. Controller generations reject errors and metadata from old previews.
This bounds the application-owned source cache, not decoder/GPU memory or kernel
I/O completion. An uninterruptible filesystem operation can delay helper reaping.

## Validation

`dzurkel_video` covers selection without snapshots, silent video, controls,
actual-frame seeking, audio-only MP4, front/tail MP4 indexes, MKV/WebM,
replacement, stale failures and invalid-media fallback. Playback fixtures need
FFmpeg with MPEG4, AAC and VP8 encoders; unavailable fixture tools are explicitly
skipped. Source/playback suites cover delayed reads, bounded read-ahead,
cancellation, truncation, helper limits and repeated teardown. Abrupt helper
loss during a blocked read must fail promptly, retire that source and permit
explicit reopening through a new source. Timeout tests verify the expired
source cannot resume. Fake KIO-FUSE tests exercise both SMB and SFTP resolution;
they do not reproduce real authentication or network disconnection.
Optional-adapter tests cover absent/broken Multimedia and Multimedia Widgets runtimes.

Read-only personal-file cases are opt-in through `DZURKEL_TEST_VIDEO_LOCAL` and
`DZURKEL_TEST_VIDEO_REMOTE`. Native KDE captures can use
`DZURKEL_VIDEO_NATIVE_SCREENSHOT` with Spectacle installed and the test window
active. Ordinary QWidget snapshots omit the GPU video surface. Personal paths,
logs and screenshots stay outside committed fixtures and documentation.

Mounted CIFS and controlled read-delay experiments are not evidence of real
network-disconnection recovery, cold-cache bandwidth behavior, every codec,
all compositor configurations or hard decoder-resource bounds.
