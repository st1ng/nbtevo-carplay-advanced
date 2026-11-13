"""CarPlay lane guidance in librghook.so (src/rghook.c), built for the Mac: LaneGuidanceInformation (0x5204) and the lane fields of
RouteGuidanceUpdate (0x5201: 16 LaneGuidanceIndex, 18 LaneGuidanceShowing) decoded into the routeguidance file's "lanes" line, which
src/hud.h reads. Messages built as the iPhone sends them (layout: libesoiap2's parseLaneInformation, via the community
MIB2 implementation's rgd_tlv.c)."""
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# rghook.c is included whole; its read()/write() hooks are renamed so the harness links on the Mac. The file goes through the Mac's
# open() (QNX flag values differ), the rest of its libc pointers to the Mac's.
HARNESS = r'''
#define read rg_read_hook
#define write rg_write_hook
#include "rghook.c"
#undef read
#undef write
extern int open(const char *, int, ...); extern int close(int); extern long write(int, const void *, unsigned long);
extern int unlink(const char *); extern int rename(const char *, const char *); extern int printf(const char *, ...);
extern long strtol(const char *, char **, int);
static int mac_open(const char *p, int f, ...) { (void)f; return open(p, 0x601 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644); }
static long mac_write(int fd, const void *b, size_t n) { return write(fd, b, n); }
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10; }
int main(int argc, char **argv) {          // message id (hex) and its parameters (hex), one pair per argument pair
  r_open = mac_open; r_write = (long (*)(int, const void *, size_t))mac_write; r_close = close; r_unlink = unlink; r_rename = rename;
  inited = 1; logfd = -1;
  for (int i = 1; i + 1 < argc; i += 2) {
    static u8 b[4096]; u32 n = 0; const char *h = argv[i + 1];
    for (; h[0] && h[1] && n < sizeof b; h += 2) b[n++] = (u8)(hexv(h[0]) << 4 | hexv(h[1]));
    on_rg((u32)strtol(argv[i], 0, 16), b, n);
  }
  return 0;
}
'''


def tlv(pid, v):
    return struct.pack('>HH', len(v) + 4, pid) + v


def lane(pos, status, angles, hl=None):
    v = tlv(0, struct.pack('>H', pos)) + tlv(1, bytes([status])) + tlv(2, b''.join(struct.pack('>h', a) for a in angles))
    return tlv(2, v + (tlv(3, struct.pack('>h', hl)) if hl is not None else b''))


def lanes_msg(idx, *lanes):
    return tlv(0, struct.pack('>H', 0x0F0E)) + tlv(1, struct.pack('>H', idx)) + b''.join(lanes) + tlv(3, b'Use the right lanes\0')


def update(lane_idx=None, showing=None):
    m = tlv(0, struct.pack('>H', 0x0F0E)) + tlv(1, b'\x01') + tlv(13, struct.pack('>H', 0))
    if lane_idx is not None:
        m += tlv(16, struct.pack('>H', lane_idx)) + tlv(17, struct.pack('>H', 3))
    if showing is not None:
        m += tlv(18, bytes([showing]))
    return m


class RgHookLanes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('clang'):
            raise unittest.SkipTest('clang not found')
        cls.tmp = tempfile.TemporaryDirectory(prefix='rghook-')
        d = Path(cls.tmp.name)
        (d / 'h.c').write_text(HARNESS)
        cls.exe = d / 'h'
        subprocess.run(['clang', '-w', '-I', str(ROOT / 'src'), '-o', str(cls.exe), str(d / 'h.c')], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_msgs(self, *msgs):
        pps = Path(self.tmp.name) / 'routeguidance'
        if pps.exists():
            pps.unlink()
        args = [str(self.exe)]
        for mid, payload in msgs:
            args += ['%x' % mid, payload.hex()]
        subprocess.run(args, check=True, env=dict(os.environ, RGHOOK_PPS=str(pps)))
        return dict(l.split(':', 2)[0::2] for l in pps.read_text().splitlines() if l.count(':') >= 2)

    def test_lanes_published_left_to_right(self):
        out = self.run_msgs((0x5204, lanes_msg(4, lane(2, 2, [0, 90], 90), lane(0, 0, [-90]), lane(1, 1, [0]))),
                            (0x5201, update(4, 1)))
        self.assertEqual(out['lane_guidance_index'], '4')
        self.assertEqual(out['lanes'], '0/1000/-90;1/1000/0;2/90/0,90')

    def test_lanes_follow_the_index_and_showing(self):
        one, two = lanes_msg(1, lane(0, 1, [0])), lanes_msg(2, lane(0, 0, [0]), lane(1, 2, [45], 45))
        self.assertEqual(self.run_msgs((0x5204, one), (0x5204, two), (0x5201, update(2)))['lanes'], '0/1000/0;2/45/45')
        self.assertEqual(self.run_msgs((0x5204, one), (0x5204, two), (0x5201, update(1)))['lanes'], '1/1000/0')
        self.assertNotIn('lanes', self.run_msgs((0x5204, one), (0x5201, update(1, 0))))             # the iPhone stopped showing them
        self.assertNotIn('lanes', self.run_msgs((0x5204, one), (0x5201, update(3, 1))))             # none for that index yet
        self.assertNotIn('lanes', self.run_msgs((0x5204, one), (0x5201, update())))                 # no lane index at all
        out = self.run_msgs((0x5201, update(1, 1)), (0x5204, one), (0x5201, update()))              # later updates without the fields keep them
        self.assertEqual(out['lanes'], '1/1000/0')

    def test_lane_details(self):
        # several angles per lane as a list or as repeated angle TLVs, a lane without position, at most 8 lanes and 4 angles
        rep = tlv(2, tlv(0, struct.pack('>H', 0)) + tlv(1, b'\x01') + tlv(2, struct.pack('>h', -45)) + tlv(2, struct.pack('>h', 0)))
        nopos = tlv(2, tlv(1, b'\x02') + tlv(2, struct.pack('>h', 180)))
        out = self.run_msgs((0x5204, lanes_msg(0, rep)), (0x5201, update(0, 1)))
        self.assertEqual(out['lanes'], '1/1000/-45,0')
        self.assertEqual(self.run_msgs((0x5204, lanes_msg(0, nopos)), (0x5201, update(0, 1)))['lanes'], '2/1000/180')
        many = lanes_msg(0, *[lane(i, 1, [0, 10, 20, 30, 40, 50]) for i in range(10)])
        got = self.run_msgs((0x5204, many), (0x5201, update(0, 1)))['lanes'].split(';')
        self.assertEqual(len(got), 8)
        self.assertEqual(got[0], '1/1000/0,10,20,30')


if __name__ == '__main__':
    unittest.main()
