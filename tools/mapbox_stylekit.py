"""Interprets Mapbox's ManeuversStyleKit.swift (mapbox-navigation-ios v0.40.0, ISC licence; drawn from the CC0 Maneuvers.pcvd) into
absolute-coordinate shapes: a list of {'kind': 'fill'|'stroke', 'color': 'primary'|'secondary', 'width', 'dash', 'subpaths'}, each
subpath a list of segments ('M', p) ('L', p) ('C', c1, c2, p) ('Z',). Canvas 32 x 32, y down."""
import math
import re

NUM = r'[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?'


class Affine:
    def __init__(self, m=(1, 0, 0, 1, 0, 0)):
        self.m = m

    def mul(self, o):            # self * o (o applied first)
        a, b, c, d, e, f = self.m
        A, B, C, D, E, F = o.m
        return Affine((a * A + c * B, b * A + d * B, a * C + c * D, b * C + d * D, a * E + c * F + e, b * E + d * F + f))

    def ap(self, p):
        a, b, c, d, e, f = self.m
        return (a * p[0] + c * p[1] + e, b * p[0] + d * p[1] + f)

    def scale_len(self):
        a, b, c, d, _, _ = self.m
        return math.sqrt(abs(a * d - b * c))


def py_expr(s, env):
    s = s.replace('CGFloat.pi', 'math.pi').replace('size.width', '32.0').replace('size.height', '32.0')
    s = re.sub(r'CGFloat\(([^)]*)\)', r'(\1)', s)
    m = re.match(r'^\s*(\w+) \? (.*) : (.*)$', s)                   # Swift's ternary (LanesStyleKit: flipHorizontally ? -1 : 1)
    if m: s = '(%s) if %s else (%s)' % (m.group(2), m.group(1), m.group(3))
    return eval(s, {'math': math, 'cos': math.cos, 'sin': math.sin, 'min': min, 'max': max, 'abs': abs}, env)


