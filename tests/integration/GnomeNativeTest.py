#!/usr/bin/env python3
"""Real GNOME/Wayland regression gate on a private software-rendered desktop.

Never connects to the user's session bus/display. Requires GNOME Shell/Mutter,
Nautilus, Qt Wayland, Python GI and Pillow, in addition to the build dependencies.
The GNOME 50 input/capture D-Bus interfaces are test-only and version-sensitive.
No Shell.Eval, extensions, compositor scripts or window-position commands.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import traceback
import zlib

PROJECT = 'io.github.EdinUser.Dzurkel.Previewer'
PROJECT_PATH = '/io/github/EdinUser/Dzurkel/Previewer'
PROJECT_IFACE = PROJECT + '1'
PROBE = 'io.github.EdinUser.Dzurkel.GnomeTest'
MARKER = (213, 238, 252)


def png(path, size):
    width, height = size
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'
                     + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress((b'\0' + b'\x24\x67\xa1' * width) * height))
                     + chunk(b'IEND', b''))


def marker_bounds(image):
    from PIL import ImageChops
    red, green, blue = image.convert('RGB').split()
    mask = ImageChops.multiply(red.point(lambda v: 255 if v == MARKER[0] else 0),
                              green.point(lambda v: 255 if v == MARKER[1] else 0))
    mask = ImageChops.multiply(mask, blue.point(lambda v: 255 if v == MARKER[2] else 0))
    bounds = mask.getbbox()
    if bounds is None or mask.histogram()[255] < 1000:
        raise AssertionError('No rendered preview marker; capture/placement cannot be treated as passed')
    return list(bounds)


def assert_contained(bounds, screen):
    x, y, right, bottom = bounds
    if not (0 <= x < right <= screen[0] and 0 <= y < bottom <= screen[1]):
        raise AssertionError(f'Preview outside framebuffer: {bounds}, screen={screen}')


def assert_centered(bounds, baseline, screen):
    assert_contained(bounds, screen)
    x, y, right, bottom = bounds
    center = ((x + right) / 2, (y + bottom) / 2)
    if baseline is not None and any(abs(a - b) > 3 for a, b in zip(center, baseline)):
        raise AssertionError(f'Adaptive navigation displaced rendered center: {center}, initial={baseline}')
    if baseline is None and (abs(center[0] - screen[0] / 2) > 3 or abs(center[1] - screen[1] / 2) > 45):
        raise AssertionError(f'Initial preview did not use centered placement: {center}')
    return center


def unmount_test_filesystems(root):
    # Some desktop services mount FUSE views even for a local-only test. Never
    # inspect/unmount a user's runtime mounts: only this newly created root.
    prefix = str(root / 'runtime') + '/'
    for line in Path('/proc/self/mountinfo').read_text().splitlines():
        fields = line.split()
        filesystem = fields[fields.index('-') + 1]
        target = fields[4]
        if not filesystem.startswith('fuse') or not target.startswith(prefix):
            continue
        helper = shutil.which('fusermount3') or shutil.which('fusermount')
        if not helper:
            raise RuntimeError('Cannot clean the isolated FUSE mount: ' + target)
        subprocess.run([helper, '-u', '-z', target], check=True, timeout=5,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)


class Session:
    def __init__(self, args):
        from gi.repository import Gio, GLib
        self.Gio, self.GLib = Gio, GLib
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        self.args = args
        self.root = Path(os.environ['DZURKEL_GNOME_TEST_ROOT'])
        self.report = Path(args.report_dir)
        self.records, self.processes, self.logs = [], [], []
        self.input_path = None
        self.isolated = False
        self.versions = {}

    def call(self, name, path, interface, method, signature=None, values=(), timeout=5000):
        params = self.GLib.Variant(signature, values) if signature else None
        return self.bus.call_sync(name, path, interface, method, params, None,
                                  self.Gio.DBusCallFlags.NONE, timeout, None).unpack()

    def wait(self, description, predicate, seconds=12):
        deadline = time.monotonic() + seconds
        last = None
        while time.monotonic() < deadline:
            try:
                value = predicate()
                if value:
                    return value
            except self.GLib.Error as error:
                last = str(error)
            time.sleep(.08)
        raise AssertionError(f'Timed out: {description}; last D-Bus error={last}')

    def spawn(self, name, command):
        log = open(self.report / f'{name}.log', 'w')
        self.logs.append(log)
        process = subprocess.Popen(command, stdout=log, stderr=log)
        self.processes.append(process)
        return process

    def owner(self, name):
        return self.call('org.freedesktop.DBus', '/org/freedesktop/DBus',
                         'org.freedesktop.DBus', 'NameHasOwner', '(s)', (name,))[0]

    def properties(self):
        return self.call(PROJECT, PROJECT_PATH, 'org.freedesktop.DBus.Properties',
                         'GetAll', '(s)', (PROJECT_IFACE,))[0]

    def snapshot(self):
        return json.loads(self.call(PROBE, '/GnomeTest', PROBE + '1', 'Snapshot')[0])

    def key(self, *codes):
        # Native evdev input delivered by Mutter to whichever window has focus.
        # These are not Qt-synthesized application events or window-control tools.
        try:
            for code in codes:
                self.call('org.gnome.Mutter.RemoteDesktop', self.input_path,
                          'org.gnome.Mutter.RemoteDesktop.Session', 'NotifyKeyboardKeycode', '(ub)', (code, True))
            time.sleep(.06)
        finally:
            for code in reversed(codes):
                self.call('org.gnome.Mutter.RemoteDesktop', self.input_path,
                          'org.gnome.Mutter.RemoteDesktop.Session', 'NotifyKeyboardKeycode', '(ub)', (code, False))
        time.sleep(.25)

    def capture(self, name):
        from PIL import Image
        path = self.report / f'{name}.png'
        success, actual = self.call('org.gnome.Shell.Screenshot', '/org/gnome/Shell/Screenshot',
                                    'org.gnome.Shell.Screenshot', 'Screenshot', '(bbs)',
                                    (False, False, str(path)))
        if not success or Path(actual) != path:
            raise AssertionError('GNOME framebuffer capture failed')
        return Image.open(path).convert('RGB')

    def ready(self, filename, probe=True):
        target = (self.fixtures / filename).as_uri()
        kind = 'image' if filename.endswith('.png') else 'table' if filename.endswith('.csv') else 'text'
        last_state = None
        def finished():
            nonlocal last_state
            state = self.snapshot() if probe else self.properties()
            last_state = state
            if probe:
                return state if (state['visible'] and not state['loading'] and state['file'] == target
                                 and state['active'] and state['kind'] == kind) else None
            return state if (state['Visible'] and not state['Loading'] and state['FileUrl'] == target
                             and state['PreviewKind'] == kind) else None
        try:
            return self.wait('active ready preview for ' + filename, finished)
        except AssertionError as error:
            raise AssertionError(f'{error}; last state={last_state}') from error

    def checkpoint(self, name, centered=True):
        state = self.snapshot()
        if not state['gnome'] or not state['active'] or not state['mapped'] or state['loading']:
            raise AssertionError(f'Expected native focused settled preview: {state}')
        # Wait for Mutter's normal animation to settle; do not disable animations.
        time.sleep(.6)
        frame = self.capture(name)
        bounds = marker_bounds(frame)
        assert_contained(bounds, frame.size)
        if centered:
            center = assert_centered(bounds, self.center, frame.size)
            if self.center is None:
                self.center = center
        if state['maximum'] != [16777215, 16777215]:
            raise AssertionError('Initial remap size bound was not released')
        self.records.append({'step': name, 'state': state, 'renderedBounds': bounds})
        self.save()
        return state

    def save(self, error=None):
        result = {'status': 'failed' if error else 'running', 'versions': self.versions, 'checks': self.records}
        if error:
            result['error'] = str(error)
        (self.report / 'report.json').write_text(json.dumps(result, indent=2) + '\n')

    def setup(self):
        # Worker only: refuses to operate on an inherited user display or bus.
        if (Path(os.environ['HOME']) != self.root / 'home'
                or Path(os.environ['XDG_RUNTIME_DIR']) != self.root / 'runtime'
                or os.environ.get('DBUS_SYSTEM_BUS_ADDRESS') != 'unix:path=' + str(self.root / 'runtime/system-bus')
                or os.environ.get('WAYLAND_DISPLAY') != 'dzurkel-test'
                or not (self.root / 'isolated-session').is_file()):
            raise RuntimeError('Refusing native input/capture outside the isolated test session')
        if os.getuid() == 0:
            raise RuntimeError('Run native tests as an ordinary user')
        self.isolated = True
        # Shell creates system-bus proxies even in headless mode. An ordinary
        # CI container has no system bus; provide an empty private one rather
        # than connecting to host logind, power, account or display services.
        self.spawn('system-bus', ['dbus-daemon', '--session', '--nofork',
                                  '--address=' + os.environ['DBUS_SYSTEM_BUS_ADDRESS']])
        self.wait('private system bus', lambda: (self.root / 'runtime/system-bus').is_socket(), seconds=5)
        self.versions['gnome-shell'] = subprocess.check_output(['gnome-shell', '--version'], text=True, timeout=10).strip()
        self.Gio.Settings.new('org.gnome.shell').set_string(
            'welcome-dialog-last-shown-version', self.versions['gnome-shell'].split()[-1])
        settings = [('org.gnome.mutter', 'center-new-windows', True),
                    ('org.gnome.desktop.screensaver', 'lock-enabled', False)]
        for schema, key, value in settings:
            self.Gio.Settings.new(schema).set_boolean(key, value)
        self.Gio.Settings.new('org.gnome.desktop.session').set_uint('idle-delay', 0)
        self.Gio.Settings.sync()
        self.shell = self.spawn('gnome-shell', ['gnome-shell', '--headless', '--wayland', '--no-x11',
                                               '--force-animations', '--wayland-display', 'dzurkel-test',
                                               '--virtual-monitor', '1280x800'])
        self.input_path = self.wait('Mutter remote input', lambda: self.call(
            'org.gnome.Mutter.RemoteDesktop', '/org/gnome/Mutter/RemoteDesktop',
            'org.gnome.Mutter.RemoteDesktop', 'CreateSession')[0], seconds=30)
        self.call('org.gnome.Mutter.RemoteDesktop', self.input_path, 'org.gnome.Mutter.RemoteDesktop.Session', 'Start')
        # Test fixture for Shell's screenshot sender allowlist, on this private
        # bus only. Do not enable unsafe mode or execute Shell.Eval scripts.
        self.call('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                  'RequestName', '(su)', ('org.gnome.SettingsDaemon.MediaKeys', 4))
        self.wait('GNOME Shell startup', lambda: self.owner('org.gnome.Shell.Screenshot'), seconds=30)
        time.sleep(2)
        self.call('org.gnome.Shell', '/org/gnome/Shell', 'org.freedesktop.DBus.Properties', 'Set', '(ssv)',
                  ('org.gnome.Shell', 'OverviewActive', self.GLib.Variant('b', False)))
        self.key(42)  # Create the virtual keyboard before Qt negotiates its seat.
        self.versions['nautilus'] = subprocess.check_output(['nautilus', '--version'], text=True, timeout=10).strip()
        self.capture('desktop-startup')
        self.fixtures = self.root / 'fixtures'
        self.fixtures.mkdir()
        for name, size in [('a-small.png', (40, 30)), ('b-same.png', (40, 30)),
                           ('c-wide.png', (800, 420)), ('e-small.png', (40, 30))]:
            png(self.fixtures / name, size)
        (self.fixtures / 'd-table.csv').write_text('Name,Value\nFirst,42\nSecond,84\n')
        (self.fixtures / 'f-spaces # % Пример.txt').write_text('Synthetic GNOME keyboard and cold-start test.\n')

    def run(self):
        self.setup()
        # The formerly skipped native regression is mandatory in this job.
        native = self.spawn('native-qt-regression', [self.args.preview_tests, 'gnomePresentationKeepsPendingContentAndDismissal'])
        if native.wait(timeout=35) != 0:
            raise AssertionError('Native GNOME Qt regression failed; see native-qt-regression.log')
        log = (self.report / 'native-qt-regression.log').read_text()
        if 'SKIP' in log or '0 skipped' not in log:
            raise AssertionError('Native Qt regression skipped instead of exercising GNOME')
        self.records.append({'step': 'mandatory-native-qt-regression', 'passed': True})
        self.probe = self.spawn('probe-setup', [self.args.probe, self.args.binary])
        self.wait('test probe', lambda: self.owner(PROBE))
        error = self.call(PROBE, '/GnomeTest', PROBE + '1', 'Enable')[0]
        if error:
            raise AssertionError(error)
        self.call(PROJECT, PROJECT_PATH, PROJECT_IFACE, 'Quit')
        self.probe.wait(timeout=5)
        self.wait('setup provider exit', lambda: not self.owner(PROJECT))
        self.files = self.spawn('nautilus', ['nautilus', '--new-window', str(self.fixtures)])
        self.wait('Files startup', lambda: self.owner('org.gnome.Nautilus'))
        time.sleep(2)
        if self.owner(PROJECT) and self.properties()['Visible']:
            raise AssertionError('Files startup Ping unexpectedly opened a preview')
        self.key(102)  # Home selects first sorted file in Files.
        self.key(57)   # Real Space; actual production executable cold-activates.
        self.ready('a-small.png', probe=False)
        self.capture('production-space-cold-start')
        self.key(1)
        self.wait('production Escape dismissal', lambda: not self.properties()['Visible'])
        self.call(PROJECT, PROJECT_PATH, PROJECT_IFACE, 'Quit')
        self.wait('production provider exit', lambda: not self.owner(PROJECT))
        # Files caches the preview proxy. Space must restart the absent provider.
        self.key(57)
        self.ready('a-small.png', probe=False)
        self.key(57)
        self.wait('production Space dismissal', lambda: not self.properties()['Visible'])
        self.call(PROJECT, PROJECT_PATH, PROJECT_IFACE, 'Quit')
        self.wait('cold restart provider exit', lambda: not self.owner(PROJECT))
        self.records.append({'step': 'real-production-space-cold-restart-and-dismissal', 'passed': True})
        self.probe = self.spawn('probe-observer', [self.args.probe, self.args.binary])
        self.wait('observing probe', lambda: self.owner(PROBE))
        self.key(57)
        self.ready('a-small.png')
        self.center = None
        first = self.checkpoint('small-first')
        self.key(56, 15)  # Alt+Tab -> Files, not an application requestActivate.
        self.wait('Files has focus', lambda: not self.snapshot()['active'])
        self.key(106)     # Files selects b-same.png and delivers ShowFile.
        target = (self.fixtures / 'b-same.png').as_uri()
        self.wait('background selection accepted', lambda: self.snapshot()['file'] == target)
        time.sleep(.8)
        held = self.snapshot()
        if held['active'] or not held['loading'] or held['title'] != 'a-small.png' or held['maps'] != first['maps']:
            raise AssertionError(f'Background update stole focus or changed presentation: {held}')
        self.capture('background-files-keeps-focus')
        self.records.append({'step': 'background-result-held', 'state': held})
        self.key(56, 15)
        self.ready('b-same.png')
        same = self.checkpoint('same-size-return')
        if same['maps'] != first['maps']:
            raise AssertionError('Equal-size update remapped the preview')
        self.key(56, 106)  # Real adapter SelectionEvent -> Files -> ShowFile.
        self.ready('c-wide.png')
        wide = self.checkpoint('wide-centered')
        if wide['maps'] != same['maps'] + 1 or wide['qtWindow'] != first['qtWindow']:
            raise AssertionError('Changed size failed to remap once while retaining the Qt window')
        self.key(56, 106)
        self.ready('d-table.csv')
        if not self.snapshot()['maximized']:
            self.key(56, 68)  # Alt+F10: explicit native document maximization.
        self.wait('maximized CSV', lambda: self.snapshot()['maximized'])
        self.checkpoint('maximized-table', centered=False)
        self.key(56, 106)
        self.ready('e-small.png')
        small = self.checkpoint('document-to-small-centered')
        if small['maximized'] or small['client'] != first['client']:
            raise AssertionError('Maximized document size leaked into the small image')
        self.key(56, 105)
        self.ready('d-table.csv')
        if not self.snapshot()['maximized']:
            raise AssertionError('Document maximization was not restored')
        self.key(56, 106)
        self.ready('e-small.png')
        self.key(56, 106)
        self.ready('f-spaces # % Пример.txt')
        self.key(1)
        self.wait('probe Escape dismissal', lambda: not self.snapshot()['visible'])
        self.key(57)
        self.ready('f-spaces # % Пример.txt')
        self.key(56, 15)
        self.wait('Files refocused', lambda: not self.snapshot()['active'])
        self.key(57)
        self.wait('same-URI Space toggle from Files', lambda: not self.snapshot()['visible'])
        error = self.call(PROBE, '/GnomeTest', PROBE + '1', 'Restore')[0]
        if error or self.owner('org.gnome.NautilusPreviewer'):
            raise AssertionError('Restore failed to release preview ownership: ' + error)
        if ((self.root / 'data/dbus-1/services/org.gnome.NautilusPreviewer.service').exists()
                or (self.root / 'state/dzurkel/nautilus-preview-backup.json').exists()):
            raise AssertionError('Restore left the temporary preview registration or backup behind')
        self.records.append({'step': 'real-navigation-special-filename-dismissal-toggle-and-restore', 'passed': True})
        self.save()
        result = json.loads((self.report / 'report.json').read_text())
        result['status'] = 'passed'
        (self.report / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
        print(f'{len(self.records)} native GNOME checks passed; report: {self.report}', flush=True)

    def close(self):
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for log in self.logs:
            log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=lambda p: str(Path(p).resolve(strict=True)))
    parser.add_argument('--probe', required=True, type=lambda p: str(Path(p).resolve(strict=True)))
    parser.add_argument('--preview-tests', required=True, type=lambda p: str(Path(p).resolve(strict=True)))
    parser.add_argument('--report-dir', required=True, type=lambda p: str(Path(p).resolve()))
    parser.add_argument('--inside-session', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    Path(args.report_dir).mkdir(parents=True, exist_ok=True)
    if args.inside_session:
        session = Session(args)
        try:
            session.run()
            return 0
        except Exception as error:
            session.save(error)
            if session.isolated:
                try:
                    session.capture('failure')
                except Exception:
                    pass
            traceback.print_exc()
            return 1
        finally:
            session.close()
    with tempfile.TemporaryDirectory(prefix='dzurkel-gnome-native-') as directory:
        (Path(args.report_dir) / 'report.json').write_text(json.dumps({'status': 'starting', 'checks': []}) + '\n')
        root = Path(directory)
        for child in ('home', 'runtime', 'config', 'data', 'state', 'cache'):
            (root / child).mkdir(mode=0o700)
        (root / 'isolated-session').touch()
        env = os.environ.copy()
        for key in ('DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'DBUS_STARTER_ADDRESS', 'DBUS_STARTER_BUS_TYPE',
                    'XDG_ACTIVATION_TOKEN', 'DESKTOP_STARTUP_ID', 'DZURKEL_TEST_KWIN_BUS_ADDRESS',
                    'QT_WAYLAND_DISABLE_WINDOWDECORATION', 'WAYLAND_DEBUG', 'QT_STYLE_OVERRIDE',
                    'QT_QPA_PLATFORMTHEME', 'QT_SCREEN_SCALE_FACTORS', 'QT_AUTO_SCREEN_SCALE_FACTOR', 'GTK_THEME'):
            env.pop(key, None)
        env.update(HOME=str(root / 'home'), XDG_RUNTIME_DIR=str(root / 'runtime'), XDG_CONFIG_HOME=str(root / 'config'),
                   DBUS_SYSTEM_BUS_ADDRESS='unix:path=' + str(root / 'runtime/system-bus'),
                   XDG_DATA_HOME=str(root / 'data'), XDG_STATE_HOME=str(root / 'state'), XDG_CACHE_HOME=str(root / 'cache'),
                   XDG_DATA_DIRS='/usr/local/share:/usr/share', XDG_CURRENT_DESKTOP='GNOME', XDG_SESSION_TYPE='wayland',
                   WAYLAND_DISPLAY='dzurkel-test', QT_QPA_PLATFORM='wayland', GSETTINGS_BACKEND='keyfile',
                   QT_SCALE_FACTOR='1', GDK_SCALE='1', GDK_DPI_SCALE='1',
                   LIBGL_ALWAYS_SOFTWARE='1', DZURKEL_REQUIRE_GNOME_NATIVE='1', DZURKEL_GNOME_TEST_ROOT=str(root))
        # This suite previews local fixtures, not GVfs mounts. Keep GVfs and
        # portal services running, but do not create a separate FUSE file view.
        env['GVFS_DISABLE_FUSE'] = '1'
        command = ['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve()),
                   '--binary', args.binary, '--probe', args.probe, '--preview-tests', args.preview_tests,
                   '--report-dir', args.report_dir, '--inside-session']
        with open(Path(args.report_dir) / 'session.log', 'w') as log:
            process = subprocess.Popen(command, env=env, stdout=log, stderr=log, start_new_session=True)
            try:
                result = process.wait(timeout=160)
            except subprocess.TimeoutExpired:
                result = 1
                print('Native GNOME session timed out; see session.log', file=sys.stderr)
            finally:
                # Also reap D-Bus-activated services, including cold-started app.
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                time.sleep(.2)
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                unmount_test_filesystems(root)
        report = Path(args.report_dir) / 'report.json'
        if result != 0:
            data = json.loads(report.read_text())
            data['status'] = 'failed'
            data.setdefault('error', 'Native worker failed; see session.log')
            report.write_text(json.dumps(data, indent=2) + '\n')
        print(report.read_text() if report.exists() else f'No native report; see {args.report_dir}/session.log')
        return result


if __name__ == '__main__':
    sys.exit(main())
