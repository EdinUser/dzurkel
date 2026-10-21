Name:           dzurkel
Version:        1.0.0
Release:        1%{?dist}
Summary:        Quick file previews for Dolphin, Nautilus and Thunar

License:        GPL-3.0-or-later
URL:            https://github.com/EdinUser/dzurkel
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

# Adapters are loaded only when available; do not turn their libraries into a
# mandatory dependency through the ELF dependency generator.
%global __requires_exclude ^(libgio-2\\.0\\.so\\.0|libgobject-2\\.0\\.so\\.0|libglib-2\\.0\\.so\\.0|libKF6SyntaxHighlighting\\.so\\.6|libQt6Multimedia(Widgets)?\\.so\\.6|libKF6WindowSystem\\.so\\.6|libKF6CoreAddons\\.so\\.6|libKF6KIO(Core|FileWidgets)\\.so\\.6).*$

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.22
BuildRequires:  cmake-rpm-macros
BuildRequires:  make
BuildRequires:  ninja-build
BuildRequires:  pkgconfig(libzip) >= 1.7
BuildRequires:  pkgconfig(gio-2.0) >= 2.78
BuildRequires:  qt6-qtbase-devel >= 6.4
BuildRequires:  qt6-qtpdf-devel
BuildRequires:  qt6-qtmultimedia-devel
BuildRequires:  kf6-kio-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kwindowsystem-devel
BuildRequires:  kf6-syntax-highlighting-devel >= 6.0
BuildRequires:  dbus-daemon
BuildRequires:  python3
BuildRequires:  desktop-file-utils
Requires:       qt6-qtwayland
Recommends:     qt6-qtimageformats
Recommends:     kf6-syntax-highlighting >= 6.0
Suggests:       qt6-qtmultimedia
Suggests:       glib2 >= 2.78
Suggests:       kf6-kio-core
Suggests:       kf6-kio-file-widgets
Suggests:       kf6-kwindowsystem
Suggests:       antiword
Suggests:       dolphin >= 26.04
Suggests:       nautilus
Suggests:       thunar
Suggests:       kio-fuse
Suggests:       kio-extras

%description
Dzurkel previews images, PDFs, audio, video, formatted documents, e-books, text and
CSV/TSV/XLSX spreadsheets in a reusable window. It integrates with Dolphin,
Nautilus and Thunar through optional Space
shortcut setup.
Open Dzurkel Settings after installation to configure the shortcut for your user.

%prep
%autosetup

%build
%cmake -DBUILD_TESTING=ON -DDZURKEL_HIGHLIGHTING=ON -DDZURKEL_AUDIO=ON
%cmake_build

%install
%cmake_install
desktop-file-validate %{buildroot}%{_datadir}/applications/dzurkel.desktop
desktop-file-validate %{buildroot}%{_datadir}/applications/dzurkel-settings.desktop

%check
export QT_QPA_PLATFORM=offscreen
export XDG_RUNTIME_DIR=$(mktemp -d)
chmod 700 "$XDG_RUNTIME_DIR"
trap 'rm -rf "$XDG_RUNTIME_DIR"' EXIT
%ctest --output-on-failure

%files
%license LICENSE
%doc README.md
%{_bindir}/dzurkel
%{_libdir}/dzurkel/
%{_datadir}/dzurkel/
%{_datadir}/applications/dzurkel.desktop
%{_datadir}/applications/dzurkel-settings.desktop
%{_datadir}/kio/servicemenus/dzurkel.desktop
%{_datadir}/icons/hicolor/*/apps/dzurkel.png
%{_datadir}/icons/hicolor/scalable/apps/dzurkel.svg
%{_datadir}/icons/hicolor/scalable/apps/dzurkel-symbolic.svg

%changelog
* Wed Oct 21 2026 Dzurkel contributors - 1.0.0-1
- Load text, tables, documents and e-books progressively from direct sources.
- Bound resident portions and add practical Beginning/End access for text files.
- Preserve preview geometry and document formatting during continuation.
- Avoid helper process-wrapper destruction waits during application shutdown.
- Fit video windows to display dimensions while preserving manual resizing.

* Tue Oct 20 2026 Dzurkel contributors - 0.9.0-1
- Add desktop-backed folder contents sizing with optional KIO and GIO adapters.
- Add reversible Thunar folder preview setup and opt-in KDE file-dialog previews.
- Cover media failure/recovery and keep desktop libraries optional at runtime.

* Mon Oct 19 2026 Dzurkel contributors - 0.8.0-1
- Add streaming video with shared audio/video playback and mute/volume choices.
- Add bounded media read-ahead, slow-source feedback and adapter mismatch diagnostics.
- Add remembered text wrapping and Settings OS-compatibility details.

* Mon Oct 19 2026 Dzurkel contributors - 0.7.1-1
- Add optional backends, grouped Settings and missing-library guidance.
- Fix Markdown opening, Contents placement and image reopening.
- Add application icons, compatibility reports and Arch VM tooling.

* Mon Oct 19 2026 Dzurkel contributors - 0.7.0-1
- Add streaming audio and bounded document/e-book previews.
- Add reading typography, portrait defaults and content-only text zoom.

* Sun Oct 18 2026 Dzurkel contributors - 0.6.2-1
- Add reversible Thunar Space/context-menu setup and Settings restart focus.
- Publish runtime installers with a concise checksum manifest.

* Sun Oct 18 2026 Dzurkel contributors - 0.6.1-1
- Add opt-in Nautilus Space previews and GNOME Wayland placement fixes.

* Sat Oct 17 2026 Dzurkel contributors - 0.6.0-1
- Add initial Fedora package with automated tests and Dolphin integration.
