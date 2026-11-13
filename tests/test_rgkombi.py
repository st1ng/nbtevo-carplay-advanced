"""librghook-kombi.so's KOMBI sender (src/rgkombi.cpp with rghook.c built with RGHOOK_KOMBI), built for the Mac and fed iAP2 route-guidance messages as the iPhone
sends them; its worker thread talks to a fake KOMBI on 127.0.0.1 (RGHOOK_KOMBI). End to end: the first maneuver = the route graph with
the iPhone's direction, street and distance, a distance change = one frame, the end of the route = the factory cancel, "CarPlay
directions in HUD" switched off = cancel, Loading / Locating keeps the arrow, the marker for the watcher (pid while an arrow is
shown), an earlier session's left-over arrow cleared first, and the yield to a BMW navigation route seen in the system log (a file
here instead of sloginfo). (MOST cars get the plain librghook.so: tests/test_rghook_lanes.py.)"""
import os
import shutil
import struct
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_kombi import FakeKombi  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#define read rg_read_hook
#define write rg_write_hook
#include "rghook.c"
#undef read
#undef write
extern int open(const char *, int, ...); extern int close(int); extern long write(int, const void *, unsigned long);
extern long read(int, void *, unsigned long); extern int unlink(const char *); extern int rename(const char *, const char *);
extern long strtol(const char *, char **, int); extern char *fgets(char *, int, void *); extern void *__stdinp;
extern int strncmp(const char *, const char *, unsigned long);
extern int usleep(unsigned); extern int fflush(void *); extern int printf(const char *, ...); extern void *__stdoutp;
static int mac_open(const char *p, int f, ...) { return f ? open(p, 0x601 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644) : open(p, 0); }
static long mac_write(int fd, const void *b, size_t n) { return write(fd, b, n); }
static long mac_read(int fd, void *b, size_t n) { return read(fd, b, n); }
int spawnle(int m, const char *p, const char *a, ...) { (void)m; (void)p; (void)a; return -1; }   /* QNX only: the watcher */
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10; }
int main(void) {             /* stdin: "msg <id hex> <payload hex>", "sleep <ms>" */
  r_open = mac_open; r_write = (long (*)(int, const void *, size_t))mac_write; r_read = (long (*)(int, void *, size_t))mac_read;
  r_close = close; r_unlink = unlink; r_rename = rename; inited = 1;
  char line[8192];
  while (fgets(line, sizeof line, __stdinp)) {
    if (!strncmp(line, "sleep ", 6)) usleep((unsigned)strtol(line + 6, 0, 10) * 1000);
    else if (!strncmp(line, "msg ", 4)) {
      char *e; u32 mid = (u32)strtol(line + 4, &e, 16); while (*e == ' ') e++;
      static u8 b[4096]; u32 n = 0;
      for (const char *h = e; h[0] > ' ' && h[1] > ' ' && n < sizeof b; h += 2) b[n++] = (u8)(hexv(h[0]) << 4 | hexv(h[1]));
      on_rg(mid, b, n);
    }
    printf("ok\n"); fflush(__stdoutp);
  }
  return 0;
}
'''


def tlv(pid, v):
    return struct.pack('>HH', len(v) + 4, pid) + v


def maneuver(idx, mtype, road, angle=0):
    return (tlv(0, struct.pack('>H', 0x0F0E)) + tlv(1, struct.pack('>H', idx)) + tlv(3, bytes([mtype])) + tlv(4, road.encode() + b'\0')
            + tlv(11, struct.pack('>h', angle)) + tlv(8, b'\x00'))


def update(state=1, idx=0, metres=450, text='450 m', units=2):
    return (tlv(0, struct.pack('>H', 0x0F0E)) + tlv(1, bytes([state])) + tlv(10, struct.pack('>I', metres))
            + tlv(11, text.encode() + b'\0') + tlv(12, bytes([units])) + tlv(13, struct.pack('>H', idx)))


class RgKombi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('clang++'):
            raise unittest.SkipTest('clang++ not found')
        cls.tmp = tempfile.TemporaryDirectory(prefix='rgkombi-')
        d = Path(cls.tmp.name)
        (d / 'h.c').write_text(HARNESS)
        inc = ['-I', str(ROOT / 'src')]
        subprocess.run(['clang', '-w', '-c', '-DRGHOOK_KOMBI', *inc, '-o', str(d / 'h.o'), str(d / 'h.c')], check=True)
        subprocess.run(['clang++', '-w', '-std=c++11', '-DKOMBI_HOST', '-c', *inc, '-o', str(d / 'k.o'),
                        str(ROOT / 'src/rgkombi.cpp')], check=True)
        cls.exe = d / 'h'
        subprocess.run(['clang++', '-o', str(cls.exe), str(d / 'h.o'), str(d / 'k.o'), '-lpthread'], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.k = FakeKombi()
        self.addCleanup(self.k.close)
        self.dir = Path(tempfile.mkdtemp(prefix='rgk-'))
        self.addCleanup(shutil.rmtree, self.dir)
        self.cfg = self.dir / 'cfg'
        self.cfg.mkdir()
        (self.cfg / 'hud_guidance').write_text('1')
        self.marker = self.dir / 'marker'
        self.log = self.dir / 'rghook.log'
        self.slog = self.dir / 'slog'
        self.slog.write_text('')

    def start(self):
        env = dict(os.environ, RGHOOK_KOMBI='127.0.0.1:%d' % self.k.port, RGHOOK_CFG=str(self.cfg), RGHOOK_MARKER=str(self.marker),
                   RGHOOK_NO_WATCH='1', RGHOOK_PPS=str(self.dir / 'routeguidance'), RGHOOK_LOG=str(self.log),
                   RGHOOK_SLOG=str(self.slog), RGHOOK_SLOG_SETTLE_MS='0')
        self.p = subprocess.Popen([str(self.exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, env=env)
        self.addCleanup(self.stop)

    def stop(self):
        self.p.kill()
        self.p.wait()
        self.p.stdin.close()
        self.p.stdout.close()

    def send(self, *lines):
        for l in lines:
            self.p.stdin.write(l + '\n')
            self.p.stdin.flush()
            self.assertEqual(self.p.stdout.readline().strip(), 'ok')

    def msg(self, mid, payload):
        self.send('msg %x %s' % (mid, payload.hex()))

    def wait_requests(self, n, timeout=5.0):
        t = time.time() + timeout
        got = []
        while time.time() < t:
            got = list(self.k.requests)
            if len(got) >= n:
                time.sleep(0.3)
                return self.k.take()
            time.sleep(0.05)
        return self.k.take()

    def logtext(self):
        return self.log.read_text() if self.log.exists() else ''

    @staticmethod
    def text_at_bom(b):
        i = b.index(b'\xfe\xff', 18)
        end = i + b[i - 1]
        return b[i + 2:end - 2].decode('utf-16-be'), b[end] if end < len(b) else None

    def test_route_distance_end(self):
        self.start()
        self.msg(0x5202, maneuver(0, 1, 'Main Street'))                  # left turn
        self.msg(0x5201, update())
        reqs = self.wait_requests(97)
        self.assertEqual(len(reqs), 97, self.logtext())
        m0 = [r for r in reqs if r['method'] == 0xA and r['body'][8] == 15 and r['body'][5:8] == b'\0\0\0']
        self.assertEqual({r['body'][33] for r in m0}, {6})
        d0 = [r for r in reqs if r['method'] == 0xA and r['body'][8] == 29 and r['body'][5:8] == b'\0\0\0']
        self.assertEqual(self.text_at_bom(d0[-1]['body']), ('450', 3))
        roads = {self.text_at_bom(r['body'])[0] for r in reqs if r['method'] == 0x3 and r['body'][8] == 3 and r['body'][5:8] == b'\0\0\0'
                 and len(r['body']) > 9}
        self.assertEqual(roads, {'Main Street'})
        self.assertEqual(self.marker.read_text().strip(), str(self.p.pid))
        self.assertIn('kombi: CarPlay directions to the Ethernet KOMBI', self.logtext())

        self.msg(0x5201, update(metres=400, text='400 m'))
        reqs = self.wait_requests(1)
        self.assertEqual([r['method'] for r in reqs], [0xA])
        self.assertEqual(self.text_at_bom(reqs[0]['body'])[0], '400')

        self.msg(0x5201, update(state=3))                                 # Loading: the arrow stays
        time.sleep(1.2)
        self.assertEqual(self.k.take(), [])
        self.msg(0x5201, update(state=2))                                 # Arrived: the factory cancel
        reqs = self.wait_requests(6)
        self.assertEqual([r['session'] for r in reqs], list(range(400, 406)))
        self.assertFalse(self.marker.exists())

    def test_switched_off(self):
        self.start()
        self.msg(0x5202, maneuver(0, 2, 'Oak Road'))
        self.msg(0x5201, update())
        self.assertEqual(len(self.wait_requests(97)), 97)
        (self.cfg / 'hud_guidance').unlink()                              # "CarPlay directions in HUD" unticked
        self.assertEqual(len(self.wait_requests(6)), 6)
        self.assertFalse(self.marker.exists())

    def test_left_over_arrow_cleared_first(self):
        self.marker.write_text('4242\n')                                  # an earlier mm-ipod ended with an arrow shown
        self.start()
        self.msg(0x5202, maneuver(0, 1, 'Main Street'))
        self.msg(0x5201, update())
        reqs = self.wait_requests(6 + 97)
        self.assertEqual([r['session'] for r in reqs[:6]], list(range(400, 406)))
        self.assertEqual(len(reqs), 6 + 97)
        self.assertIn('arrow of an earlier session (pid 4242) cleared', self.logtext())
        self.assertEqual(self.marker.read_text().strip(), str(self.p.pid))


    def bmw(self, *lines):
        with self.slog.open('a') as f:
            for l in lines:
                f.write('Nov 13 10:00:00    2 19 0 NavNBTEvoController: %s\n' % l)

    def test_bmw_route_wins(self):
        self.start()
        self.msg(0x5202, maneuver(0, 1, 'Main Street'))
        self.msg(0x5201, update())
        self.assertEqual(len(self.wait_requests(97)), 97)
        self.bmw('RouteGuidanceStarted', 'NaviRouteVisibleInMap sourceHandle = 12', 'disable route: routeId 11')   # old preview
        time.sleep(1.2)
        self.msg(0x5201, update(metres=300, text='300 m'))
        time.sleep(1.2)
        self.assertEqual(self.k.take(), [])                               # no cancel, no update: the BMW graph replaces ours
        self.assertFalse(self.marker.exists())
        self.assertIn('BMW navigation route: CarPlay arrows step aside', self.logtext())
        self.bmw('disable route: routeId 12')                             # the BMW route ended
        reqs = self.wait_requests(97)
        self.assertEqual(len(reqs), 97)                                   # back with a whole graph
        d0 = [r for r in reqs if r['method'] == 0xA and r['body'][8] == 29 and r['body'][5:8] == b'\0\0\0']
        self.assertEqual(self.text_at_bom(d0[-1]['body'])[0], '300')
        self.assertIn('BMW route ended: CarPlay arrows back', self.logtext())

    def test_new_carplay_route_takes_over(self):
        self.start()
        self.bmw('RouteGuidanceStarted', 'NaviRouteVisibleInMap sourceHandle = 7')
        self.msg(0x5202, maneuver(0, 2, 'Oak Road'))
        self.msg(0x5201, update(state=2))                                 # no CarPlay route yet
        time.sleep(1.2)
        self.assertEqual(self.k.take(), [])
        self.msg(0x5201, update())                                        # the driver starts one in CarPlay
        self.assertEqual(len(self.wait_requests(97)), 97)
        self.assertIn('BMW-route yield released: a new CarPlay route', self.logtext())


if __name__ == '__main__':
    unittest.main()
