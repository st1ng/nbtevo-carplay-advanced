#!/usr/bin/env python3
"""Renders HUD maneuver pictures to one SVG sheet, to look at them before a car does.

    python3 .claude/skills/hud-glyphs/preview.py out.svg 1 2 49:0:1 6:45 33:0:0:j1:-90,0,90 [--lanes "0/1000/-90;1/1000/0"] [--mini]
    python3 .claude/skills/hud-glyphs/preview.py out.svg --glyphs          # every generated glyph in src/hudglyphs.h, as stored

A picture is TYPE[:ANGLE[:SIDE[:jJTYPE:A,A,...]]]: the iAP2 maneuver type 0..53, the exit angle (positive = right), the driving
side (0 right-hand traffic, 1 left-hand), the junction type and its roads' angles. Each picture is built by src/hudshapes.h exactly
as the head unit builds it (tests/test_hud_shapes.py's harness, compiled with the Mac's clang++): tone 0 (route) white, tone 1
(context) grey, tone 2 (effect) orange; the 6WA box (70,20)-(170,120) is outlined, and with --mini the points are mapped to the MINI
cluster's area (35,15)-(85,65) as hud.h sends them. --lanes adds the lane strip (rghook.c's "status/highlight/angles;..." form)."""
import importlib.util
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TONES = {0: '#ffffff', 1: '#7a7a7a', 2: '#ff9f1a'}
W, H, S = 240, 140, 2                      # the terminal's canvas around the box, scale


def harness():
    spec = importlib.util.spec_from_file_location('test_hud_shapes', ROOT / 'tests' / 'test_hud_shapes.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    d = Path(tempfile.mkdtemp(prefix='hudpreview-'))
    (d / 'h.cpp').write_text(mod.HARNESS)
    exe = d / 'h'
    subprocess.run(['clang++', '-std=c++11', '-I', str(ROOT / 'src'), '-o', str(exe), str(d / 'h.cpp')], check=True)
    return exe


def shape(exe, spec, lanes, mini):
    parts = spec.split(':')
    args = [parts[0], parts[1] if len(parts) > 1 else '0', parts[2] if len(parts) > 2 else '0']
    if len(parts) > 3 and parts[3].startswith('j'):
        args += [parts[3][1:]] + (parts[4].split(',') if len(parts) > 4 and parts[4] else [])
    env = {}
    if lanes: env['HUD_LANES'] = lanes
    if mini: env['HUD_MINI'] = '1'
    out = subprocess.run([str(exe)] + args, env=env, capture_output=True, text=True, check=True).stdout
    pieces = []
    for line in out.splitlines():
        f = line.split()
        pieces.append((int(f[0]), [tuple(int(v) for v in p.split(',')) for p in f[1:]]))
    return pieces


def glyphs():
    """the pieces stored in src/hudglyphs.h: {name: [(tone, [(x, y), ...]), ...]} (kRbArrow is in 1/16 units around 0,0: shifted)"""
    text = (ROOT / 'src' / 'hudglyphs.h').read_text()
    out = {}
    for name, body in re.findall(r'const Piece k(\w+)\[\] = \{(.*?)\n\};', text, re.S):
        ps = []
        for tone, pts in re.findall(r'\{ (\d+), \d+, \{ (.*?) \} \}', body):
            pp = [tuple(int(v) for v in q.split(',')) for q in re.findall(r'\{(-?\d+,-?\d+)\}', pts)]
            if name == 'RbArrow':
                pp = [(120 + x // 8, 70 + y // 8) for x, y in pp]
            ps.append((int(tone), pp))
        out[name] = ps
    return out


def cell(x0, y0, title, pieces, mini):
    o = ['<g transform="translate(%d,%d)">' % (x0, y0),
         '<rect width="%d" height="%d" fill="#101418"/>' % (W * S, H * S)]
    bx = (35, 15, 85, 65) if mini else (70, 20, 170, 120)
    o.append('<rect x="%d" y="%d" width="%d" height="%d" fill="none" stroke="#3a6ea5" stroke-dasharray="4 3"/>'
             % (bx[0] * S, bx[1] * S, (bx[2] - bx[0]) * S, (bx[3] - bx[1]) * S))
    for tone, pts in pieces:
        o.append('<polygon points="%s" fill="%s" stroke="#101418" stroke-width="0.6"/>'
                 % (' '.join('%d,%d' % (x * S, y * S) for x, y in pts), TONES.get(tone, '#ff00ff')))
    o.append('<text x="6" y="16" fill="#cfd8e3" font-family="monospace" font-size="13">%s (%d pieces)</text>' % (title, len(pieces)))
    o.append('</g>')
    return o


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    out, rest = Path(args[0]), args[1:]
    lanes = mini = None
    items = []
    if '--lanes' in rest:
        i = rest.index('--lanes'); lanes = rest[i + 1]; del rest[i:i + 2]
    if '--mini' in rest:
        rest.remove('--mini'); mini = True
    if '--glyphs' in rest:
        items = [(name, ps) for name, ps in glyphs().items()]
    else:
        exe = harness()
        items = [(spec, shape(exe, spec, lanes, mini)) for spec in rest]
    cols = 4
    rows = (len(items) + cols - 1) // cols
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d">' % (cols * (W * S + 8), rows * (H * S + 8))]
    for k, (title, ps) in enumerate(items):
        svg += cell((k % cols) * (W * S + 8), (k // cols) * (H * S + 8), title, ps, mini)
    svg.append('</svg>')
    out.write_text('\n'.join(svg))
    print('%d picture(s) -> %s' % (len(items), out))


if __name__ == '__main__':
    main()
