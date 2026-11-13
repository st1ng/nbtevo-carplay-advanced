""""Full-screen CarPlay" file changes: src/pbwpatch.h (the HMI layout) and src/iniwide.h (carplay.ini), built for the Mac.

No firmware is kept here. The synthetic cases build small protobuf files with the same structure as the HMI's layouts. The cases on
real files run only when CPA_LAYOUTS names a folder with them (anything below it is searched), for example the IDEmu repository's
cache/bmw-layouts with one folder per HMI build:
    <CPA_LAYOUTS>/006_017_006/.../ro/mini/id5/widget/generated/Spec_State_Merged.pbw   (MINI brand package, swfl 2716)
    <CPA_LAYOUTS>/006_017_006/.../ro/common/widget/generated/Spec_State_Merged.pbw     (HMI package, swfl 2720: BMW's CarPlay state)
    <CPA_LAYOUTS>/006_006_006/...                                                      (the same for ID5_1703-490 Build 139)
    <CPA_LAYOUTS>/006_017_006/.../opt/conn/data/carplay/carplay.ini                     (swfl 271a; 006_006_006 has the same file)
    <CPA_LAYOUTS>/007_047_001/.../opt/conn/data/carplay/carplay.ini                     (optional: 007's own full-width sections)
The outputs of the known input files are pinned by SHA-256 (the MINI ones as the plugin made them before BMW support).
"""
import hashlib
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LAYOUTS = Path(os.environ['CPA_LAYOUTS']) if os.environ.get('CPA_LAYOUTS') else None

HARNESS = r'''
#include <cstdio>
#include <cstring>
#include "pbwpatch.h"
#include "iniwide.h"
static unsigned char b[1 << 21], o[(1 << 21) + 4096];
int main(int argc, char **argv) {  // mode in out
  FILE *f = fopen(argv[2], "rb");
  if (!f) return 2;
  unsigned n = fread(b, 1, sizeof b, f), m = 0;
  fclose(f);
  const char *w = 0, *k = argv[1];
  if (!strcmp(k, "mini006")) m = pbw::patch(b, n, o);
  else if (!strcmp(k, "mini1703")) m = pbw::patchFixed(b, n, o);
  else if (!strcmp(k, "bmw1703")) m = pbw::patchCommon1703(b, n, o);
  else if (!strcmp(k, "bmw006")) return pbw::hasFullscreen006(b, n) ? 0 : 3;
  else if (!strcmp(k, "ini")) m = ini::patch(b, n, o, true, false);
  else if (!strcmp(k, "inibmw")) m = ini::patch(b, n, o, true, false, true);
  else if (!strcmp(k, "conn")) m = ini::patchConn(b, n, o, true, false, 0, false, 0, &w);
  if (!m) return 3;
  f = fopen(argv[3], "wb");
  fwrite(o, 1, m, f);
  fclose(f);
  if (w) printf("%s\n", w);
  return 0;
}
'''

# known inputs (SHA-256) -> the output the plugin must write (SHA-256); None = nothing is written (006 BMW: the HMI's own slot)
KNOWN = {
    'mini006': ('01175b85ad8156b93bb1ab23e2367a6ed6f5d58f0584b56838c66aba4008e782',
                '92fd52f35b68331e5234a20c46d62a75e1deec348dd5cbf9548cc7ff87bec10e'),
    'mini1703': ('e8263cac770e957befea2b3299f01d6f07a5d6c8025e2d53e3f428300037dd9a',
                 '16ec36d9f666ce1956c43a3a4347780bd1711eff5c77fc00092a7c492f1f2bf0'),
    'bmw006': ('e7d86ccb3fa4b72ac190a3d87546807f237cc0e45550de2aeeea2945e5a87250', None),
    'bmw1703': ('ba7989d773baaa87941e752a94c1d770b68a524cdb8ba0a4b3761fc4ea9cc196',
                'b9ff4218ba4752286a39bba7e405e21fc1b3b91412c60cdb546fb64d9c487b29'),
}


# ---- a minimal protobuf writer / reader ----------------------------------------------------------------------------------------
def varint(v):
    out = bytearray()
    while v >= 0x80:
        out.append(v & 0x7f | 0x80)
        v >>= 7
    out.append(v)
    return bytes(out)


def ld(field, payload):          # length-delimited field
    return varint(field << 3 | 2) + varint(len(payload)) + payload


def vi(field, v):                # varint field
    return varint(field << 3) + varint(v)


