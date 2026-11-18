"""The plugin's per-build tables (src/cpm.cpp kBuilds, src/tilt_*.json), without firmware, and against a build's own HmiMain3D when
one is given.

No firmware is kept here. The always-run cases check that each tilt patch is self-consistent: the same 19 places, the same new code,
and every branch it writes lands where the patch means it to (key filter -> the function's exit, the direction block -> the send call,
the original argument setup -> the direction block, the trace's branch -> the argument setup). With CPA_HMI_1607 set to the plain
HmiMain3D of ID5_1607-490-1610T Build 153 (sha256 0fa0e6e0...), the 1607 entry is checked word by word against it, as the plugin
checks it in the car.
"""
import hashlib
import json
import os
import re
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPM = (ROOT / 'src' / 'cpm.cpp').read_text()
TILTS = {name: json.loads((ROOT / 'src' / ('tilt_%s.json' % name)).read_text())['words'] for name in ('006', '1703', '1607')}


def kbuilds():
    """kBuilds as {id: [touchFn, ctlSetFn, ctlProxy, playFn, playObj, tdFirst, tdEnd, tdTime, tdOrigin, tilt, fs, pbw, pbwBmw, ini]}."""
    block = CPM[CPM.index('const Build kBuilds[] = {'):]
    block = block[:block.index('\n};')]
    out = {}
    for chunk in block.split('\n  { "')[1:]:
        name = chunk[:chunk.index('"')]
        body = re.sub(r'//[^\n]*|/\*.*?\*/', '', chunk[chunk.index('",') + 2:], flags=re.S)
        body = body[body.index('",') + 2:].rstrip().rstrip(',').rstrip('}')
        f = [x.strip() for x in body.split(',') if x.strip()]
        nums = [int(x, 0) if re.match(r'^(0x)?[0-9a-f]+$', x) else x for x in f]
        # touchFn..tdOrigin (9), tilt, its count, fs, its count, pbw, pbwBmw, ini
        out[name] = nums[:9] + [nums[9], nums[11], nums[13], nums[14], nums[15]]
    return out


def word(w):
    return int(w, 16)


def b_target(addr, w):
    off = w & 0xffffff
    if off & 0x800000:
        off -= 1 << 24
    return addr + 8 + 4 * off


class TiltTables(unittest.TestCase):
    def test_same_places_and_code(self):
        new = {k: [w['new'] for w in v] for k, v in TILTS.items()}
        for name, words in TILTS.items():
            self.assertEqual(len(words), 19, name)
            # the key filter's bhi keeps the build's own exit (imm24), the trace's branch its own distance (far in 006); every other
            # new word is the same in every build
            for i in range(18):
                if i == 2:
                    self.assertEqual(words[i]['new'][:2], '8a', name)
                    self.assertEqual(words[i]['new'][2:], words[i]['orig'][2:], name)
                    self.assertEqual(words[i]['orig'][:2], '1a', name)
                else:
                    self.assertEqual(words[i]['new'], new['1703'][i], (name, i))

    def test_branches_land_where_meant(self):
        for name, words in TILTS.items():
            a = [word(w['addr']) for w in words]
            tA, tB, tC = a[0], a[3], a[18]
            self.assertEqual(a[:3], [tA, tA + 4, tA + 8], name)
            self.assertEqual(a[3:16], [tB + 4 * i for i in range(13)], name)
            self.assertEqual(a[16:18], [tB + 0x54, tB + 0x7c], name)
            # the direction block ends with a jump to the send call (tB + 0xa0), the setup jumps to the block, the trace to the setup
            self.assertEqual(b_target(a[15], word(words[15]['new'])), tB + 0xa0, name)
            self.assertEqual(b_target(a[17], word(words[17]['new'])), tB, name)
            self.assertEqual(b_target(a[18], word(words[18]['new'])), tB + 0x40, name)
            # the original words the patch expects at those places
            self.assertEqual([w['orig'] for w in words[:2]], ['e355000e', '1355000d'], name)
            self.assertEqual(words[16]['orig'], '03833001', name)   # orreq r3, r3, #1 (tilt left = Back)
            self.assertEqual(words[17]['orig'], 'e355003a', name)   # cmp r5, #0x3a
            self.assertEqual(words[18]['orig'], 'e51b302c', name)

    def test_kbuilds_entries(self):
        b = kbuilds()
        self.assertEqual(set(b), {'ID5_1803-510-1749MA Build 71', 'ID5_1703-490-1650ZD Build 139', 'ID5_1607-490-1610T Build 153'})
        e = b['ID5_1607-490-1610T Build 153']
        self.assertEqual(e[:9], [0x8b4d24, 0x1ec2a24, 0x4d64, 0x1ec2984, 0x51b4, 8, 0xc, 0x18, 0x1c])
        self.assertEqual(e[9], 'kTilt1607')
        self.assertEqual(e[11:], [0, 3, 1])                         # BMW common layout patch (900 -> 1440, panel off screen), widened ini
        # the controller and playback proxies keep their distance from 1703's (both 0xfc lower), the send functions theirs (0xa0)
        e7 = b['ID5_1703-490-1650ZD Build 139']
        self.assertEqual(e7[2] - e[2], 0xfc)
        self.assertEqual(e7[4] - e[4], 0xfc)
        self.assertEqual(e7[1] - e7[3], e[1] - e[3])

    def test_installer_lists_1607(self):
        s = (ROOT / 'carplay-advanced.sh').read_text()
        table = s[s.index('supported_table() {'):]
        table = table[:table.index('\nEOF')]
        rows = {tuple(l.split()[:2]): l.split()[2] for l in table.splitlines() if l.startswith('ID5_1607')}
        self.assertEqual(rows[('ID5_1607-490-1610T_Build_153', 'HmiMain3D')],
                         '0fa0e6e0de49235c02a8bcebca588dad18bd5eab512066ab44ed295a09ca3fbd')
        self.assertEqual(rows[('ID5_1607-490-1610T_Build_153', 'libconn_carplay.so')],
                         'bbdbae7df387c448a3eec9dd607f9e7b196b899039a0123e6299d5ad0fa96827')
        self.assertEqual(rows[('ID5_1607-490-1610T_Build_153', 'scan')], 'none')
        self.assertEqual(rows[('ID5_1607-490-1610T_Build_153', 'fullscreen')], 'pbw1607')


