"""The HUD maneuver pictures (src/hudshapes.h, on Mapbox's drawings in hudglyphs.h), built for the Mac: every iAP2 /
CPManeuverType type 0..53 draws something the HUD terminal accepts (filled convex pieces of 3..8 points, at most 64 for a maneuver) inside
the picture box, and the roundabout details hold: thin roads for the exits passed (2..4), the exit number from exit 5 clear of the
drawing and never mirrored, left-hand traffic mirrored; the lane strip (addLanes) under the picture made smaller. Also that hudglyphs.h is
what tools/hud_glyphs.py generates (with MAPBOX_STYLEKIT pointing at ManeuversStyleKit.swift of mapbox-navigation-ios v0.40.0)."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "hudshapes.h"
int main(int argc, char **argv) {          // type angle side [jtype a1 a2 ...]: "tone x,y x,y ..." per piece
  int ang[8]; unsigned n = 0, jt = 0;
  if (argc > 4) { jt = atoi(argv[4]); for (int i = 5; i < argc && n < 8; i++) ang[n++] = atoi(argv[i]); }
  static hud::Shape s; s = hud::shapeOf(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), jt, n ? ang : 0, n);
  if (getenv("HUD_HIGH")) hud::allTone(s, 0, 2);                     // the blink's bright phase
  else if (const char *f = getenv("HUD_FILL")) hud::applyFill(s, atoi(f));   // the progress fill, level 0..8
  if (const char *ls = getenv("HUD_LANES")) {   // "status/highlight/a,a;..." as rghook.c publishes them
    hud::Lane L[8]; unsigned nl = 0;
    for (const char *q = ls; *q && nl < 8; ) {
      hud::Lane &l = L[nl++]; l.na = 0; char *e;
      l.status = strtoul(q, &e, 10); q = e + 1; l.hl = strtol(q, &e, 10); q = e + 1;
      while (*q && *q != ';') { long v = strtol(q, &e, 10); if (e == q) break; if (l.na < 4) l.ang[l.na++] = (int)v; q = e; if (*q == ',') q++; }
      if (*q == ';') q++;
    }
    hud::addLanes(s, L, nl, atoi(argv[3]) == 1);
  }
  bool mini = getenv("HUD_MINI") != 0;     // as hud.h sends them to the MINI cluster (toMini), pieces folded flat left out
  if (getenv("HUD_MERGE")) {               // the whole polygons hud.h sends (always, unless hud_convex)
    static hud::Polys m; hud::mergePieces(s, m);
    for (unsigned i = 0; i < m.n; i++) {
      static hud::Pt q[hud::kPolyPts]; unsigned k = m.q[i].n; for (unsigned j = 0; j < k; j++) q[j] = m.q[i].p[j];
      if (mini && (k = hud::toMiniPoly(q, k)) < 3) continue;
      printf("%d", m.q[i].t); for (unsigned j = 0; j < k; j++) printf(" %d,%d", q[j].x, q[j].y); printf("\n");
    }
    return 0;
  }
  for (unsigned i = 0; i < s.n; i++) {
    hud::Pt q[8]; unsigned k = s.k[i]; for (unsigned j = 0; j < k; j++) q[j] = s.p[i][j];
    if (mini && (k = hud::toMini(q, k)) < 3) continue;
    printf("%d", s.t[i]); for (unsigned j = 0; j < k; j++) printf(" %d,%d", q[j].x, q[j].y); printf("\n");
  }
}
'''
ANGLES = range(-165, 181, 15)


class HudShapes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('clang++'):
            raise unittest.SkipTest('clang++ not found')
        cls.tmp = tempfile.TemporaryDirectory(prefix='hudshapes-')
        d = Path(cls.tmp.name)
        (d / 'h.cpp').write_text(HARNESS)
        cls.exe = d / 'h'
        subprocess.run(['clang++', '-std=c++11', '-Wall', '-Werror', '-I', str(ROOT / 'src'), '-o', str(cls.exe), str(d / 'h.cpp')],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def shape(self, t, angle=0, side=0, jtype=None, angles=(), mini=False, lanes=None, fill=None, high=False, merge=False):
        args = [str(self.exe), str(t), str(angle), str(side)] + ([str(jtype)] + [str(a) for a in angles] if jtype is not None else [])
        env = {k: v for k, v in os.environ.items() if k not in ('HUD_MINI', 'HUD_LANES', 'HUD_FILL', 'HUD_HIGH', 'HUD_MERGE')}
        if mini:
            env['HUD_MINI'] = '1'
        if lanes is not None:
            env['HUD_LANES'] = lanes
        if fill is not None:
            env['HUD_FILL'] = str(fill)
        if high:
            env['HUD_HIGH'] = '1'
        if merge:
            env['HUD_MERGE'] = '1'
        out = []
        for line in subprocess.run(args, capture_output=True, text=True, check=True, env=env).stdout.splitlines():
            p = line.split()
            out.append((int(p[0]), [tuple(int(c) for c in q.split(',')) for q in p[1:]]))
        return out

    @staticmethod
    def convex(pts):
        sign = 0
        for i in range(len(pts)):
            (x1, y1), (x2, y2), (x3, y3) = pts[i], pts[(i + 1) % len(pts)], pts[(i + 2) % len(pts)]
            c = (x2 - x1) * (y3 - y2) - (y2 - y1) * (x3 - x2)
            if c:
                if sign and (c > 0) != (sign > 0):
                    return False
                sign = c
        return True

    @staticmethod
    def overlap(a, b):
        """two convex polygons share area (separating axis; touching edges do not count)"""
        for poly in (a, b):
            for i in range(len(poly)):
                (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % len(poly)]
                nx, ny = y1 - y2, x2 - x1
                if not nx and not ny:
                    continue
                pa = [nx * x + ny * y for x, y in a]
                pb = [nx * x + ny * y for x, y in b]
                if max(pa) <= min(pb) or max(pb) <= min(pa):
                    return False
        return True

    @staticmethod
    def mirrored(pieces):
        return [(t, [(240 - x, y) for x, y in pts]) for t, pts in pieces]

    def dim(self, pieces):
        return len([p for p in pieces if p[0] == 1])

    def test_every_type(self):
        for t in range(54):
            for side in (0, 1):
                for angle in (0, 90, -90, 170, -45):
                    with self.subTest(t=t, side=side, angle=angle):
                        pieces = self.shape(t, angle, side)
                        self.assertTrue(1 <= len(pieces) <= 64)
                        self.assertTrue(any(tone == 0 for tone, _ in pieces))          # something in the route colour
                        for tone, pts in pieces:
                            self.assertIn(tone, (0, 1))
                            self.assertTrue(3 <= len(pts) <= 8)
                            self.assertTrue(self.convex(pts), pts)
                            for x, y in pts:                                         # the 6WA box
                                self.assertTrue(68 <= x <= 172 and 18 <= y <= 122, (x, y))

    def test_roundabouts_every_angle(self):
        for n in range(1, 20):
            for side in (0, 1):
                for a in ANGLES:
                    with self.subTest(exit=n, side=side, angle=a):
                        pieces = self.shape(27 + n, a, side)
                        self.assertLessEqual(len(pieces), 64)
                        for tone, pts in pieces:
                            self.assertTrue(self.convex(pts), pts)
                            for x, y in pts:
                                self.assertTrue(68 <= x <= 172 and 18 <= y <= 122, (x, y))

    def test_mini_cluster_pieces(self):
        # halved to the MINI's pixels (hud.h polygon -> toMini): still convex, never flat, inside its picture area, and nothing large lost
        cases = [(t, a, side) for t in range(54) for a in (0, 90, -90, -135) for side in (0, 1)]
        cases += [(27 + n, a, side) for n in (1, 3, 4, 7, 12, 19) for a in ANGLES for side in (0, 1)]
        for t, a, side in cases:
            with self.subTest(t=t, angle=a, side=side):
                box, mini = self.shape(t, a, side), self.shape(t, a, side, mini=True)
                self.assertGreaterEqual(len(mini), len(box) * 3 // 4)
                for tone, pts in mini:
                    self.assertTrue(3 <= len(pts) <= 8)
                    self.assertEqual(len(set(pts)), len(pts))
                    self.assertTrue(self.convex(pts), pts)
                    self.assertTrue(any((pts[i][0] - pts[0][0]) * (pts[i + 1][1] - pts[0][1]) - (pts[i][1] - pts[0][1]) * (pts[i + 1][0] - pts[0][0])
                                        for i in range(1, len(pts) - 1)), pts)
                    for x, y in pts:
                        self.assertTrue(34 <= x <= 86 and 14 <= y <= 66, (x, y))

    def test_passed_exits(self):
        for a in ANGLES:
            base = self.dim(self.shape(28, a))                                       # exit 1: the ring alone
            arc = min(300, max(60, (180 - a) % 360 or 360))                          # the way round, as drawn
            for n in (2, 3, 4):
                if arc < 30 * n:                                                     # exits at least 30 degrees apart
                    continue
                with self.subTest(exit=n, angle=a):
                    self.assertEqual(self.dim(self.shape(27 + n, a)) - base, n - 1)   # the exits passed before the one taken
        # with the iPhone's angles, those are drawn instead (the taken exit and the entry left out)
        self.assertEqual(self.dim(self.shape(30, -90, 0, 1, (90, 0, -90, 180))) - self.dim(self.shape(28, -90)), 2)

    def test_exit_number(self):
        for n in range(5, 20):
            for side in (0, 1):
                for a in ANGLES:
                    with self.subTest(exit=n, side=side, angle=a):
                        ring, pieces = self.shape(28, a, side), self.shape(27 + n, a, side)
                        self.assertEqual(pieces[:len(ring)], ring)                   # no thin roads, the same drawing
                        number = pieces[len(ring):]
                        self.assertTrue(number and all(t == 0 for t, _ in number))
                        for _, d in number:
                            for x, _y in d:
                                self.assertTrue(74 <= x <= 166, x)                   # where the MINI shows it
                            for _, r in ring:
                                self.assertFalse(self.overlap(d, r), (d, r))
        # digits read the same in both driving sides: the number is drawn after the mirror image
        r, l = self.shape(41, -90, 0), self.shape(41, 90, 1)
        nr, nl = r[len(self.shape(28, -90, 0)):], l[len(self.shape(28, 90, 1)):]
        self.assertEqual([[(x - min(p[0] for _, q in nr for p in q), y) for x, y in pts] for _, pts in nr],
                         [[(x - min(p[0] for _, q in nl for p in q), y) for x, y in pts] for _, pts in nl])

    def test_left_hand_traffic_mirrors(self):
        for t in (4, 18, 26, 9):                                                     # U-turns and on-ramps change side
            with self.subTest(t=t):
                self.assertEqual(self.mirrored(self.shape(t, 0, 0)), self.shape(t, 0, 1))
        for t in (1, 2, 13, 14, 22, 23, 24, 25):                                     # a left turn is a left turn
            with self.subTest(t=t):
                self.assertEqual(self.shape(t, 0, 0), self.shape(t, 0, 1))
        for t, a in ((28, 90), (30, -90), (31, 45), (19, 0), (6, 0)):
            with self.subTest(t=t, angle=a):
                self.assertEqual(self.mirrored(self.shape(t, a, 0)), self.shape(t, -a, 1))   # roundabouts run clockwise

    def test_types_with_their_own_picture(self):
        straight = self.shape(3)
        for t in (11, 15, 16, 17, 18, 19, 20, 21, 24, 25, 10, 12, 27, 8, 9, 13, 14):
            with self.subTest(t=t):
                self.assertNotEqual(self.shape(t), straight)

    def test_turns_by_angle(self):
        self.assertEqual(self.shape(0, 0), self.shape(3))
        self.assertEqual(self.shape(5, 90), self.shape(2))                           # follow the road round a bend
        self.assertEqual(self.shape(0, -45), self.shape(49))
        self.assertEqual(self.shape(0, 140), self.shape(48))

    # lane strings as rghook.c publishes them: status/highlight/angles, left to right
    LANES = ['0/1000/0;1/1000/0;2/90/0,90',                                         # straight, straight, straight or right (take right)
             '2/-90/-90;0/1000/0;0/1000/0',
             '1/1000/-90,0;1/0/0;1/0/0,45;0/1000/90',
             '2/1000/180',                                                          # one U-turn lane
             ';'.join(['1/0/0'] * 4 + ['0/1000/0'] * 4),                            # eight lanes
             '1/-45/-45,0;2/135/135;0/1000/-135,-90']

    def test_division(self):
        # udiv (no division instruction on the head unit) over all 31 bits (addLaneGlyph divides w * 1024)
        src = r'''
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "hudshapes.h"
int main(int argc, char **argv) { printf("%d\n", hud::udiv(atoi(argv[1]), atoi(argv[2]))); return 0; }
'''
        d = Path(self.tmp.name)
        (d / 'div.cpp').write_text(src)
        subprocess.run(['clang++', '-std=c++11', '-Wall', '-Werror', '-I', str(ROOT / 'src'), '-o', str(d / 'div'), str(d / 'div.cpp')],
                       check=True)
        run = lambda *a: int(subprocess.run([str(d / 'div')] + [str(x) for x in a], capture_output=True, text=True, check=True).stdout)  # noqa: E731
        for a, b in ((0, 7), (359, 40), (65535, 3), (65536, 3), (90112, 1020), (88 * 1024, 89), (2 ** 31 - 1, 1), (2 ** 31 - 1, 12345)):
            with self.subTest(a=a, b=b):
                self.assertEqual(run(a, b), a // b)

    def test_lanes(self):
        for lanes in self.LANES:
            for t, a, side in ((2, 0, 0), (34, -90, 0), (41, 90, 1), (12, 0, 0), (0, 0, 0)):
                with self.subTest(lanes=lanes, t=t):
                    plain, pieces = self.shape(t, a, side), self.shape(t, a, side, lanes=lanes)
                    self.assertLessEqual(len(pieces), 192)                              # kMaxPieces
                    n = lanes.count(';') + 1
                    picture, strip = [p for p in pieces if max(y for _, y in p[1]) <= 83], [p for p in pieces if min(y for _, y in p[1]) >= 84]
                    self.assertEqual(len(picture) + len(strip), len(pieces))            # the picture above, the lanes below, nothing between
                    self.assertGreaterEqual(len(picture), len(plain) * 3 // 4)          # the picture made smaller, not lost
                    self.assertGreaterEqual(len(strip), n)                              # every lane drawn
                    self.assertLessEqual(len(strip), 19 * n)                            # Mapbox's lane drawings: at most 19 pieces
                    for tone, pts in pieces:
                        self.assertTrue(3 <= len(pts) <= 8)
                        self.assertTrue(self.convex(pts), pts)
                    for tone, pts in strip:
                        for x, y in pts:
                            self.assertTrue(74 <= x <= 166 and 84 <= y <= 119, (x, y))  # where the MINI shows it
                    good = any(int(l.split('/')[0]) for l in lanes.split(';'))
                    self.assertEqual(any(t == 0 for t, _ in strip), good)               # the lanes to take in the route colour
                    seen = set()                                                        # the context tone never over the route tone
                    for tone, pts in strip:
                        if tone == 0:
                            seen.add(tuple(pts))
                        else:
                            self.assertFalse(seen, 'context-tone lane piece after a route-tone one')
                    mini = self.shape(t, a, side, lanes=lanes, mini=True)
                    for tone, pts in mini:
                        for x, y in pts:
                            self.assertTrue(34 <= x <= 86 and 14 <= y <= 66, (x, y))
                    self.assertGreaterEqual(len([p for p in mini if min(y for _, y in p[1]) >= 47]), n)   # each lane still there in pixels

    def test_lanes_follow_the_road_not_the_driving_side(self):
        lanes = '2/-90/-90;0/1000/0;0/1000/0'
        self.assertEqual(self.shape(12, 0, 0, lanes=lanes)[-12:], self.shape(12, 0, 1, lanes=lanes)[-12:])
        left, right = self.shape(12, lanes='1/-90/-90'), self.shape(12, lanes='1/90/90')
        strip = lambda ps: [p for p in ps if min(y for _, y in p[1]) >= 84]
        self.assertEqual(sorted(sorted((240 - x, y) for x, y in p) for _, p in strip(left)), sorted(sorted(p) for _, p in strip(right)))
        # a U-turn lane hooks to the driving side's U-turn side: left in right-hand traffic, right in left-hand traffic
        rht, lht = strip(self.shape(12, 0, 0, lanes='2/1000/180')), strip(self.shape(12, 0, 1, lanes='2/1000/180'))
        self.assertEqual(sorted(sorted((240 - x, y) for x, y in p) for _, p in rht), sorted(sorted(p) for _, p in lht))
        head = lambda ps: min((p for _, p in ps if len(p) == 3), key=lambda p: -max(y for _, y in p))   # the head pointing down
        self.assertLess(sum(x for x, _ in head(rht)) / 3, 120)

    def test_glyphs_generated(self):
        swift = Path(os.environ.get('MAPBOX_STYLEKIT', '/nonexistent'))   # ManeuversStyleKit.swift, mapbox-navigation-ios v0.40.0
        if not swift.exists():
            self.skipTest('set MAPBOX_STYLEKIT to ManeuversStyleKit.swift (mapbox-navigation-ios v0.40.0)')
        out = Path(self.tmp.name) / 'hudglyphs.h'
        subprocess.run(['python3', str(ROOT / 'tools/hud_glyphs.py'), str(swift), str(out)], check=True, capture_output=True)
        self.assertEqual(out.read_text(), (ROOT / 'src/hudglyphs.h').read_text())


if __name__ == '__main__':
    unittest.main()


def raster(pieces, x0=68, x1=172, y0=18, y1=122):
    """the colour (tone) each sample point ends up with, pieces drawn in order, each filled even-odd; None = nothing"""
    out = {}
    for tone, pts in pieces:
        n = len(pts)
        for py in range(y0, y1):
            y = py + 0.5
            xs = []
            for i in range(n):
                (ax, ay), (bx, by) = pts[i], pts[(i + 1) % n]
                if (ay <= y) != (by <= y):
                    xs.append(ax + (y - ay) * (bx - ax) / (by - ay))
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                for px in range(max(x0, int(a + 0.5)), min(x1, int(b + 0.5) + 1)):
                    if a <= px + 0.5 <= b:
                        out[(px, py)] = tone
    return out


CASES = [(t, a, s) for t in (1, 2, 3, 4, 9, 10, 12, 13, 22, 23, 47, 48, 49, 50, 15) for a in (0, 90) for s in (0, 1)]
CASES += [(27 + n, a, s) for n in (1, 2, 3, 4, 7, 12) for a in (90, 0, -90, -150) for s in (0, 1)] + [(19, 0, 0), (6, 45, 1)]
LANES = '0/1000/0;1/-90/-90,0;2/0/0;0/1000/90'


class HudEffects(unittest.TestCase):
    """HUD effects hud_fill / hud_blink and the whole polygons (hudshapes.h applyFill, allTone, mergePieces)"""
    setUpClass = classmethod(HudShapes.setUpClass.__func__)
    tearDownClass = classmethod(HudShapes.tearDownClass.__func__)
    shape = HudShapes.shape
    convex = staticmethod(HudShapes.convex)

    def test_fill_keeps_the_picture(self):
        for t, a, side in CASES[::3]:
            base = raster(self.shape(t, a, side))
            for level in range(0, 9):
                with self.subTest(t=t, angle=a, side=side, level=level):
                    pieces = self.shape(t, a, side, fill=level)
                    for tone, pts in pieces:
                        self.assertTrue(3 <= len(pts) <= 10)
                        self.assertTrue(self.convex(pts), pts)
                    r = raster(pieces)
                    # the same area drawn, only the route tone changes; the cut point is rounded to whole units, so a sample
                    # beside the cut may differ
                    diff = [k for k in set(r) | set(base) if (r.get(k) == 1) != (base.get(k) == 1) or (k in r) != (k in base)]
                    self.assertLessEqual(len(diff), max(3, len(base) // 50), diff)
                    filled = [k[1] for k, tone in r.items() if tone == 2]
                    route = [k[1] for k, tone in r.items() if tone == 0]
                    if level == 0:
                        self.assertFalse(filled)
                    if level == 8:
                        self.assertFalse(route)
                    if filled and route:
                        self.assertLessEqual(max(route), min(filled) + 2)                # from the bottom up

    def test_fill_grows(self):
        for t, a, side in ((2, 0, 0), (4, 0, 0), (30, -90, 0), (13, 0, 0)):
            sizes = [sum(1 for v in raster(self.shape(t, a, side, fill=lv)).values() if v == 2) for lv in range(9)]
            self.assertEqual(sizes, sorted(sizes), (t, sizes))
            self.assertGreater(sizes[4], 0)

    def test_blink_bright_phase(self):
        for t, a, side in CASES[::5]:
            with self.subTest(t=t, angle=a, side=side):
                base, high = raster(self.shape(t, a, side)), raster(self.shape(t, a, side, high=True))
                self.assertEqual(set(base), set(high))
                self.assertEqual({k for k, v in base.items() if v == 0}, {k for k, v in high.items() if v == 2})

    def test_lanes_never_fill(self):
        lanes = [k for k, v in raster(self.shape(2, 90, 0, lanes=LANES)).items()]
        filled = raster(self.shape(2, 90, 0, lanes=LANES, fill=8))
        strip = {k: v for k, v in filled.items() if k[1] >= 84}
        self.assertTrue(strip and all(v != 2 for v in strip.values()))
        self.assertEqual(set(filled), set(lanes))

    def test_whole_polygons_draw_the_same(self):
        worst = 0
        for t, a, side in CASES:
            for kw in ({}, {'fill': 3}, {'high': True}, {'lanes': LANES}):
                with self.subTest(t=t, angle=a, side=side, **kw):
                    pieces, whole = self.shape(t, a, side, **kw), self.shape(t, a, side, merge=True, **kw)
                    self.assertLessEqual(len(whole), len(pieces))
                    for tone, pts in whole:
                        self.assertTrue(3 <= len(pts) <= 96)
                    rp, rw = raster(pieces), raster(whole)
                    diff = sum(1 for k in set(rp) | set(rw) if rp.get(k) != rw.get(k))
                    worst = max(worst, diff / max(1, len(rp)))
                    self.assertLess(diff, max(4, len(rp) // 100), (diff, len(rp)))       # edge samples at most
        print('whole polygons: worst difference %.2f %%' % (worst * 100))

    def test_whole_polygons_are_few(self):
        counts = {}
        for t, a in ((3, 0), (2, 0), (1, 0), (47, 0), (4, 0), (13, 0), (22, 0), (9, 0), (12, 0), (28, 90), (30, -90), (15, 0)):
            counts[t, a] = (len(self.shape(t, a)), len(self.shape(t, a, merge=True)))
        print('pieces -> whole polygons:', counts)
        for (t, a), (p, w) in counts.items():
            self.assertLessEqual(w, p, (t, p, w))
        for t in (2, 1, 3, 12):                                                    # Mapbox's filled arrows join up (strokes stay quads)
            self.assertLessEqual(counts[t, 0][1], 4, (t, counts[t, 0]))

    def test_whole_polygons_on_the_mini(self):
        for t, a, side in CASES:
            with self.subTest(t=t, angle=a, side=side):
                whole = self.shape(t, a, side, merge=True, mini=True)
                self.assertTrue(whole)
                for tone, pts in whole:
                    self.assertTrue(3 <= len(pts) <= 96)
                    for i in range(len(pts)):
                        self.assertNotEqual(pts[i], pts[(i + 1) % len(pts)])
                    for x, y in pts:
                        self.assertTrue(34 <= x <= 86 and 14 <= y <= 66, (x, y))