def parse(d):
    i, out = 0, []
    while i < len(d):
        k, i = _rv(d, i)
        f, t = k >> 3, k & 7
        if t == 0:
            v, i = _rv(d, i)
        elif t == 1:
            v, i = d[i:i + 8], i + 8
        elif t == 5:
            v, i = d[i:i + 4], i + 4
        elif t == 2:
            n, i = _rv(d, i)
            v, i = d[i:i + n], i + n
        else:
            raise ValueError('wire type %d' % t)
        out.append((f, t, v))
    if i != len(d):
        raise ValueError('trailing bytes')
    return out


def _rv(d, i):
    r = s = 0
    while True:
        b = d[i]
        i += 1
        r |= (b & 0x7f) << s
        s += 7
        if b < 0x80:
            return r, i


def decodes_fully(test, b, depth=0):
    """every length-delimited value that decodes as a message re-encodes to the same bytes (the lengths were fixed)"""
    for f, t, v in parse(b):
        if t == 2 and depth < 60:
            try:
                sub = parse(v)
            except (ValueError, IndexError):
                continue
            if sub:
                decodes_fully(test, v, depth + 1)


# the HMI's layout pieces (as in HMI ID5_1703-490: names in strings)
def name(s):
    return ld(1, ld(2, s.encode()))


def ref(s, kind):               # operand: a name; kind 0/1 own parameter, 4 constant
    return ld(1, ld(2, s.encode())) + ld(2, vi(4, kind))


def lit(v):                     # operand: a float literal
    return ld(2, ld(1, ld(5, varint(3 << 3 | 5) + struct.pack('<f', v))))


def vec3(*ops):                 # expression vec3(float, float, float)
    return vi(1, 21) + vi(2, 2) + vi(2, 2) + vi(2, 2) + b''.join(ld(3, o) for o in ops)


def widget(wname, size, pos):   # a layout entry: name, size (field 3), position (field 5)
    return ld(1, ld(1, ld(2, wname.encode())) + ld(3, ld(2, ld(5, size))) + ld(5, ld(2, ld(5, pos))))


def state(sname, *body):
    return ld(1, name(sname) + ld(2, ld(2, b'eWidgetType_BasicTablet')) + b''.join(body))


def bmw_carplay_1703(slot_w=None, panel_x=None):
    slot_w = slot_w or ref('smallScreenW', 4)
    panel_x = panel_x or ref('smallScreenW', 4)
    slot = widget('Table', vec3(slot_w, ref('defaultD', 4), ref('eParam_WidgetHeight', 1)), vec3(lit(0), lit(0), lit(0)))
    panel = widget('ContentPreview', vec3(ref('contentPreviewW', 4), ref('defaultD', 4), ref('contentPreviewH', 4)),
                   vec3(panel_x, lit(0), ref('contentPreviewMB', 4)))
    return state('LT_State_CarPlay', ld(4, ld(1, ld(2, b'spec') + ld(2, slot + panel))))


def other_state_1703():         # another state with the same side-panel position: only the CarPlay state may change
    panel = widget('ContentPreview', vec3(ref('contentPreviewW', 4), ref('defaultD', 4), ref('contentPreviewH', 4)),
                   vec3(ref('smallScreenW', 4), lit(0), ref('contentPreviewMB', 4)))
    return state('LT_State_Media', ld(4, ld(1, ld(2, b'spec') + ld(2, panel))))


