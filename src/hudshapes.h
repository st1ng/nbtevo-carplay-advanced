// hudshapes.h — the HUD maneuver pictures (hud.h draws them on the cluster's HUDTerminal), in Mapbox's maneuver style.
//
// The drawings are Mapbox's: hudglyphs.h, generated from ManeuversStyleKit.swift (mapbox-navigation-ios
// v0.40.0, ISC; drawn from Maneuvers.pcvd, CC0). The roundabout is Mapbox's parametric drawRoundabout, computed here for any exit: the
// ring in the context tone, the way round and the exit arrow in the route tone, the composition shifted away from the arrow as Mapbox
// does. Additions in the same style: thin roads on the ring for the exits passed before exit 2..4 (or the junction's roads when the
// iPhone sends them), the exit number beside the ring from exit 5 on, and a ferry. Left-hand traffic is the mirror image (roundabouts
// clockwise, U-turns to the right); digits are never mirrored.
//
// Pieces: filled convex polygons (the terminal's type 3), 3..8 points, at most kMaxPieces per picture, in the 6WA box (70,20)-(170,120),
// y down; hud.h maps that box to the MINI cluster's area. Pure code (no libc but memset), so the Mac renders and tests every type
// (tests/test_hud_shapes.py). No floating point and no division by a variable (the head unit's libc has no __aeabi_idiv): angles in
// degrees with a sine table, Mapbox's 32-unit canvas in 1/16 units.
//
// Maneuver types: iAP2 RouteGuidanceManeuverType 0..53 = CPManeuverType (Apple, CarPlay Developer Guide, "Share upcoming maneuvers with
// vehicle", iOS 17.4), numbered as in the community iAP2 layout (github.com/WaylandAce/mib2q-carplay-rgi, rgd_tlv.h) and confirmed in the
// car. Angles: 0 = straight on, positive = clockwise (right), as the iPhone's exit angle. Driving side: 0 right-hand traffic, 1 left-hand.
#pragma once
#include "hudglyphs.h"

