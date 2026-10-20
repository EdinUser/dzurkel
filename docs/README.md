# Dzurkel documentation

Start with the [project overview](../README.md) for capabilities and downloads.
These guides describe existing behavior, implementation boundaries and validation;
an available access route or dependency candidate is not a desktop support claim.

## Getting started

- [Installation, updates and removal](installation.md) — Fedora, Debian and Arch packages, source installation and optional dependencies.
- [Usage and keyboard controls](usage.md) — Settings, playback, navigation and dismissal.
- [Preview demonstration](video/README.md) — video, GIF and recording details using synthetic files.
- [Supported previews and limits](previews.md) — formats, simplified rendering, resource limits and filesystem policy.
- [Runtime baseline and OS compatibility](runtime-baseline.md) — baseline application, optional adapters and build-specific requirements.

## File manager and location integration

- [Dolphin Space previews](dolphin-integration.md) — setup, restoration and desktop acceptance.
- [KDE file-dialog previews](file-dialog-previews.md) — session opt-in, controls, ownership and validation.
- [GNOME and Nautilus](gnome-testing.md) — opt-in setup, provider conflicts, local-file scope and native testing.
- [Thunar](thunar-integration.md) — Space/context-menu setup, restoration and single-file scope.
- [Location compatibility and experimental SFTP](compatibility.md) — mounted paths, direct SMB, opt-in SFTP and evidence gaps.
- [GNOME Wayland placement](gnome-window-placement.md) — compositor behavior, implementation and validation limits.

## Architecture and preview implementation

- [Architecture](architecture.md) — modules, request lifecycle, ownership and desktop boundaries.
- [Preview library and reader boundaries](preview-library.md) — selection, source policies and document/table reader limits.
- [Folder previews](folder-previews.md) — desktop-backed contents size, cancellation and optional KIO/GIO support.
- [Video previews](video-preview.md) — shared audio/video behavior, streaming deadlines, buffering and teardown.
- [Media streaming experiment](media-streaming-experiment.md) — opt-in source probes, historical measurements and remaining validation.
- [D-Bus API](dbus-api.md) — methods, properties, errors and the separate Nautilus contract.
- [Performance and footprint](performance.md) — measurement methods, historical results and their scope.

## Development, validation and releases

- [Contributing](../CONTRIBUTING.md) — development and issue-reporting guidance.
- [Testing evidence](testing.md) — recorded automated/native results and unresolved checks.
- [Arch VM library workflow](arch-vm-testing.md) — separate builder/runtime environments and optional-library recovery.
- [Packaging and release process](releasing.md) — validation gates, assets and prerelease-to-stable promotion.
- [0.9.0 release candidate](releases/v0.9.0.md) — folder contents sizing, Thunar folders and opt-in KDE dialog previews; package validation precedes stable promotion.
- [0.8.0 release notes](releases/v0.8.0.md) — video, shared media controls, downloads and known limits. Validation and stable promotion are complete; check the [GitHub release](https://github.com/EdinUser/dzurkel/releases/tag/v0.8.0) for current status.

## Historical release notes

- [0.7.1](releases/v0.7.1.md) — optional-library recovery, Settings and icons.
- [0.7.0](releases/v0.7.0.md) — audio, formatted documents and e-books.
- [0.6.2](releases/v0.6.2.md) — earlier desktop integration and packaging work.
- [Legacy 0.7.1 notes location](release-notes-0.7.1.md) — redirect retained for existing links.

Historical notes and test measurements retain the version and environment they
describe. They do not automatically establish support or performance for 0.9.0.
