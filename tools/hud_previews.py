#!/usr/bin/env python3
"""The HUD previews in docs/: every maneuver picture and a set of lane strips, as src/hudshapes.h builds them for the head unit.

    python3 tools/hud_previews.py            # writes docs/hud-maneuvers.svg and docs/hud-lanes.svg

Each picture is built by the same code the head unit runs (compiled for this computer by the hud-glyphs skill's preview.py harness),
then drawn in the 6WA box (100 x 100 units) with the HUD's colours: white the route, grey the context. The drawings are Mapbox's
(ISC, see NOTICE). Needs clang++.
"""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('preview', ROOT / '.claude' / 'skills' / 'hud-glyphs' / 'preview.py')
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)

TONES = {0: '#ffffff', 1: '#8a8a8a', 2: '#ff9f1a'}
BG, FG = '#101418', '#cfd8e3'
SCALE, PAD, LABEL, COLS = 1.4, 10, 34, 6

# iAP2 RouteGuidanceManeuverType: the name and a picture spec (TYPE:ANGLE, preview.py's form); roundabout exits get an exit angle
MANEUVERS = [
    ('No turn', '0'), ('Left turn', '1'), ('Right turn', '2'), ('Straight ahead', '3'), ('U-turn', '4'), ('Follow road', '5'),
    ('Enter roundabout', '6:0'), ('Exit roundabout', '7:90'), ('Off-ramp', '8'), ('On-ramp', '9'), ('End of navigation', '10'),
    ('Start route', '11'), ('Arrive', '12'), ('Keep left', '13'), ('Keep right', '14'), ('Enter ferry', '15'), ('Exit ferry', '16'),
    ('Change ferry', '17'), ('Start with U-turn', '18'), ('U-turn at roundabout', '19:180'), ('Left at the end', '20'),
    ('Right at the end', '21'), ('Highway exit left', '22'), ('Highway exit right', '23'), ('Arrive on the left', '24'),
    ('Arrive on the right', '25'), ('U-turn when possible', '26'), ('End of directions', '27'),
    ('Roundabout, exit 1', '28:90'), ('Roundabout, exit 2', '29:0'), ('Roundabout, exit 3', '30:-90'),
    ('Roundabout, exit 4', '31:-150'), ('Roundabout, exit 7', '34:-45'), ('Sharp left', '47'), ('Sharp right', '48'),
    ('Slight left', '49'), ('Slight right', '50'), ('Change highway', '51'), ('Change highway left', '52'),
    ('Change highway right', '53'), ('Roundabout, left-hand traffic', '29:0:1'), ('Right turn, left-hand traffic', '2:0:1'),
]
# lanes: rghook.c's "status/highlight/angles;..." left to right (status 0 = not to take; highlight 1000 = none)
LANES = [
    ('Left from the left lane', '1', '1/-90/-90;0/1000/0;0/1000/0'),
    ('Straight: middle lanes', '3', '0/1000/-90;1/0/0;1/0/0;0/1000/90'),
    ('Shared lane: turn right', '2', '0/1000/-90;0/1000/0;1/90/0,90'),
    ('Shared lane: straight', '3', '0/1000/-90;1/0/0;1/0/0,90'),
    ('Slight right', '50', '0/1000/0;0/1000/0;1/45/45'),
    ('U-turn, left lane', '4', '1/-180/-180;0/1000/0;0/1000/0'),
    ('Five lanes', '3', '0/1000/-90;1/0/0;1/0/0;1/0/0,90;0/1000/90'),
    ('Two lanes, keep right', '14', '0/1000/0;1/30/30'),
]


def cell(x0, y0, title, pieces):
    w = int(100 * SCALE)
    o = ['<g transform="translate(%d,%d)">' % (x0, y0),
         '<rect width="%d" height="%d" rx="6" fill="%s"/>' % (w + 2 * PAD, w + 2 * PAD + LABEL, BG)]
    for tone, pts in pieces:
        c = TONES.get(tone, '#ff00ff')
        # the stroke in the fill colour closes the hairline seams between pieces (the HUD fills whole polygons)
        o.append('<polygon points="%s" fill="%s" stroke="%s" stroke-width="0.8" stroke-linejoin="round"/>'
                 % (' '.join('%.1f,%.1f' % (PAD + (x - 70) * SCALE, PAD + (y - 20) * SCALE) for x, y in pts), c, c))
    o.append('<text x="%d" y="%d" fill="%s" font-family="-apple-system,Segoe UI,Helvetica,Arial,sans-serif" font-size="12" '
             'text-anchor="middle">%s</text>' % ((w + 2 * PAD) // 2, w + 2 * PAD + 20, FG, title))
    o.append('</g>')
    return o


def sheet(path, items, exe):
    w = int(100 * SCALE) + 2 * PAD
    h = w + LABEL
    rows = (len(items) + COLS - 1) // COLS
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
           % (COLS * (w + 8) + 8, rows * (h + 8) + 8, COLS * (w + 8) + 8, rows * (h + 8) + 8)]
    for i, (title, pic, lanes) in enumerate(items):
        out += cell(8 + (i % COLS) * (w + 8), 8 + (i // COLS) * (h + 8), title, preview.shape(exe, pic, lanes, False))
    out.append('</svg>')
    path.write_text('\n'.join(out) + '\n')
    print('%d pictures -> %s' % (len(items), path.relative_to(ROOT)))


def main():
    exe = preview.harness()
    (ROOT / 'docs').mkdir(exist_ok=True)
    sheet(ROOT / 'docs' / 'hud-maneuvers.svg', [(t, p, None) for t, p in MANEUVERS], exe)
    sheet(ROOT / 'docs' / 'hud-lanes.svg', LANES, exe)


if __name__ == '__main__':
    main()
