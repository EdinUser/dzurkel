Name:           dzurkel
Version:        0.6.1
Release:        1%{?dist}
Summary:        Quick file previews for KDE Dolphin

License:        GPL-3.0-or-later
URL:            https://github.com/EdinUser/dzurkel
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.22
BuildRequires:  cmake-rpm-macros
BuildRequires:  make
BuildRequires:  ninja-build
BuildRequires:  pkgconfig(libzip) >= 1.7
BuildRequires:  qt6-qtbase-devel >= 6.4
BuildRequires:  qt6-qtpdf-devel
BuildRequires:  kf6-kwindowsystem-devel
BuildRequires:  dbus-daemon
BuildRequires:  python3
BuildRequires:  desktop-file-utils
Requires:       qt6-qtwayland
Recommends:     qt6-qtimageformats
Suggests:       dolphin >= 26.04
Suggests:       kio-fuse
Suggests:       kio-extras

%description
Dzurkel previews images, PDFs, text and CSV/TSV/XLSX spreadsheets in a reusable
window. It integrates with KDE Dolphin and supports an optional Space shortcut.
Open Dzurkel Settings after installation to configure the shortcut for your user.

%prep
%autosetup

%build
%cmake -DBUILD_TESTING=ON
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
%{_datadir}/applications/dzurkel.desktop
%{_datadir}/applications/dzurkel-settings.desktop
%{_datadir}/kio/servicemenus/dzurkel.desktop

%changelog
* Sun Oct 18 2026 Dzurkel contributors - 0.6.1-1
- Add opt-in Nautilus Space previews and GNOME Wayland placement fixes.

* Sat Oct 17 2026 Dzurkel contributors - 0.6.0-1
- Add initial Fedora package with automated tests and Dolphin integration.
