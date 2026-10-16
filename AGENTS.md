# Repository Guidelines

## Project Structure & Module Organization

Dzurkel is a standalone Linux file-preview application targeting Fedora, KDE
Plasma 6, Wayland, and Dolphin 26.04+. Milestone 2 adds bounded image, PDF, and
text previews to single-instance D-Bus control and Dolphin integration. Read
`README.md` for current scope and `docs/dbus-api.md` for IPC. Private
specifications and work notes in `helpers/` are ignored by Git.

Use the specification’s layout: `src/` for application, controller, IPC, preview
providers, resources, and UI modules; `tests/{unit,integration,fixtures}/` for
tests; `integrations/dolphin/` for desktop integration; `docs/` for architecture
and API documentation; and `packaging/fedora/` for packaging.

## Build, Test, and Development Commands

Use C++20, Qt 6 Widgets, and CMake with AUTOMOC. Use Core, Gui, Widgets, DBus,
Concurrent, Pdf, PdfWidgets, and Qt Test. Support GCC and Clang, enable
warnings, and do not fall back to Qt 5.

Run these commands from the repository root:

- `cmake -S . -B build` — configure an out-of-source build.
- `cmake --build build` — compile the application.
- `ctest --test-dir build --output-on-failure` — run registered tests.
- `./build/dzurkel --show "/path/to/file.pdf"` — preview the supplied file.

See `README.md` for dependencies and user-local Dolphin installation. Desktop
acceptance checks are tracked in `docs/testing.md`.

## Coding Style & Naming Conventions

Use descriptive PascalCase class names with matching `.h` and `.cpp` files, e.g.
`PreviewController.h`. Use consistent four-space indentation as an initial
convention; no formatter or linter is configured yet. Prefer public Qt APIs and
small, focused interfaces. Keep preview rendering, window management, IPC, and
file-manager integration separate. The preview engine must not depend on Dolphin
internals or X11-only hooks.

## Testing Guidelines

Use Qt Test alongside functional changes. Cover CLI parsing, URL validation,
provider selection, controller transitions, and invalid resources. Integration
tests should cover single-instance ownership races, forwarding, D-Bus
operations, and lifecycle behavior using an isolated session bus where
practical.

Use descriptive behavior-based test names. No numeric coverage threshold is
specified. Validate focus, dismissal, scaling, themes, and repeated previews in
a real KDE Wayland session; compilation alone cannot verify desktop integration.
Check resource stability and use sanitizers during development.

## Commit & Pull Request Guidelines

Use short imperative commit subjects, e.g. `Add preview provider registry`. Keep
changes focused on specification milestones. PRs should describe behavior, link
relevant issues, report automated and desktop validation, and include
screenshots for UI changes. Record unresolved platform limitations and obtain
explicit requirements before expanding V1 scope.