@unittest.skipUnless(os.environ.get('CPA_HMI_1607'), 'CPA_HMI_1607: the plain HmiMain3D of ID5_1607-490-1610T Build 153')
class Build1607(unittest.TestCase):
    """The 1607 entry against its HmiMain3D: what the plugin compares before it writes, and how the places were found."""

    @classmethod
    def setUpClass(cls):
        cls.data = Path(os.environ['CPA_HMI_1607']).read_bytes()
        assert hashlib.sha256(cls.data).hexdigest() == '0fa0e6e0de49235c02a8bcebca588dad18bd5eab512066ab44ed295a09ca3fbd'
        cls.e = kbuilds()['ID5_1607-490-1610T Build 153']

    def u32(self, va):
        return struct.unpack_from('<I', self.data, va - 0x100000)[0]          # text: file offset 0 at 0x100000

    def test_tilt_originals(self):
        for w in TILTS['1607']:
            self.assertEqual('%08x' % self.u32(word(w['addr'])), w['orig'], w['addr'])

    def test_hook_entries(self):
        touch, ctl, _, play = self.e[:4]
        self.assertEqual((self.u32(touch), self.u32(touch + 4)), (0xe92d4ff0, 0xe28db020))   # installTouchHook
        self.assertEqual((self.u32(ctl), self.u32(ctl + 4)), (0xe92d49f0, 0xe28db018))       # installCtlHook
        self.assertEqual(self.u32(play), 0xe92d4870)

    def test_touch_data_offsets(self):
        touch = self.e[0]
        # ldr r1, [fp, #-0x8c] (CTouchData**); ldr r3, [r1]; ldr r3, [r3, #0x1c] (origin); cmp r3, #1
        self.assertEqual([self.u32(touch + 0x20 + 4 * i) for i in range(4)], [0xe51b108c, 0xe5913000, 0xe593301c, 0xe3530001])
        self.assertEqual(self.u32(touch + 0x84), 0xe5933008)        # ldr r3, [r3, #8]: the first finger
        self.assertEqual(self.u32(touch + 0x4a8), 0xe593300c)       # ldr r3, [r3, #0xc]: the end of the list

    def test_controller_send(self):
        tB = word(TILTS['1607'][3]['addr'])
        call = tB + 0xa0
        self.assertEqual(b_target(call, self.u32(call)), self.e[1])                         # bl ctlSetFn
        # add r0, r0, #0x4d00 / add r0, r0, #0x64: the proxy at *(activity + 0xb0) + ctlProxy
        self.assertEqual((self.u32(tB + 0x68), self.u32(tB + 0x78)), (0xe2800c4d, 0xe2800064))
        self.assertEqual(self.u32(tB + 0x4c), 0xe59100b0)                                   # ldr r0, [r1, #0xb0]

    def test_playback_send(self):
        # the HMI's own next-track call: mov r1, #0x8d; add r0, r0, #0x5100; add r0, r0, #0xb4; bl playFn
        call = 0xc5b3e8
        self.assertEqual([self.u32(call - 12 + 4 * i) for i in range(3)], [0xe3a0108d, 0xe2800c51, 0xe28000b4])
        self.assertEqual(b_target(call, self.u32(call)), self.e[3])


if __name__ == '__main__':
    unittest.main()