def split_args(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([': depth += 1
        if ch in ')]': depth -= 1
        if ch == ',' and depth == 0: out.append(cur); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur)
    return [x.strip() for x in out]


def named(args):
    d = {}
    for a in args:
        k, _, v = a.partition(':'); d[k.strip()] = v.strip()
    return d


def point(s, env):
    m = re.match(r'CGPoint\((.*)\)$', s.strip())
    d = named(split_args(m.group(1)))
    return (py_expr(d['x'], env), py_expr(d['y'], env))


def functions(src):
    out = {}
    for m in re.finditer(r'class func draw(\w+)\((.*?)\) \{\n(.*?)\n    \}\n', src, re.S):
        out[m.group(1)] = (m.group(2), m.group(3))
    return out


class Frame:                         # a "let frame = CGRect(...)" of LanesStyleKit: its minX / minY are what the drawings use
    def __init__(self, x, y): self.minX, self.minY = x, y


def run(body, params=None):
    env = dict({'flipHorizontally': False}, **(params or {}))
    stack, T = [], Affine()
    paths, shapes = {}, []
    color = {'fill': 'primary', 'stroke': 'primary'}
    dash = None
    for raw in body.split('\n'):
        line = raw.strip()
        if not line or line.startswith('//'): continue
        if line.startswith('let context') or line.startswith('let resizedFrame'): continue
        m = re.match(r'let (\w+): CGFloat = (.*)$', line)
        if m: env[m.group(1)] = py_expr(m.group(2), env); continue
        m = re.match(r'let (\w+) = CGRect\((.*)\)$', line)
        if m:
            r = named(split_args(m.group(2))); env[m.group(1)] = Frame(py_expr(r['x'], env), py_expr(r['y'], env)); continue
        if line == 'context.saveGState()': stack.append((T, dash)); continue
        if line == 'context.restoreGState()': T, dash = stack.pop(); continue
        m = re.match(r'context\.translateBy\(x: (.*), y: (.*)\)$', line)
        if m:
            if 'resizedFrame' in line: continue
            T = T.mul(Affine((1, 0, 0, 1, py_expr(m.group(1), env), py_expr(m.group(2), env)))); continue
        m = re.match(r'context\.scaleBy\(x: (.*), y: (.*)\)$', line)
        if m:
            if 'resizedFrame' in line: continue
            T = T.mul(Affine((py_expr(m.group(1), env), 0, 0, py_expr(m.group(2), env), 0, 0))); continue
        m = re.match(r'context\.rotate\(by: (.*)\)$', line)
        if m:
            a = py_expr(m.group(1), env); c, s = math.cos(a), math.sin(a)
            T = T.mul(Affine((c, s, -s, c, 0, 0))); continue
        m = re.match(r'context\.setLineDash\(phase: (.*), lengths: \[(.*)\]\)$', line)
        if m: dash = [py_expr(x, env) * T.scale_len() for x in split_args(m.group(2))]; continue
        m = re.match(r'let (\w+) = UIBezierPath\(\)$', line)
        if m: paths[m.group(1)] = {'sub': [], 'width': 1, 'T': T}; continue
        m = re.match(r'let (\w+) = UIBezierPath\(roundedRect: CGRect\((.*?)\), byRoundingCorners: \[(.*?)\], cornerRadii: CGSize\((.*?)\)\)$', line)
        if m:
            r = named(split_args(m.group(2))); x, y, w, h = (py_expr(r[k], env) for k in ('x', 'y', 'width', 'height'))
            rad = py_expr(named(split_args(m.group(4)))['width'], env); corners = m.group(3)
            k = 0.5523 * rad
            tl, tr = '.topLeft' in corners, '.topRight' in corners
            seg = [('M', T.ap((x, y + h)))]
            if tl: seg += [('L', T.ap((x, y + rad))), ('C', T.ap((x, y + rad - k)), T.ap((x + rad - k, y)), T.ap((x + rad, y)))]
            else: seg += [('L', T.ap((x, y)))]
            if tr: seg += [('L', T.ap((x + w - rad, y))), ('C', T.ap((x + w - rad + k, y)), T.ap((x + w, y + rad - k)), T.ap((x + w, y + rad)))]
            else: seg += [('L', T.ap((x + w, y)))]
            seg += [('L', T.ap((x + w, y + h))), ('Z',)]
            paths[m.group(1)] = {'sub': [seg], 'width': 1, 'T': T}; continue
        m = re.match(r'let (\w+) = UIBezierPath\(rect: CGRect\((.*?)\)\)$', line)
        if m:
            r = named(split_args(m.group(2))); x, y, w, h = (py_expr(r[k], env) for k in ('x', 'y', 'width', 'height'))
            paths[m.group(1)] = {'sub': [[('M', T.ap((x, y))), ('L', T.ap((x + w, y))), ('L', T.ap((x + w, y + h))), ('L', T.ap((x, y + h))), ('Z',)]], 'width': 1, 'T': T}; continue
        m = re.match(r'let (\w+) = UIBezierPath\(ovalIn: CGRect\((.*?)\)\)$', line)
        if m:
            r = named(split_args(m.group(2))); x, y, w, h = (py_expr(r[k], env) for k in ('x', 'y', 'width', 'height'))
            cx, cy, rx, ry = x + w / 2, y + h / 2, w / 2, h / 2; k = 0.5523
            pts = [(cx + rx, cy), (cx, cy + ry), (cx - rx, cy), (cx, cy - ry)]          # right, bottom, left, top
            ctl = [((cx + rx, cy + k * ry), (cx + k * rx, cy + ry)), ((cx - k * rx, cy + ry), (cx - rx, cy + k * ry)),
                   ((cx - rx, cy - k * ry), (cx - k * rx, cy - ry)), ((cx + k * rx, cy - ry), (cx + rx, cy - k * ry))]
            seg = [('M', T.ap(pts[0]))]
            for i in range(4):
                seg.append(('C', T.ap(ctl[i][0]), T.ap(ctl[i][1]), T.ap(pts[(i + 1) % 4])))
            seg.append(('Z',))
            paths[m.group(1)] = {'sub': [seg], 'width': 1, 'T': T}; continue
        m = re.match(r'(\w+)\.move\(to: (.*)\)$', line)
        if m: paths[m.group(1)]['sub'].append([('M', T.ap(point(m.group(2), env)))]); continue
        m = re.match(r'(\w+)\.addLine\(to: (.*)\)$', line)
        if m: paths[m.group(1)]['sub'][-1].append(('L', T.ap(point(m.group(2), env)))); continue
        m = re.match(r'(\w+)\.addCurve\((.*)\)$', line)
        if m:
            d = named(split_args(m.group(2)))
            paths[m.group(1)]['sub'][-1].append(('C', T.ap(point(d['controlPoint1'], env)), T.ap(point(d['controlPoint2'], env)), T.ap(point(d['to'], env)))); continue
        m = re.match(r'(\w+)\.close\(\)$', line)
        if m:
            if paths[m.group(1)]['sub'] and paths[m.group(1)]['sub'][-1][-1] != ('Z',): paths[m.group(1)]['sub'][-1].append(('Z',))
            continue
        m = re.match(r'(\w+)\.lineWidth = (.*)$', line)
        if m: paths[m.group(1)]['width'] = py_expr(m.group(2), env); continue
        if re.match(r'\w+\.line(JoinStyle|CapStyle) = ', line) or 'usesEvenOddFillRule' in line: continue
        m = re.match(r'(primaryColor|secondaryColor|\w+)\.set(Fill|Stroke)\(\)$', line)
        if m: color[m.group(2).lower()] = 'secondary' if m.group(1) == 'secondaryColor' else 'primary'; continue
        m = re.match(r'(\w+)\.(fill|stroke)\(\)$', line)
        if m:
            pth = paths[m.group(1)]
            shapes.append({'kind': m.group(2), 'color': color[m.group(2)], 'width': pth['width'] * pth['T'].scale_len(),
                           'dash': dash if m.group(2) == 'stroke' else None, 'subpaths': [list(sp) for sp in pth['sub']]})
            continue
        raise ValueError('not understood: ' + line)
    return shapes


def to_svg_d(shape, tf=lambda p: p):
    out = []
    for sp in shape['subpaths']:
        for seg in sp:
            if seg[0] == 'Z': out.append('Z')
            elif seg[0] in 'ML': out.append('%s%.2f,%.2f' % ((seg[0],) + tf(seg[1])))
            else: out.append('C%.2f,%.2f %.2f,%.2f %.2f,%.2f' % (tf(seg[1]) + tf(seg[2]) + tf(seg[3])))
    return ' '.join(out)


if __name__ == '__main__':
    import sys
    src = open(sys.argv[1]).read()
    for name, (args, body) in functions(src).items():
        params = {'roundabout_angle': 90, 'roundabout_radius': 6.5} if 'roundabout' in args else {}
        shapes = run(body, params)
        print(name, [(s['kind'], s['color'], round(s['width'], 1), len(s['subpaths'])) for s in shapes])