namespace hud {

typedef short s16;
typedef unsigned u32;
struct Pt {
  s16 x, y;
};

const s16 kSin5[19] = {
  0, 89, 178, 265, 350, 433, 512, 587, 658, 724, 784, 839, 887, 928, 962, 989, 1008, 1020, 1024
};  // sin x 1024
inline int sinQ(int d) {
  int v = kSin5[d / 5];
  int fr = d % 5;
  if (fr)
    v += (kSin5[d / 5 + 1] - kSin5[d / 5]) * fr / 5;
  return v;
}  // 0..90
inline int isin(int d) {
  while (d < 0)
    d += 360;
  while (d >= 360)
    d -= 360;
  if (d <= 90)
    return sinQ(d);
  if (d <= 180)
    return sinQ(180 - d);
  if (d <= 270)
    return -sinQ(d - 180);
  return -sinQ(360 - d);
}
inline int icos(int d) {
  return isin(d + 90);
}
// a / b for a >= 0, b > 0, by shift and subtract: clang turns a counting loop back into a division (__aeabi_uidiv, not in the head
// unit's libc). All 31 bits: addLaneGlyph divides w * 1024.
inline int udiv(int a, int b) {
  unsigned q = 0, r = 0;
  for (int i = 30; i >= 0; i--) {
    r = (r << 1) | (((unsigned)a >> i) & 1u);
    if (r >= (unsigned)b) {
      r -= (unsigned)b;
      q |= 1u << i;
    }
  }
  return (int)q;
}

// tone: 0 the route (fill colour 4, white), 1 the context (3, grey on the F54): two colours in one picture, as the BMW navigation's own
// HUD pictures. A tone 1 piece over tone 0 pieces shows grey there (the pin's hole, the ferry's windows).
const u32 kMaxPieces = 192;  // a maneuver takes at most 64, the lanes (addLanes, Mapbox's lane drawings) up to 8 x 19 more; whole
                             // polygons (mergePieces) join them back into a few MOST calls a lane
const u32 kPiecePts = 10;   // 8 from add(); the progress fill (applyFill) cuts a piece, one point more
struct Shape {
  u32 n, tone;
  Pt p[kMaxPieces][kPiecePts];
  u32 k[kMaxPieces];
  unsigned char t[kMaxPieces];
};
// the convex hull of k <= kPiecePts points, in place (monotone chain): rounding a turned piece to whole units can dent it, the terminal fills
// convex pieces only. Collinear and repeated points go. Returns the points left.
inline int cross(Pt o, Pt a, Pt b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}
inline u32 hull(Pt *p, u32 k) {
  for (u32 i = 1; i < k; i++) {
    Pt v = p[i];
    u32 j = i;
    while (j && (p[j - 1].x > v.x || (p[j - 1].x == v.x && p[j - 1].y > v.y))) {
      p[j] = p[j - 1];
      j--;
    }
    p[j] = v;
  }
  Pt h[2 * kPiecePts + 1];
  u32 m = 0;
  for (u32 i = 0; i < k; i++) {
    while (m >= 2 && cross(h[m - 2], h[m - 1], p[i]) <= 0)
      m--;
    h[m++] = p[i];
  }
  for (int i = (int)k - 2, lo = (int)m + 1; i >= 0; i--) {
    while ((int)m >= lo && cross(h[m - 2], h[m - 1], p[i]) <= 0)
      m--;
    h[m++] = p[i];
  }
  if (m)
    m--;  // the last point is the first again
  for (u32 i = 0; i < m; i++)
    p[i] = h[i];
  return m;
}
// a piece in the MINI cluster's picture area: the box at half size, at (35,15)-(85,65). Halving to whole pixels can dent a piece or fold
// it flat, so the hull is taken again; fewer than 3 points left = nothing to draw.
inline u32 toMini(Pt *p, u32 k) {
  for (u32 i = 0; i < k; i++) {
    p[i].x = (s16)(35 + ((p[i].x - 70) >> 1));
    p[i].y = (s16)(15 + ((p[i].y - 20) >> 1));
  }
  return hull(p, k);
}
inline void add(Shape &s, const Pt *q, u32 k) {
  if (s.n >= kMaxPieces || k < 3 || k > 8)
    return;
  Pt h[8];
  for (u32 i = 0; i < k; i++)
    h[i] = q[i];
  k = hull(h, k);
  if (k < 3)
    return;
  for (u32 i = 0; i < k; i++)
    s.p[s.n][i] = h[i];
  s.t[s.n] = (unsigned char)s.tone;
  s.k[s.n++] = k;
}
inline void addRect(Shape &s, int x0, int y0, int x1, int y1) {
  Pt q[4] = { { (s16)x0, (s16)y0 }, { (s16)x1, (s16)y0 }, { (s16)x1, (s16)y1 }, { (s16)x0, (s16)y1 } };
  add(s, q, 4);
}
inline void mirror(Shape &s, u32 from = 0) {
  for (u32 i = from; i < s.n; i++)
    for (u32 j = 0; j < s.k[i]; j++)
      s.p[i][j].x = (s16)(240 - s.p[i][j].x);
}

// a generated Mapbox drawing, or its mirror image
inline void addGlyph(Shape &s, const glyph::Piece *g, u32 n, bool mirrored = false) {
  u32 from = s.n, was = s.tone;
  for (u32 i = 0; i < n; i++) {
    Pt q[8];
    for (u32 j = 0; j < g[i].n && j < 8; j++)
      q[j] = { g[i].p[j][0], g[i].p[j][1] };
    s.tone = g[i].tone;
    add(s, q, g[i].n);
  }
  s.tone = was;
  if (mirrored)
    mirror(s, from);
}
#define HUD_GLYPH(name) glyph::k##name, glyph::k##name##N

// ---- Mapbox's canvas in 1/16 units (0..512 = 32 units), y down -> the box (100 units per 32: 25/128 per 1/16 unit) --------------------
inline s16 boxX(int v) {
  return (s16)(70 + ((v * 25 + 64) >> 7));
}
inline s16 boxY(int v) {
  return (s16)(20 + ((v * 25 + 64) >> 7));
}
struct P16 {
  int x, y;
};
// the point r along deg (0 up, clockwise) from c, and w to the right of that direction
inline P16 polar(P16 c, int deg, int r, int w = 0) {
  return { c.x + (r * isin(deg) + w * isin(deg + 90)) / 1024, c.y - (r * icos(deg) + w * icos(deg + 90)) / 1024 };
}
inline void quad16(Shape &s, P16 a, P16 b, P16 c, P16 d) {
  Pt q[4] = { { boxX(a.x), boxY(a.y) }, { boxX(b.x), boxY(b.y) }, { boxX(c.x), boxY(c.y) }, { boxX(d.x), boxY(d.y) } };
  add(s, q, 4);
}
inline void rect16(Shape &s, int x0, int y0, int x1, int y1) {
  quad16(s, { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 });
}
// a band of the circle (radius r, width w) from angle a0 to a1, either way round, in equal pieces of at most 40 degrees: the rim in steps
// of at most 20, the inner edge one chord (so each piece is convex)
inline void arc(Shape &s, P16 c, int r, int w, int a0, int a1) {
  int span = a1 > a0 ? a1 - a0 : a0 - a1, dir = a1 > a0 ? 1 : -1;
  if (!span)
    return;
  int steps = (span + 39) / 40, step = udiv(span, steps), rem = span - step * steps, b0 = a0, ro = r + w / 2, ri = r - w / 2;
  for (int i = 1; i <= steps; i++) {
    int b1 = a0 + dir * (i * step + (i < rem ? i : rem)), d = b1 - b0;
    P16 q[5] = { polar(c, b0, ro), polar(c, b0 + d / 2, ro), polar(c, b1, ro), polar(c, b1, ri), polar(c, b0, ri) };
    Pt o[5];
    u32 k = 0;
    for (u32 j = 0; j < 5; j++)
      if (j != 1 || d > 20 || d < -20)
        o[k++] = { boxX(q[j].x), boxY(q[j].y) };
    add(s, o, k);
    b0 = b1;
  }
}
// a straight road from radius r0 to r1 along deg, w wide
inline void road(Shape &s, P16 c, int deg, int r0, int r1, int w) {
  quad16(s, polar(c, deg, r0, -w / 2), polar(c, deg, r1, -w / 2), polar(c, deg, r1, w / 2), polar(c, deg, r0, w / 2));
}

// ---- the roundabout: Mapbox's drawRoundabout ------------------------------------------------------------------------------------------
// Worked out for right-hand traffic (anticlockwise, from the entry below); shapeOf mirrors it for left-hand traffic.
const int kRoundaboutRadius = 104, kRoundaboutWidth = 64;  // Mapbox: radius 6.5, line 4
const int kRbUturn = -120;        // U-turn at the roundabout: out beside the entry, clear of it
// Mapbox's roundabout_angle: degrees travelled round from the entry to the exit at angle e; 60..300, or the arrow head runs into
// the entry
inline int rbArc(int e) {
  int A = 180 - e;
  while (A <= 0)
    A += 360;
  while (A > 360)
    A -= 360;
  return A < 60 ? 60 : A > 300 ? 300 : A;
}
// the n - 1 exits passed before exit n, evenly round the way travelled
inline u32 passedExits(int *out, u32 n, int A) {
  u32 k = 0;
  if (n < 2)
    return 0;
  int step = udiv(A, (int)n);
  for (u32 i = 1; i < n && k < 8; i++)
    out[k++] = 180 - (int)i * step;
  return k;
}
// a thin road at angle a is drawn: not the exit taken (out), not the entry below
inline bool rbRoad(int a, int out) {
  int d = a - out;
  while (d > 180)
    d -= 360;
  while (d <= -180)
    d += 360;
  return !((d > -20 && d < 20) || a > 160 || a < -160);
}
// the ring centre at k/16 of Mapbox's size: roundabout_x, roundabout_y, the composition shifted away from the arrow
inline P16 rbCentre(int A, int k) {
  return { 256 + 96 * k * isin(A - 180) / 16384, 256 + 80 * k * icos(A - 180) / 16384 };
}
inline bool inPicture(P16 p) {
  return p.x >= 24 && p.x <= 488 && p.y >= 8 && p.y <= 504;
}  // the part of the box the MINI shows
// draws it, with thin roads on the ring at the given angles; returns the ring centre. Mapbox's size, or down to 3/4 of it when a thin
// road would leave the picture (a left exit puts the ring against the right edge, where the exits passed are).
inline P16 roundabout(Shape &s, int e, const int *roads, u32 nroads) {
  int A = rbArc(e), out = 180 - A, k = 16;
  for (; k > 12; k--) {  // the largest size at which every thin road shows 2 units outside the ring
    P16 c = rbCentre(A, k);
    int r = (kRoundaboutRadius + kRoundaboutWidth / 2) * k / 16 + 32;
    bool fit = true;
    for (u32 i = 0; fit && i < nroads; i++)
      if (rbRoad(roads[i], out))
        fit = inPicture(polar(c, roads[i], r, -14)) && inPicture(polar(c, roads[i], r, 14));
    if (fit)
      break;
  }
  P16 c = rbCentre(A, k);
  int R = kRoundaboutRadius * k / 16, W = kRoundaboutWidth * k / 16, ro = R + W / 2;
  u32 was = s.tone;
  s.tone = 1;
  for (u32 i = 0; i < nroads; i++) {
    int a = roads[i];
    if (!rbRoad(a, out))
      continue;
    int r1 = ro + 64;  // 4 units out of the ring, 1.75 wide; shorter where the picture ends
    while (r1 > ro && !(inPicture(polar(c, a, r1, -14)) && inPicture(polar(c, a, r1, 14))))
      r1 -= 8;
    if (r1 >= ro + 16)
      road(s, c, a, ro - 8, r1, 28);
  }
  arc(s, c, R, W, out, -180);  // the ring, the part not driven
  s.tone = 0;
  arc(s, c, R, W, 180, out);  // the way round
  int sw = 32 * k / 16, bottom = k < 16 ? 512 : c.y + 264 > 512 ? 512 : c.y + 264;
  rect16(s, c.x - sw, c.y + 72 * k / 16, c.x + sw, bottom);  // the entry, from below up to the ring's inner edge
  int th = -(A + 90), co = icos(th), si = isin(th);          // the exit arrow, turned out of the ring
  for (u32 i = 0; i < glyph::kRbArrowN; i++) {
    const glyph::Piece &g = glyph::kRbArrow[i];
    Pt q[8];
    for (u32 j = 0; j < g.n && j < 8; j++) {
      int x = g.p[j][0], y = g.p[j][1];
      q[j] = { boxX(c.x + (x * co - y * si) * k / 16384), boxY(c.y + (x * si + y * co) * k / 16384) };
    }
    add(s, q, g.n);
  }
  s.tone = was;
  return c;
}

// ---- the exit number: segment digits 15 x 23 box units (about 7 x 11 pixels on the MINI), strokes 4 -----------------------------------
const int kDW = 15, kDH = 23, kDT = 4, kDGap = 3;
inline void digit(Shape &s, int x, int y, int d) {
  static const unsigned char kSeg[10] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f
  };  // bit 0 a (top) .. bit 6 g (middle)
  unsigned m = kSeg[d];
  int r = x + kDW - kDT;
  int m1 = y + (kDH - kDT) / 2, m2 = m1 + kDT;
  if (m & 0x01)
    addRect(s, x, y, x + kDW, y + kDT);
  if (m & 0x40)
    addRect(s, x, m1, x + kDW, m2);
  if (m & 0x08)
    addRect(s, x, y + kDH - kDT, x + kDW, y + kDH);
  if ((m & 0x06) == 0x06)
    addRect(s, r, y, x + kDW, y + kDH);  // b and c: the whole right side
  else {
    if (m & 0x02)
      addRect(s, r, y, x + kDW, m2);
    if (m & 0x04)
      addRect(s, r, m1, x + kDW, y + kDH);
  }
  if ((m & 0x30) == 0x30)
    addRect(s, x, y, x + kDT, y + kDH);  // f and e: the whole left side
  else {
    if (m & 0x20)
      addRect(s, x, y, x + kDT, m2);
    if (m & 0x10)
      addRect(s, x, m1, x + kDT, y + kDH);
  }
}
inline int numberWidth(u32 n) {
  return n < 10 ? kDW : kDT + kDGap + kDW;
}  // exits go to 19: the tens are a narrow 1
inline void number(Shape &s, u32 n, int x, int y) {
  if (n >= 10) {
    addRect(s, x, y, x + kDT, y + kDH);
    x += kDT + kDGap;
    n -= 10;
  }
  digit(s, x, y, (int)n);
}
// (x, y) inside piece i (convex, either winding)
inline bool inside(const Shape &s, u32 i, int x, int y) {
  int sign = 0;
  for (u32 j = 0; j < s.k[i]; j++) {
    const Pt &a = s.p[i][j], &b = s.p[i][j + 1 < s.k[i] ? j + 1 : 0];
    int c = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
    if (c > 0) {
      if (sign < 0)
        return false;
      sign = 1;
    } else if (c < 0) {
      if (sign > 0)
        return false;
      sign = -1;
    }
  }
  return true;
}
// the rectangle x0..x1 x y0..y1 shares area with piece i (separating axes; touching is clear)
inline bool rectHits(const Shape &s, u32 i, int x0, int y0, int x1, int y1) {
  const Pt *p = s.p[i];
  u32 k = s.k[i];
  for (u32 j = 0; j < k; j++) {
    const Pt &a = p[j], &b = p[j + 1 < k ? j + 1 : 0];
    int nx = a.y - b.y, ny = b.x - a.x, pmin = 0, pmax = 0, rmin = 0, rmax = 0;
    for (u32 m = 0; m < k; m++) {
      int v = nx * p[m].x + ny * p[m].y;
      if (!m || v < pmin)
        pmin = v;
      if (!m || v > pmax)
        pmax = v;
    }
    int rx[4] = { x0, x1, x1, x0 }, ry[4] = { y0, y0, y1, y1 };
    for (int m = 0; m < 4; m++) {
      int v = nx * rx[m] + ny * ry[m];
      if (!m || v < rmin)
        rmin = v;
      if (!m || v > rmax)
        rmax = v;
    }
    if (pmax <= rmin || rmax <= pmin)
      return false;
  }
  int minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
  for (u32 m = 1; m < k; m++) {
    if (p[m].x < minx)
      minx = p[m].x;
    if (p[m].x > maxx)
      maxx = p[m].x;
    if (p[m].y < miny)
      miny = p[m].y;
    if (p[m].y > maxy)
      maxy = p[m].y;
  }
  return !(maxx <= x0 || minx >= x1 || maxy <= y0 || miny >= y1);
}
// beside the ring: the free spot nearest the ring centre (cx, cy in the box), clear of every piece and of the ring's hole, inside the
// part of the box the MINI shows (x 74..166). A grid of 2-unit cells with prefix sums picks the candidates quickly, an exact test of
// the number's rectangle against every piece settles each one.
inline void exitNumber(Shape &s, u32 n, int cx, int cy) {
  const int N = 50, kHole = 28;  // the ring's outer radius in box units, and a little
  static unsigned short S[N + 1][N + 1];
  for (int j = 0; j <= N; j++)
    S[j][0] = S[0][j] = 0;
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      int x = 71 + 2 * i, y = 21 + 2 * j, dx = x - cx, dy = y - cy;
      bool o = dx * dx + dy * dy <= kHole * kHole;
      for (u32 p = 0; !o && p < s.n; p++)
        o = inside(s, p, x, y);
      S[j + 1][i + 1] = (unsigned short)(o + S[j][i + 1] + S[j + 1][i] - S[j][i]);
    }
  int w = numberWidth(n), drawn = (int)s.n;
  for (int margin = 2; margin >= 0; margin--) {  // cells of clearance: 4 units, 2, touching
    int wc = (w + 1) / 2 + 2 * margin, hc = (kDH + 1) / 2 + 2 * margin, best = -1, bx_ = 0, by_ = 0, m = 2 * margin;
    for (int j = 0; j + hc <= N; j++)
      for (int i = 2; i + wc <= N - 2; i++) {
        if (S[j + hc][i + wc] - S[j][i + wc] - S[j + hc][i] + S[j][i])
          continue;
        int x = 70 + 2 * (i + margin), y = 20 + 2 * (j + margin), dx = x + w / 2 - cx, dy = y + kDH / 2 - cy, d = dx * dx + dy * dy;
        if (best >= 0 && d >= best)
          continue;
        int qx = cx < x - m       ? x - m
                 : cx > x + w + m ? x + w + m
                                  : cx,
            qy = cy < y - m         ? y - m
                 : cy > y + kDH + m ? y + kDH + m
                                    : cy;
        if ((qx - cx) * (qx - cx) + (qy - cy) * (qy - cy) < kHole * kHole)
          continue;
        bool hit = false;
        for (int p = 0; !hit && p < drawn; p++)
          hit = rectHits(s, (u32)p, x - m, y - m, x + w + m, y + kDH + m);
        if (!hit) {
          best = d;
          bx_ = x;
          by_ = y;
        }
      }
    if (best >= 0) {
      s.tone = 0;
      number(s, n, bx_, by_);
      return;
    }
  }
  s.tone = 0;
  number(s, n, 76, 118 - kDH);  // nowhere free: bottom left
}

