#!/usr/bin/env python3
"""Opt-in native KDE portal/focus test; use dbus-run-session, never the live portal."""
import argparse
import os
from pathlib import Path
import subprocess
import time
import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

parser = argparse.ArgumentParser()
parser.add_argument("--build-dir", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--portal", default="/usr/libexec/xdg-desktop-portal-kde")
parser.add_argument("--host", choices=("portal", "kdialog"), default="portal")
args = parser.parse_args()
assert os.environ.get("DZURKEL_PRIVATE_TEST_BUS") == "1", "Run with a new dbus-run-session and DZURKEL_PRIVATE_TEST_BUS=1"
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

def owns(name):
    return bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
                         GLib.Variant("(s)", (name,)), GLib.VariantType.new("(b)"), Gio.DBusCallFlags.NONE, 1000, None).unpack()[0]

assert not owns("org.freedesktop.impl.portal.desktop.kde"), "Refusing an already-running portal backend"
assert not owns("io.github.EdinUser.Dzurkel.Previewer"), "Refusing an already-running Dzurkel"
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
(root / "files").mkdir()
(root / "files/native #%.txt").write_text("Generated chooser preview fixture\n")
environment = {**os.environ, "QT_QPA_PLATFORM": "wayland", "QT_QPA_PLATFORMTHEME": "kde",
               "QT_PLUGIN_PATH": str(args.build_dir.resolve() / "dzurkel-qt-plugins"),
               "QT_QPA_GENERIC_PLUGINS": f"dzurkelchooser:{args.build_dir.resolve() / 'dzurkel'},dzurkelchooserdriver",
               "DZURKEL_CHOOSER_DRIVER_ROOT": str(root)}
with (root / "portal.log").open("w") as log:
    command = [args.portal] if args.host == "portal" else ["kdialog", "--getopenfilename", str(root / "files") + "/", "--title", "Dzurkel isolated native chooser check"]
    portal = subprocess.Popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT)
    try:
        if args.host == "portal":
            deadline = time.monotonic() + 8
            while not owns("org.freedesktop.impl.portal.desktop.kde"):
                assert portal.poll() is None, "Portal exited; inspect portal.log"
                assert time.monotonic() < deadline, "Portal registration timed out"
                time.sleep(.05)
            response = bus.call_sync("org.freedesktop.impl.portal.desktop.kde", "/org/freedesktop/portal/desktop",
                "org.freedesktop.impl.portal.FileChooser", "OpenFile",
                GLib.Variant("(osssa{sv})", ("/org/dzurkel/test", "", "", "Dzurkel isolated KDE chooser check",
                    {"current_folder": GLib.Variant("ay", os.fsencode(root / "files") + b"\0")})),
                None, Gio.DBusCallFlags.NONE, 18000, None).unpack()
            assert response[0] == 1, f"Chooser must cancel without accepting a file: {response}"
        else:
            assert portal.wait(timeout=18) == 1, "Native chooser must cancel without accepting a file"
        for marker in ("chooser-active", "preview-active", "preview-dismissed", "chooser-focus-returned", "chooser-cancelled"):
            assert (root / marker).exists(), f"Missing {marker}; inspect {root}"
        assert not list(root.glob("error-*")), "Native focus driver reported a failure"
        print(f"Stock KDE {args.host}: Space preview, preview focus, Escape dismissal, chooser focus return and cancellation passed.")
    finally:
        if owns("io.github.EdinUser.Dzurkel.Previewer"):
            bus.call_sync("io.github.EdinUser.Dzurkel.Previewer", "/io/github/EdinUser/Dzurkel/Previewer",
                          "io.github.EdinUser.Dzurkel.Previewer1", "Quit", None, None, Gio.DBusCallFlags.NONE, 1000, None)
        portal.terminate()
        try:
            portal.wait(timeout=3)
        except subprocess.TimeoutExpired:
            portal.kill()
            portal.wait(timeout=3)