class Layout(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cxx = shutil.which('clang++') or shutil.which('c++')
        if not cxx:
            raise unittest.SkipTest('clang++ not found')
        cls.tmp = tempfile.TemporaryDirectory(prefix='cpa-layout-')
        cls.d = Path(cls.tmp.name)
        (cls.d / 'h.cpp').write_text(HARNESS)
        cls.exe = cls.d / 'h'
        subprocess.run([cxx, '-std=c++11', '-I', str(ROOT / 'src'), '-o', str(cls.exe), str(cls.d / 'h.cpp')], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_h(self, mode, data):
        src = self.d / ('in-' + mode)
        out = self.d / ('out-' + mode)
        src.write_bytes(data)
        r = subprocess.run([str(self.exe), mode, str(src), str(out)], capture_output=True)
        self.assertIn(r.returncode, (0, 3), r.stderr)
        self.last_stdout = r.stdout.decode().strip()
        if mode == 'bmw006':
            return r.returncode == 0
        return out.read_bytes() if r.returncode == 0 else None

    # ---- synthetic ----------------------------------------------------------------------------------------------------------------
    def test_bmw1703_synthetic(self):
        src = state('LT_State_Start', name('x')) + other_state_1703() + bmw_carplay_1703() + other_state_1703()
        out = self.run_h('bmw1703', src)
        want = (state('LT_State_Start', name('x')) + other_state_1703() + bmw_carplay_1703(ref('screenW', 4), lit(2000)) +
                other_state_1703())
        self.assertEqual(out, want)
        self.assertEqual(len(out), len(src) - 14)
        decodes_fully(self, out)
        self.assertIsNone(self.run_h('bmw1703', out))                       # already changed: refused

    def test_bmw1703_refuses_unexpected(self):
        self.assertIsNone(self.run_h('bmw1703', other_state_1703()))      # no CarPlay state
        self.assertIsNone(self.run_h('bmw1703', bmw_carplay_1703() * 2))  # two CarPlay states
        self.assertIsNone(self.run_h('bmw1703', bmw_carplay_1703(panel_x=ref('layoutScreenW', 4))))   # side panel elsewhere
        self.assertIsNone(self.run_h('bmw1703', bmw_carplay_1703(lit(844))))   # the MINI slot (844): patchFixed's, not this
        self.assertIsNotNone(self.run_h('bmw1703', bmw_carplay_1703()))
        self.assertIsNone(self.run_h('bmw1703', b'\x0a\x05abc'))           # not protobuf

    def test_bmw006_synthetic(self):
        def ix(i, kind):
            return ld(1, vi(1, i)) + ld(2, vi(4, kind))
        sel = ld(2, ld(5, vi(1, 18) + vi(2, 11) + vi(2, 2) + vi(2, 2) + ld(3, ix(2243, 0)) + ld(3, ix(4631, 4)) + ld(3, ix(25094, 4))))
        slot = vi(1, 21) + vi(2, 2) + vi(2, 2) + vi(2, 2) + ld(3, sel) + ld(3, ix(23046, 4)) + ld(3, ix(4353, 1))
        good = ld(1, ld(1, vi(1, 27810)) + ld(4, ld(3, ld(2, ld(5, slot)))))
        other = ld(1, ld(1, vi(1, 12345)) + ld(4, ld(3, ld(2, ld(5, slot)))))
        self.assertTrue(self.run_h('bmw006', other + good))
        self.assertFalse(self.run_h('bmw006', other))                       # the slot, but not in LT_State_CarPlay
        self.assertFalse(self.run_h('bmw006', good.replace(ix(4631, 4), ix(4632, 4))))

    def test_ini_synthetic(self):
        sec = lambda o, w, h, mm: ('Option: %s\n###\n[Screen]\nWidth=%d\nHeight=%d\nPhysicalWidth=%d\nPhysicalHeight=78\n'
                                   'VideoURL=screen:/any?winid=1280&dstx=0&dsty=0&dstw=%d&dsth=%d\n###\n\n' % (o, w, h, mm, w, h))
        tpl = (sec('900_540', 900, 540, 152) + sec('800_480', 800, 480, 131) + sec('750_480', 750, 480, 123) +
               sec('711_480', 711, 480, 127)).encode()
        mini = self.run_h('ini', tpl).decode()
        self.assertEqual(mini, tpl.decode().replace('Width=750\n', 'Width=1184\n').replace('Width=711\n', 'Width=1184\n')
                         .replace('PhysicalWidth=123', 'PhysicalWidth=194').replace('PhysicalWidth=127', 'PhysicalWidth=194')
                         .replace('dstw=750', 'dstw=1180').replace('dstw=711', 'dstw=1180'))
        bmw = self.run_h('inibmw', tpl).decode()
        self.assertEqual(bmw, tpl.decode().replace('Width=900\n', 'Width=1440\n').replace('Width=800\n', 'Width=1280\n')
                         .replace('PhysicalWidth=152', 'PhysicalWidth=242').replace('PhysicalWidth=131', 'PhysicalWidth=208')
                         .replace('dstw=900', 'dstw=1440').replace('dstw=800', 'dstw=1280'))
        for w, want in ((900, (1440, 242)), (800, (1280, 208)), (750, (1184, 194)), (711, (1184, 194))):
            conn = ('[General]\nName=x\n[Screen]\nUUID=1\nWidth=%d\nHeight=480\nPhysicalWidth=100\n'
                    'VideoURL=screen:/any?dstw=%d&dsth=480\n[KnobInput]\nPrimaryInput=YES\n' % (w, w)).encode()
            out = self.run_h('conn', conn).decode()
            dst = 1180 if want[0] == 1184 else want[0]
            self.assertIn('\nWidth=%d\nHeight=480\nPhysicalWidth=%d\n' % want, out)
            self.assertIn('dstw=%d&' % dst, out)
            self.assertEqual(self.last_stdout, 'Width=%d' % want[0])

    # ---- real files (CPA_LAYOUTS) -----------------------------------------------------------------------------------------------------
    def find(self, build, tail):
        if not LAYOUTS:
            self.skipTest('CPA_LAYOUTS not set (real layout files)')
        hits = sorted((LAYOUTS / build).rglob('Spec_State_Merged.pbw' if tail.endswith('.pbw') else Path(tail).name))
        hits = [h for h in hits if h.as_posix().endswith(tail)]
        if not hits:
            self.skipTest('%s: no %s under %s' % (build, tail, LAYOUTS))
        return hits[0].read_bytes()

    def check_known(self, mode, src, out):
        want = KNOWN[mode]
        if hashlib.sha256(src).hexdigest() != want[0]:
            return                                                          # another file: only the structural checks
        self.assertEqual(hashlib.sha256(out).hexdigest() if out is not None else None, want[1])

    def test_real_mini006(self):
        src = self.find('006_017_006', 'mini/id5/widget/generated/Spec_State_Merged.pbw')
        out = self.run_h('mini006', src)
        self.assertEqual(len(out), len(src) + 61)
        decodes_fully(self, out)
        self.check_known('mini006', src, out)
        self.assertIsNone(self.run_h('mini006', out))
        self.assertIsNone(self.run_h('bmw1703', src))
        self.assertFalse(self.run_h('bmw006', src))                         # the MINI state overrides the common one

    def test_real_mini1703(self):
        src = self.find('006_006_006', 'mini/id5/widget/generated/Spec_State_Merged.pbw')
        out = self.run_h('mini1703', src)
        self.assertEqual(len(out), len(src) - 9)
        decodes_fully(self, out)
        self.check_known('mini1703', src, out)
        self.assertIsNone(self.run_h('bmw1703', src))                       # MINI's slot is the literal 844

    def test_real_bmw006(self):
        src = self.find('006_017_006', 'ro/common/widget/generated/Spec_State_Merged.pbw')
        self.assertTrue(self.run_h('bmw006', src))
        self.check_known('bmw006', src, None)
        for mode in ('mini006', 'mini1703', 'bmw1703'):
            self.assertIsNone(self.run_h(mode, src), mode)

    def test_real_bmw1703(self):
        src = self.find('006_006_006', 'ro/common/widget/generated/Spec_State_Merged.pbw')
        out = self.run_h('bmw1703', src)
        self.assertEqual(len(out), len(src) - 14)
        decodes_fully(self, out)
        self.check_known('bmw1703', src, out)
        a, b = parse(src), parse(out)
        changed = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
        self.assertEqual(len(a), len(b))
        self.assertEqual(len(changed), 1)                                   # only LT_State_CarPlay
        self.assertTrue(b[changed[0]][2].startswith(name('LT_State_CarPlay')))
        st = b[changed[0]][2]
        self.assertIn(vec3(ref('screenW', 4), ref('defaultD', 4), ref('eParam_WidgetHeight', 1)), st)
        self.assertIn(vec3(lit(2000), lit(0), ref('contentPreviewMB', 4)), st)
        self.assertIsNone(self.run_h('bmw1703', out))
        for mode in ('mini006', 'mini1703'):
            self.assertIsNone(self.run_h(mode, src), mode)
        self.assertFalse(self.run_h('bmw006', src))

    def test_real_carplay_ini(self):
        src = self.find('006_017_006', 'opt/conn/data/carplay/carplay.ini').decode()
        sections = {}
        for mode in ('ini', 'inibmw'):
            out = self.run_h(mode, src.encode()).decode()
            sec, diff = None, []
            for x, y in zip(src.split('\n'), out.split('\n')):
                if x.startswith('Option: '):
                    sec = x[8:]
                if x != y:
                    diff.append((sec, y.split('&dsty')[-1] if y.startswith('VideoURL') else y))
            sections[mode] = diff
        self.assertEqual(sorted({s for s, _ in sections['ini']}), ['711_480', '750_480'])
        self.assertEqual(sorted({s for s, _ in sections['inibmw']}), ['800_480', '900_540'])
        self.assertEqual(len(sections['ini']), 6)
        self.assertEqual(len(sections['inibmw']), 6)
        bmw = self.run_h('inibmw', src.encode()).decode()
        try:
            ref007 = self.find('007_047_001', 'opt/conn/data/carplay/carplay.ini').decode()
        except unittest.SkipTest:
            return
        # the BMW sections are what 007_047_001 has as its own full-width sections (1440_540, 1280_480)
        def screen(text, option):
            body = text.split('Option: ' + option + '\n', 1)[1].split('\nOption: ', 1)[0]
            keep = ('Width=', 'Height=', 'PhysicalWidth=', 'PhysicalHeight=', 'VideoURL=')
            return [l for l in body.split('\n') if l.startswith(keep)]
        self.assertEqual(screen(bmw, '900_540'), screen(ref007, '1440_540'))
        self.assertEqual(screen(bmw, '800_480'), screen(ref007, '1280_480'))


if __name__ == '__main__':
    unittest.main()