// ---- ferry (not in Mapbox's set; drawn in its weight): hull, cabin with windows, funnel, waves ------------------------------------------
inline void ferry(Shape &s) {
  quad16(s, { 48, 280 }, { 464, 280 }, { 416, 376 }, { 96, 376 });  // hull
  P16 cab[6] = { { 128, 280 }, { 128, 192 }, { 152, 168 }, { 360, 168 }, { 384, 192 }, { 384, 280 } };
  Pt q[6];
  for (int i = 0; i < 6; i++)
    q[i] = { boxX(cab[i].x), boxY(cab[i].y) };
  add(s, q, 6);
  quad16(s, { 192, 168 }, { 200, 88 }, { 264, 88 }, { 256, 168 });  // funnel
  s.tone = 1;
  rect16(s, 160, 200, 208, 240);
  rect16(s, 232, 200, 280, 240);
  rect16(s, 304, 200, 352, 240);  // windows
  for (int i = 0; i < 6; i++) {   // waves: a zigzag
    int x0 = 48 + i * 69, x1 = x0 + 69, y0 = (i & 1) ? 456 : 424, y1 = (i & 1) ? 424 : 456;
    quad16(s, { x0, y0 - 12 }, { x1, y1 - 12 }, { x1, y1 + 12 }, { x0, y0 + 12 });
  }
  s.tone = 0;
}

