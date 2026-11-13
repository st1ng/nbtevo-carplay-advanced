#!/usr/bin/env python3
"""Checks that the menu's three halves agree: plugin/CarPlayMenu.xml (RHMI description), src/texts.py (text ids) and src/cpm.cpp
(handlers and models). Prints the menu tree and every problem; exit status 1 if there is one.

    python3 .claude/skills/hmi-menu/check_menu.py           # the full menu, plugin/CarPlayMenu.xml
    python3 .claude/skills/hmi-menu/check_menu.py --hud     # the HUD-only menu, plugin/CarPlayMenu-hud.xml (made by build.sh)

Checks: ids unique across actions, models, states and components; every component's action / model / textModel exists and has the
right kind (checkbox and radiobutton: raAction + raBoolModel; button: hmiAction to an existing hmiState, or raAction); every
textIdModel's textId is in texts.py; every raAction has a handler in cpm.cpp (g_options, kSens, kColours, kActRestart, or the entry button's
combined action) and every raBoolModel is set by cpm.cpp; every hmiState is reachable from the entry button."""
import ast
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main():
    problems = []
    hud = '--hud' in sys.argv[1:]
    app = ET.parse(ROOT / 'plugin' / ('CarPlayMenu-hud.xml' if hud else 'CarPlayMenu.xml')).getroot().find('pluginApp')
    actions, models, states, ids = {}, {}, {}, {}

    def note(kind, el):
        i = int(el.get('id'))
        if i in ids:
            problems.append('id %d used twice (%s and %s)' % (i, ids[i], kind))
        ids[i] = kind
        return i

    for el in app.find('actions').iter():
        if el.tag in ('raAction', 'hmiAction', 'combinedAction'):
            actions[note(el.tag, el)] = el
    for el in app.find('models'):
        models[note(el.tag, el)] = el
    for st in app.find('hmiStates'):
        states[note('hmiState', st)] = st
        for c in st.find('components'):
            note('%s in state %s' % (c.tag, st.get('id')), c)
    entry = app.find('entryButton')
    note('entryButton', entry)

    src = (ROOT / 'src' / 'texts.py').read_text()
    texts = ast.literal_eval(re.search(r'TEXTS = (\{.*?\n\})', src, re.S).group(1))
    cpp = (ROOT / 'src' / 'cpm.cpp').read_text()
    opt = [(int(a), int(m), f) for f, a, m in re.findall(r'\{ "(\w+)", (\d+), (\d+), "[^"]*", false \}', cpp)]
    sens = [(int(a), int(m)) for a, m in re.findall(r'\{ (\d+), (\d+), \d+, "\w+"(?:, 0)? \}', cpp)]  # kSens, kColours
    restart = int(re.search(r'kActRestart = (\d+)', cpp).group(1))
    handled = {a for a, _, _ in opt} | {a for a, _ in sens} | {restart}
    set_models = {m for _, m, _ in opt} | {m for _, m in sens}

    def text_of(model_id):
        m = models.get(model_id)
        if m is None or m.tag != 'textIdModel':
            return None
        return texts.get(int(m.get('textId')))

    for mid, m in models.items():
        if m.tag == 'textIdModel' and int(m.get('textId')) not in texts:
            problems.append('textIdModel %d: textId %s not in texts.py' % (mid, m.get('textId')))
        if m.tag == 'raBoolModel' and mid not in set_models:
            problems.append('raBoolModel %d is never set by cpm.cpp (g_options / kSens / kColours)' % mid)

    combined = set()
    for aid, a in actions.items():
        if a.tag == 'combinedAction':
            combined |= {int(x.get('id')) for x in a.iter() if x is not a and x.get('id')}
    for aid, a in actions.items():
        if a.tag == 'raAction' and aid not in handled and aid not in combined:
            problems.append('raAction %d has no handler in cpm.cpp' % aid)
    for a, m, f in opt:
        if hud and a not in actions and m not in models:
            continue  # an option the HUD-only menu does not show
        if a not in actions or actions[a].tag != 'raAction':
            problems.append('cpm.cpp option %s: action %d is not an raAction in the XML' % (f, a))
        if m not in models or models[m].tag != 'raBoolModel':
            problems.append('cpm.cpp option %s: model %d is not an raBoolModel in the XML' % (f, m))

    if int(entry.get('model')) not in models:
        problems.append('entryButton model %s missing' % entry.get('model'))
    reach, todo = set(), []
    ea = actions.get(int(entry.get('action')))
    if ea is not None:
        todo += [int(x.get('target')) for x in ea.iter() if x.tag == 'hmiAction']

    tree = []

    def walk(sid, depth):
        if sid in reach:
            return
        reach.add(sid)
        st = states.get(sid)
        if st is None:
            problems.append('hmiAction target %d is not an hmiState' % sid)
            return
        tree.append('%s[%d] %s' % ('  ' * depth, sid, text_of(int(st.get('textModel', -1))) or st.get('text')))
        for c in st.find('components'):
            a, m, tm = actions.get(int(c.get('action', -1))), int(c.get('model', -1)), c.get('textModel')
            if a is None:
                problems.append('state %d: %s %s: action %s missing' % (sid, c.tag, c.get('id'), c.get('action')))
                continue
            if m not in models:
                problems.append('state %d: %s %s: model %d missing' % (sid, c.tag, c.get('id'), m))
            if c.tag in ('checkbox', 'radiobutton'):
                if a.tag != 'raAction' or models.get(m) is None or models[m].tag != 'raBoolModel':
                    problems.append('state %d: %s %s needs an raAction and an raBoolModel' % (sid, c.tag, c.get('id')))
                if tm is None or models.get(int(tm)) is None or models[int(tm)].tag != 'textIdModel':
                    problems.append('state %d: %s %s: textModel %s is not a textIdModel' % (sid, c.tag, c.get('id'), tm))
                label = text_of(int(tm)) if tm else None
                tree.append('%s%s %s  (action %d, model %d)' % ('  ' * (depth + 1), '[x]' if c.tag == 'checkbox' else '(o)', label,
                                                                int(c.get('action')), m))
            elif c.tag == 'button':
                label = text_of(m)
                if a.tag == 'hmiAction':
                    tree.append('%s> %s  (action %d -> state %s)' % ('  ' * (depth + 1), label, int(c.get('action')), a.get('target')))
                    walk(int(a.get('target')), depth + 2)
                else:
                    tree.append('%s* %s  (action %d)' % ('  ' * (depth + 1), label, int(c.get('action'))))
            else:
                problems.append('state %d: unknown component %s' % (sid, c.tag))

    for t in todo:
        walk(t, 0)
    for sid in states:
        if sid not in reach:
            problems.append('hmiState %d is not reachable from the entry button' % sid)

    print('\n'.join(tree))
    used = sorted(ids)
    print('\nids in use: %s' % ', '.join(map(str, used)))
    print('free from %d up' % (used[-1] + 1))
    if problems:
        print('\nPROBLEMS:\n  ' + '\n  '.join(problems))
        sys.exit(1)
    print('\nOK: XML, texts.py and cpm.cpp agree')


if __name__ == '__main__':
    main()
