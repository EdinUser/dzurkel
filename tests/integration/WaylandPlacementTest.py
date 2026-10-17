#!/usr/bin/env python3
"""Opt-in test of actual KWin frames; never controls an existing Dzurkel instance.

Run on the desktop bus (not under dbus-run-session). Generated previews use
another, private bus. The temporary KWin script only reads this test PID's
geometry; it never moves windows or changes persistent compositor settings.
With --activate, only the first generated window is activated to test retention.
Requires python3-dbus and python3-gobject in addition to the build dependencies.
"""
import argparse
import struct
import zlib
import dbus, dbus.service, dbus.mainloop.glib
from gi.repository import GLib
from pathlib import Path
import json, os, subprocess, tempfile, time, uuid
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, default=Path('build/dzurkel'))
parser.add_argument('--initial', choices=['small', 'large'], default='large')
parser.add_argument('--report', type=Path)
parser.add_argument('--activate', action='store_true', help='Activate only the initial test window to check focus retention')
args = parser.parse_args()
binary = str(args.binary.resolve(strict=True))
if not os.access(binary, os.X_OK):
    parser.error('The preview binary is not executable')
results = []

def write_png(path, size):
    width, height = size
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    pixels = (b'\0' + b'\x30\x67\xb1' * width) * height
    path.write_bytes(b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
desktop = dbus.SessionBus()
name = 'io.github.EdinUser.Dzurkel.PlacementProbe.p' + uuid.uuid4().hex
owner = dbus.service.BusName(name, desktop)
records=[]
class Receiver(dbus.service.Object):
    @dbus.service.method('io.github.EdinUser.Dzurkel.PlacementProbe', in_signature='s', out_signature='')
    def report(self, value): records.append(json.loads(str(value)))
receiver=Receiver(owner, '/PlacementProbe')
def pump(seconds):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        while GLib.MainContext.default().pending(): GLib.MainContext.default().iteration(False)
        time.sleep(.01)
with tempfile.TemporaryDirectory(prefix='dzurkel-placement-') as td:
    root=Path(td)
    for filename,size in [('small.png',(400,400)),('large.png',(2400,1800)),('wide.png',(2400,1000)),('tall.png',(800,2400))]:
        write_png(root/filename, size)
    (root / 'reading.txt').write_text('A document keeps its reading size.\n' * 20)
    (root / 'unsupported.bin').write_bytes(bytes([0, 1, 2, 255]))
    daemon=subprocess.Popen(['dbus-daemon','--session','--nofork','--print-address=1'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
    address=daemon.stdout.readline().strip()
    environment={**os.environ,'DBUS_SESSION_BUS_ADDRESS':address,'QT_QPA_PLATFORM':'wayland',
        'DZURKEL_TEST_KWIN_BUS_ADDRESS': os.environ['DBUS_SESSION_BUS_ADDRESS']}
    environment.pop('XDG_ACTIVATION_TOKEN',None)
    log=open(root/'app.log','w')
    primary=subprocess.Popen([binary,str(root/(args.initial + '.png'))],env=environment,stdout=log,stderr=log)
    plugin='dzurkel-placement-'+uuid.uuid4().hex
    scripting=dbus.Interface(desktop.get_object('org.kde.KWin','/Scripting'),'org.kde.kwin.Scripting')
    loaded = False
    bus = None
    try:
        script=root/'observer.js'
        script.write_text('''var activated = false;
function rect(r) { return [r.x,r.y,r.width,r.height]; }
function observe(w) {
    if (w.pid !== PID || !w.normalWindow || w.dialog) return;
    function report(event) {
        callDBus("SERVICE", "/PlacementProbe", "io.github.EdinUser.Dzurkel.PlacementProbe", "report",
            JSON.stringify({event:typeof event === 'string' ? event : 'geometry', caption:w.caption, active:w.active, hidden:w.hidden,
                id:String(w.internalId), frame:rect(w.frameGeometry), area:rect(workspace.clientArea(KWin.MaximizeArea,w))}));
    }
    report();
    w.frameGeometryChanged.connect(report);
    w.activeChanged.connect(report);
    w.captionChanged.connect(report);
    w.hiddenChanged.connect(function() {report('hidden');});
    w.windowHidden.connect(function() {report('unmapped');});
    w.closed.connect(function() {report('closed');});
    if (ACTIVATE && !activated) { activated = true; workspace.activeWindow = w; }
}
workspace.stackingOrder.forEach(observe);
workspace.windowAdded.connect(observe);
'''.replace('PID',str(primary.pid)).replace('SERVICE',name).replace('ACTIVATE',str(args.activate).lower()))
        sid=int(desktop.call_blocking('org.kde.KWin','/Scripting','org.kde.kwin.Scripting','loadScript','ss',(str(script),plugin)))
        loaded = True
        obj=desktop.get_object('org.kde.KWin',f'/Scripting/Script{sid}')
        dbus.Interface(obj,'org.kde.kwin.Script').run()
        pump(2)
        bus=dbus.bus.BusConnection(address)
        props=dbus.Interface(bus.get_object('io.github.EdinUser.Dzurkel.Previewer','/io/github/EdinUser/Dzurkel/Previewer'),'org.freedesktop.DBus.Properties')
        def ready(filename):
            deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                pump(.05)
                state=props.GetAll('io.github.EdinUser.Dzurkel.Previewer1')
                if state['Visible'] and not state['Loading'] and state['FileUrl']==(root/filename).as_uri(): break
            else: raise RuntimeError('Preview never became ready')
            pump(.7)
            matches=[r for r in records if filename in r['caption']]
            if not matches: raise RuntimeError('No KWin window record')
            r=matches[-1]
            x,y,w,h=r['frame']; ax,ay,aw,ah=r['area']
            r['contained']=w>0 and h>0 and x>=ax and y>=ay and x+w<=ax+aw and y+h<=ay+ah
            results.append(r)
            print(json.dumps(r),flush=True)
        ready(args.initial + '.png')
        for filename in ['small.png','large.png','small.png','wide.png','tall.png','reading.txt','unsupported.bin','wide.png','reading.txt']:
            subprocess.run([binary,'--show',str(root/filename)],env=environment,check=True,stdout=log,stderr=log,timeout=10)
            ready(filename)
        # Bursts must cancel older metadata/placement work without remapping.
        for filename in ['large.png', 'small.png', 'tall.png', 'wide.png']:
            subprocess.run([binary,'--show',str(root/filename)],env=environment,check=True,stdout=log,stderr=log,timeout=10)
        ready('wide.png')
        live_records = list(records)
    finally:
        if loaded: scripting.unloadScript(plugin)
        subprocess.run([binary,'--quit'],env=environment,stdout=log,stderr=log,timeout=5)
        try: primary.wait(timeout=5)
        except subprocess.TimeoutExpired: primary.kill(); primary.wait()
        if bus is not None: bus.close()
        daemon.terminate(); daemon.wait(timeout=5)
        log.close()

if args.report:
    args.report.write_text(json.dumps(results, indent=2) + '\n')
assert results and all(r['contained'] for r in results), 'KWin placed a preview outside its actual work area'
assert len({r['id'] for r in live_records}) == 1, 'Navigation replaced the native window'
assert not any(r['event'] in ('closed', 'unmapped') or r.get('hidden', False) for r in live_records), 'Navigation unmapped the preview'
assert len({tuple(r['frame'][2:]) for r in results}) >= 4, 'Images did not dynamically resize the window'
reading_sizes = [r['frame'][2:] for r in results if 'reading.txt' in r['caption']]
assert len(reading_sizes) == 2 and reading_sizes[0] == reading_sizes[1], 'Temporary preview sizes replaced the remembered document size'
if args.activate:
    assert all(r['active'] for r in results), 'An automatic resize lost keyboard focus'
    first_active = next(i for i,r in enumerate(live_records) if r['active'])
    assert all(r['active'] for r in live_records[first_active:]), 'Navigation temporarily handed focus away'
print(f'{len(results)} compositor placement checks passed; temporary observer removed.')