// a turn by its angle alone: straight, slight, turn, sharp, U-turn (to the driving side's side)
inline void byAngle(Shape &s, int a, bool lht) {
  int m = a < 0 ? -a : a;
  bool left = a < 0;
  if (m <= 20)
    addGlyph(s, HUD_GLYPH(Arrowstraight));
  else if (m <= 65)
    addGlyph(s, HUD_GLYPH(Arrowslightright), left);
  else if (m <= 125)
    addGlyph(s, HUD_GLYPH(Arrowright), left);
  else if (m <= 165)
    addGlyph(s, HUD_GLYPH(Arrowsharpright), left);
  else
    addGlyph(s, HUD_GLYPH(Arrow180right), !lht);
}

// iAP2 RouteGuidanceManeuverType -> picture. ang/nang: the junction's roads, from the iPhone (drawn on roundabouts)
inline Shape shapeOf(u32 t, int angle, u32 side, u32 jtype = 0, const int *ang = 0, u32 nang = 0) {
  (void)jtype;
  Shape s;
  memset(&s, 0, sizeof s);
  bool lht = side == 1;
  int a = angle < -180 ? -180 : angle > 180 ? 180 : angle;
  switch (t) {
    case 51:
      addGlyph(s, HUD_GLYPH(Arrowstraight));
      break;  // change highway
    case 1:
    case 20:
      addGlyph(s, HUD_GLYPH(Arrowright), true);
      break;  // left; left at the end of the road
    case 2:
    case 21:
      addGlyph(s, HUD_GLYPH(Arrowright));
      break;
    case 47:
      addGlyph(s, HUD_GLYPH(Arrowsharpright), true);
      break;
    case 48:
      addGlyph(s, HUD_GLYPH(Arrowsharpright));
      break;
    case 49:
      addGlyph(s, HUD_GLYPH(Arrowslightright), true);
      break;
    case 50:
      addGlyph(s, HUD_GLYPH(Arrowslightright));
      break;
    case 13:
    case 52:
      addGlyph(s, HUD_GLYPH(Fork), true);
      break;  // keep left, change highway left
    case 14:
    case 53:
      addGlyph(s, HUD_GLYPH(Fork));
      break;
    case 22:
      addGlyph(s, HUD_GLYPH(Offramp), true);
      break;  // highway exit on the left
    case 23:
      addGlyph(s, HUD_GLYPH(Offramp));
      break;
    case 8:
      addGlyph(s, HUD_GLYPH(Offramp), a ? a < 0 : lht);
      break;  // off-ramp: by the angle, else the driving side
    case 9:
      addGlyph(s, HUD_GLYPH(Merge), lht);
      break;  // on-ramp
    case 4:
    case 18:
    case 26:
      addGlyph(s, HUD_GLYPH(Arrow180right), !lht);
      break;  // U-turns: to the left in right-hand traffic
    case 11:
      addGlyph(s, HUD_GLYPH(Starting));
      break;  // start of the route
    case 12:
      addGlyph(s, HUD_GLYPH(Arrive));
      break;  // arrived
    case 10:
    case 27:
      addGlyph(s, HUD_GLYPH(Destination));
      break;  // end of navigation / of directions
    case 24:
      addGlyph(s, HUD_GLYPH(Arriveright), true);
      break;  // destination on the left
    case 25:
      addGlyph(s, HUD_GLYPH(Arriveright));
      break;
    case 15:
    case 16:
    case 17:
      ferry(s);
      break;  // enter, exit, change ferry
    default:
      if (t == 6 || t == 7 || t == 19 || (t >= 28 && t <= 46)) {  // roundabouts: enter, exit, U-turn, exit 1..19
        u32 n = t >= 28 ? t - 27 : 0;
        int e = t == 19 ? kRbUturn : lht ? -a : a, roads[8];
        u32 nr = 0;
        if (n < 5) {
          if (ang && nang) {
            for (u32 i = 0; i < nang && nr < 8; i++)
              roads[nr++] = lht ? -ang[i] : ang[i];
          } else
            nr = passedExits(roads, n, rbArc(e));
        }
        P16 c = roundabout(s, e, roads, nr);
        int cx = boxX(c.x), cy = boxY(c.y);
        if (lht) {
          mirror(s);
          cx = 240 - cx;
        }
        if (n >= 5)
          exitNumber(s, n, cx, cy);  // many exits: the number instead of the roads
      } else
        byAngle(s, a, lht);  // 0 no turn, 3 straight, 5 follow road, future types
  }
  return s;
}

