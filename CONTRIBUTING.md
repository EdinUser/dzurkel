# Contributing to Dzurkel

Dzurkel is a C++20 / Qt 6 previewer developed primarily on Fedora KDE Wayland,
with Dolphin 26.04+, Nautilus and Thunar integration. Keep preview rendering,
resource access, IPC and desktop integration separate. Prefer public Qt/KDE APIs
and four-space indentation.

## Build and test

Follow the [Fedora dependency instructions](docs/installation.md#dependencies),
then run from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Add behavior-based Qt tests alongside functional changes. The ordinary suites
use offscreen rendering and isolated session buses where needed. Real KWin
placement and SFTP checks are optional and require additional setup; see
[testing](docs/testing.md). Never commit personal documents, credentials,
screenshots containing private paths, or the ignored `helpers/` directory.

Reuse OS/desktop facilities, public Qt/libzip APIs and existing readers before
adding parsers, transport or layout infrastructure. Keep continuation state in
the reader and shared lazy coordination format-neutral. Preserve direct-source
access, bounded resident content/cache, cancellation and useful content on later
errors. A quick preview and ordinary scrolling are the scope; unlimited replay,
full-file indexing and perfect lexer context after eviction are not requirements.
See [architecture](docs/architecture.md) and [preview limits](docs/previews.md).

## Report a problem

Include your distribution, desktop, file-manager, Qt and Dzurkel versions,
installation method, reproduction steps, expected result and actual result. A small synthetic sample
is helpful for preview problems. Remove private paths, network credentials and
document contents from logs and screenshots before attaching them.

## Pull requests

Explain the user-visible behavior, include relevant test results, and provide
screenshots for UI changes. Report desktop checks separately from offscreen
tests. Discuss new formats, distributions and expanded scope before implementing
them. Contributions are provided under the project's GPL-3.0-or-later license.
