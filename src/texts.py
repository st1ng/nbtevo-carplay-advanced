#!/usr/bin/env python3
"""Writes CarPlayMenu_ALL_Texts.zip (the plugin's text database, TextDBFile in CarPlayMenu.manifest): one <language>.txt per HMI
language, lines "id=text"; 100000 is the language name. The ids are the textId of the textIdModels in CarPlayMenu.xml. English in
every language. Run by build.sh."""
import sys, zipfile

TEXTS = {
    1: 'CarPlay Advanced Settings',
    # rows, in menu order (submenus indented)
    2: 'Full-screen CarPlay',
    5: 'CarPlay directions in HUD',
    18: 'HUD effects',
    19: 'Fill toward the turn',
    20: 'Blink near the turn',
    21: 'Effect colour',
    22: 'Orange',
    23: 'Grey',
    24: 'White',
    9: 'iDrive control',
    3: 'Enable knob tilts',
    10: 'Touchpad',
    4: 'Enable touchpad',
    11: 'Tap touchpad as click',
    12: 'Touchpad sensitivity',
    13: 'Low',
    14: 'Medium',
    15: 'High',
    7: 'Two fingers tap to back',
    16: 'Enable rotation as zoom',
    6: 'Restart to apply',
}
LANGS = ['de', 'en-UK', 'en-US', 'es-ES', 'es-US', 'fr-CA', 'fr-FR', 'it', 'nl-NL', 'pt', 'ru']


def main(out):
    body = '100000=English\n' + ''.join('%d=%s\n' % (k, TEXTS[k]) for k in sorted(TEXTS))
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for lang in LANGS:
            z.writestr(zipfile.ZipInfo('%s.txt' % lang, (2026, 11, 11, 0, 0, 0)), body.encode('utf-8'), zipfile.ZIP_DEFLATED)


if __name__ == '__main__':
    main(sys.argv[1])