// ---- lane guidance: a strip of lane arrows under the maneuver ---------------------------------------------------------------------------
// The iPhone's lanes (LaneGuidanceInformation, rghook.c), left to right: status 0 not good, 1 good, 2 preferred; the angles each lane
// leads to (0 straight on, positive to the right, as the exit angle) and the one to take from it (hl, 1000 = none). Lanes are where they
// are on the road: left-hand traffic does not mirror them.
struct Lane {
  u32 status;
  int hl;
  int ang[4];
  u32 na;
};
// The strip got taller and its arrows bolder after the first look in the car (2026-11-13: lanes right, but small on the MINI's 50 px box).
const int kLaneTop = 84, kLaneBottom = 119, kLaneLeft = 74, kLaneRight = 166, kLaneMaxW = 26;
const int kLaneScale = 10;  // the maneuver picture at 10/16, in the box above the strip (y 20..82)

// the picture made smaller towards the top of the box, so the strip fits under it (pieces folded flat by the rounding go)
inline void shrinkPicture(Shape &s) {
  u32 o = 0;
  for (u32 i = 0; i < s.n; i++) {
    Pt q[kPiecePts];
    u32 k = s.k[i];
    for (u32 j = 0; j < k; j++)
      q[j] = { (s16)(120 + (((s.p[i][j].x - 120) * kLaneScale) >> 4)), (s16)(20 + (((s.p[i][j].y - 20) * kLaneScale) >> 4)) };
    k = hull(q, k);
    if (k < 3)
      continue;
    for (u32 j = 0; j < k; j++)
      s.p[o][j] = q[j];
    s.k[o] = k;
    s.t[o] = s.t[i];
    o++;
  }
  s.n = o;
}
inline int normLane(int a) {  // -180..180
  while (a > 180)
    a -= 360;
  while (a < -180)
    a += 360;
  return a;
}
// a U-turn: 160 degrees and more; +-180 goes to the driving side's U-turn side (left in right-hand traffic)
inline int uturnDir(int a, bool lht) {
  return a >= 160 && a < 180 ? 1 : a <= -160 && a > -180 ? -1 : lht ? 1 : -1;
}
// Mapbox's lane drawings (hudglyphs.h): 30 x 30 frame in 1/16 units, drawn y 32..437 (all of them), each as wide as it is
struct LaneG {
  const glyph::Piece *g;
  u32 n;
  bool mirrored;
};
const int kLaneGy = 30, kLaneGh = 410;  // the drawings' top and height in the frame
inline void laneSpan(const LaneG &d, int &x0, int &x1) {
  x0 = 480;
  x1 = 0;
  for (u32 i = 0; i < d.n; i++)
    for (u32 j = 0; j < d.g[i].n; j++) {
      int x = d.g[i].p[j][0];
      if (x < x0)
        x0 = x;
      if (x > x1)
        x1 = x;
    }
}
// the drawing into the lane's cell: sc box units per 1024 frame units, its own extent centred on cx, mirrored for the left. Pieces of
// tone `pass` only (the context tone is drawn first); force1: the whole drawing in the context tone (a lane not to take).
inline void addLaneGlyph(Shape &s, const LaneG &d, int cx, int top, int sc, bool force1, u32 pass) {
  int gx0, gx1;
  laneSpan(d, gx0, gx1);
  int gx = (gx0 + gx1) >> 1;
  u32 was = s.tone;
  for (u32 i = 0; i < d.n; i++) {
    u32 tone = force1 ? 1 : d.g[i].tone;
    if (tone != pass)
      continue;
    Pt q[8];
    for (u32 j = 0; j < d.g[i].n && j < 8; j++) {
      int dx = (d.g[i].p[j][0] - gx) * sc, dy = (d.g[i].p[j][1] - kLaneGy) * sc;
      dx = dx < 0 ? -((-dx) >> 10) : dx >> 10;  // no division: the head unit's libc has no __aeabi_idiv
      if (dy < 0)
        dy = 0;
      q[j] = { (s16)(d.mirrored ? cx - dx : cx + dx), (s16)(top + (dy >> 10)) };
    }
    s.tone = tone;
    add(s, q, d.g[i].n);
  }
  s.tone = was;
}
// a way out of a lane: 0 straight (up to 20 degrees), 1 slight (up to 59), 2 turn (up to 159), 3 U-turn
inline int laneKind(int a) {
  int m = a < 0 ? -a : a;
  return m <= 20 ? 0 : m < 60 ? 1 : m < 160 ? 2 : 3;
}
inline LaneG laneOne(int a, bool lht) {
  int k = laneKind(a);
  if (k == 0)
    return { HUD_GLYPH(LaneStraight), false };
  if (k == 1)
    return { HUD_GLYPH(LaneSlightRight), a < 0 };
  if (k == 2)
    return { HUD_GLYPH(LaneRight), a < 0 };
  return { HUD_GLYPH(LaneUturn), uturnDir(a, lht) < 0 };  // Mapbox's U-turn turns right
}
// what a lane shows: d[0] its drawing (the dim way inside it for the straight + turn ones), d[1] a second way drawn dim under it
// (two turns, or a U-turn with another way), dim0 = the whole lane dim (not to take)
struct LaneLook {
  LaneG d[2];
  u32 nd;
  bool dim0;
};
inline LaneLook laneLook(const Lane &L, bool lht) {
  int ways[2];
  u32 nw = 0;
  if (L.hl != 1000)
    ways[nw++] = normLane(L.hl);
  for (u32 j = 0; j < L.na && nw < 2; j++) {
    int a = normLane(L.ang[j]);
    if (!nw || a != ways[0])
      ways[nw++] = a;
  }
  if (!nw)
    ways[nw++] = 0;
  LaneLook v;
  v.dim0 = L.status == 0;
  int k0 = laneKind(ways[0]), k1 = nw > 1 ? laneKind(ways[1]) : -1;
  if (nw > 1 && (k0 == 0) != (k1 == 0) && (k0 == 0 ? k1 : k0) <= 2) {  // straight and a turn: one drawing
    bool left = (k0 == 0 ? ways[1] : ways[0]) < 0;
    if (v.dim0 || L.hl == 1000)
      v.d[0] = { HUD_GLYPH(LaneStraightRight), left };
    else if (k0 == 0)
      v.d[0] = { HUD_GLYPH(LaneStraightOnly), left };
    else
      v.d[0] = { HUD_GLYPH(LaneRightOnly), left };
    v.nd = 1;
    return v;
  }
  v.d[0] = laneOne(ways[0], lht);
  v.nd = nw;
  if (nw > 1)
    v.d[1] = laneOne(ways[1], lht);
  return v;
}
// the strip, on a picture made by shapeOf: each lane as Mapbox draws it (straight, slight, turn, U-turn, or straight plus a turn with
// the way not to take dim), the lanes to take in the route tone, the others in the context tone. At most 8 lanes, 2 ways out each
// (the highlighted one first). One scale for the whole strip: as tall as it fits, as wide as the widest drawing allows in its lane.
// Car, 2026-11-13: the lanes are right; the first strip (bands and triangles) looked poor at this size.
inline void addLanes(Shape &s, const Lane *lanes, u32 n, bool lht = false) {
  if (!n)
    return;
  if (n > 8)
    n = 8;
  shrinkPicture(s);
  int span = kLaneRight - kLaneLeft, w = udiv(span, (int)n);
  if (w > kLaneMaxW)
    w = kLaneMaxW;
  int x0 = kLaneLeft + ((span - w * (int)n) >> 1), h = kLaneBottom - kLaneTop;
  LaneLook look[8];
  int sc = udiv(h * 1024, kLaneGh);  // box units per 1024 frame units: the full strip height ...
  for (u32 i = 0; i < n; i++) {
    look[i] = laneLook(lanes[i], lht);
    for (u32 j = 0; j < look[i].nd; j++) {
      int a, b;
      laneSpan(look[i].d[j], a, b);
      int m = b > a ? udiv((w - 2) * 1024, b - a) : sc;  // ... unless a drawing is then wider than its lane
      if (m < sc)
        sc = m;
    }
  }
  int top = kLaneTop + ((h - ((kLaneGh * sc) >> 10)) >> 1);
  for (u32 pass = 1; pass <= 1; pass--)  // the context tone first (1, then 0): the route tone is drawn over it
    for (u32 i = 0; i < n; i++) {
      int cx = x0 + w * (int)i + (w >> 1);
      for (u32 j = look[i].nd; j-- > 0;)  // a second way dim under the first
        addLaneGlyph(s, look[i].d[j], cx, top, sc, look[i].dim0 || j > 0, pass);
    }
  s.tone = 0;
}

// ---- test switches (hud.h): progress fill, blink colour, whole polygons -----------------------------------------------------------------
// tone 2 = the effect colour (hud.h: hud_fill / hud_blink). Effects touch only the maneuver (tone 0 pieces of shapeOf), never the lanes:
// hud.h applies them before addLanes.
inline int sdivp(int a, int b) {
  return a < 0 ? -udiv(-a, b) : udiv(a, b);
}  // b > 0
inline bool samePt(Pt a, Pt b) {
  return a.x == b.x && a.y == b.y;
}
// v % n for v < 2n: a % by a variable needs __aeabi_uidivmod, which the head unit's libc.so.3 lacks (the HMI refuses the plugin)
inline u32 wrapN(u32 v, u32 n) {
  return v >= n ? v - n : v;
}
// the part of convex piece p at or below (keepBelow, y >= yc) or at or above the line y = yc: convex, at most one point more. The
// crossing point is worked out from the edge's lower end whichever way the edge runs, so two pieces sharing an edge get the same point
// (mergePieces joins them again).
inline u32 clipY(const Pt *p, u32 k, int yc, bool keepBelow, Pt *o) {
  u32 m = 0;
  for (u32 i = 0; i < k; i++) {
    Pt a = p[i], b = p[wrapN(i + 1, k)];
    bool ina = keepBelow ? a.y >= yc : a.y <= yc, inb = keepBelow ? b.y >= yc : b.y <= yc;
    if (ina)
      o[m++] = a;
    if (ina != inb) {
      bool aLow = a.y < b.y || (a.y == b.y && a.x < b.x);
      Pt lo = aLow ? a : b, hi = aLow ? b : a;
      Pt c = { (s16)(lo.x + sdivp((hi.x - lo.x) * (yc - lo.y), hi.y - lo.y)), (s16)yc };
      if (!m || !samePt(o[m - 1], c))
        o[m++] = c;
    }
  }
  while (m > 1 && samePt(o[0], o[m - 1]))
    m--;
  return m;
}
// Progress fill: the maneuver's route-tone pieces from the bottom of the picture up to level/8 of its height go to tone 2 (level 0
// nothing, 8 all). A piece across the line is cut in two, the filled part first and the rest right after it, so a cut-out drawn later
// (the pin's hole) still covers both.
inline void applyFill(Shape &s, u32 level) {
  if (!level)
    return;
  int y0 = 32767, y1 = -32768;
  for (u32 i = 0; i < s.n; i++)
    if (!s.t[i])
      for (u32 j = 0; j < s.k[i]; j++) {
        if (s.p[i][j].y < y0)
          y0 = s.p[i][j].y;
        if (s.p[i][j].y > y1)
          y1 = s.p[i][j].y;
      }
  if (y0 >= y1)
    return;
  int yc = level >= 8 ? y0 - 1 : y1 - (((y1 - y0) * (int)level) >> 3);
  for (u32 i = 0; i < s.n; i++) {
    if (s.t[i])
      continue;
    Pt lo[kPiecePts], up[kPiecePts];
    u32 nl = clipY(s.p[i], s.k[i], yc, true, lo), nu = clipY(s.p[i], s.k[i], yc, false, up);
    nl = nl >= 3 ? hull(lo, nl) : nl;
    nu = nu >= 3 ? hull(up, nu) : nu;  // the cut point rounded to whole units can dent a part
    // a part that rounds away (all on one side, or a sliver under a unit wide) leaves the other part alone in the piece
    if (nl < 3) {
      if (nu >= 3) {
        for (u32 j = 0; j < nu; j++)
          s.p[i][j] = up[j];
        s.k[i] = nu;
      }
      continue;
    }  // stays route tone
    if (nu < 3) {
      for (u32 j = 0; j < nl; j++)
        s.p[i][j] = lo[j];
      s.k[i] = nl;
      s.t[i] = 2;
      continue;
    }  // filled
    if (s.n >= kMaxPieces) {
      s.t[i] = 2;
      continue;
    }  // no room to cut it: filled whole
    for (u32 j = s.n; j > i + 1; j--) {
      memcpy(s.p[j], s.p[j - 1], sizeof s.p[j]);
      s.k[j] = s.k[j - 1];
      s.t[j] = s.t[j - 1];
    }
    s.n++;
    for (u32 j = 0; j < nl; j++)
      s.p[i][j] = lo[j];
    s.k[i] = nl;
    s.t[i] = 2;
    for (u32 j = 0; j < nu; j++)
      s.p[i + 1][j] = up[j];
    s.k[i + 1] = nu;
    s.t[i + 1] = 0;
    i++;
  }
}
// the whole maneuver in the effect colour (the blink's bright phase)
inline void allTone(Shape &s, u32 from, u32 to) {
  for (u32 i = 0; i < s.n; i++)
    if (s.t[i] == from)
      s.t[i] = (unsigned char)to;
}

// Whole polygons (always on; the test switch hud_convex keeps the pieces): the convex pieces of one tone that share an edge are joined
// back into one polygon of up to kPolyPts points (NBT-AAD's 6WA pictures are 2..4 such polygons each), so a picture takes a few MOST
// calls instead of up to 96. The F54's terminal fills a polygon that is not convex cleanly (car, 2026-11-13). Pieces are joined only
// with pieces of the same tone with no other tone drawn between them, so what covers what stays as drawn. A ring closed on itself
// keeps a zero-width slit where its ends meet.
const u32 kPolyPts = 96;  // the cluster takes up to 99 (NBT-AAD, F010 KOMBI firmware)
struct Poly {
  u32 n;
  unsigned char t;
  Pt p[kPolyPts];
};
struct Polys {
  u32 n;
  Poly q[kMaxPieces];
};
inline void dropPt(Pt *p, u32 &n, u32 i) {
  for (u32 m = i; m + 1 < n; m++)
    p[m] = p[m + 1];
  n--;
}
// repeated points and there-and-back spikes (a, b, a) out
inline u32 tidy(Pt *p, u32 n) {
  for (bool ch = true; ch && n >= 3;) {
    ch = false;
    for (u32 i = 0; i < n && n >= 3; i++) {
      u32 j = wrapN(i + 1, n), k = wrapN(i + 2, n);  // n >= 3
      if (samePt(p[i], p[j])) {
        dropPt(p, n, j);
        ch = true;
        break;
      }
      if (samePt(p[i], p[k])) {
        u32 a = j > k ? j : k, b = j > k ? k : j;
        dropPt(p, n, a);
        dropPt(p, n, b);
        ch = true;
        break;
      }
    }
  }
  return n;
}
inline void mergePieces(const Shape &s, Polys &m) {
  m.n = 0;
  for (u32 i = 0; i < s.n; i++) {
    Poly &q = m.q[m.n++];
    q.n = s.k[i];
    q.t = s.t[i];
    for (u32 j = 0; j < q.n; j++)
      q.p[j] = s.p[i][j];
  }
  static Pt tmp[2 * kPolyPts];
  for (u32 i = 0; i < m.n; i++) {
    for (u32 j = i + 1; j < m.n; j++) {
      Poly &a = m.q[i], &b = m.q[j];
      if (a.t != b.t)
        break;  // another tone between: stop here (keeps the drawing order)
      if (a.n + b.n - 2 > kPolyPts)
        continue;
      u32 fx = ~0u, fy = 0;
      for (u32 x = 0; x < a.n && fx == ~0u; x++)
        for (u32 y = 0; y < b.n; y++)
          if (samePt(a.p[x], b.p[wrapN(y + 1, b.n)]) && samePt(a.p[wrapN(x + 1, a.n)], b.p[y])) {
            fx = x;
            fy = y;
            break;
          }
      if (fx == ~0u)
        continue;
      u32 n = 0;
      for (u32 x = 0; x <= fx; x++)
        tmp[n++] = a.p[x];
      for (u32 k = 2; k < b.n; k++)
        tmp[n++] = b.p[wrapN(fy + k, b.n)];
      for (u32 x = fx + 1; x < a.n; x++)
        tmp[n++] = a.p[x];
      n = tidy(tmp, n);
      if (n < 3 || n > kPolyPts)
        continue;
      for (u32 k = 0; k < n; k++)
        a.p[k] = tmp[k];
      a.n = n;
      for (u32 k = j; k + 1 < m.n; k++)
        m.q[k] = m.q[k + 1];
      m.n--;
      j = i;  // a grew: look at the rest again
    }
  }
}
// a whole polygon in the MINI cluster's picture area (as toMini, without the hull: the polygon need not be convex)
inline u32 toMiniPoly(Pt *p, u32 k) {
  for (u32 i = 0; i < k; i++) {
    p[i].x = (s16)(35 + ((p[i].x - 70) >> 1));
    p[i].y = (s16)(15 + ((p[i].y - 20) >> 1));
  }
  return tidy(p, k);
}

}  // namespace hud
