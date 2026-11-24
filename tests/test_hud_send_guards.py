"""Ownership checks at submission time and positive-only layout recovery.

The real HUD header runs with the existing deterministic host transport. No
cluster graphics or physical connection lifecycle is emulated.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_hud_handover import HARNESS as HANDOVER_HARNESS, STREET
from test_hud_init import ROOT


HARNESS = HANDOVER_HARNESS.replace(
    'static int sSend(void *, int) { return 0; }',
    '''static bool failNextSend;
static int sSend(void *, int) {
  if (g_id == kReqProcessMessage && failNextSend) {
    failNextSend = false;
    puts("FAILED_SEND");
    return -1;
  }
  return 0;
}''')
HARNESS = HARNESS.replace('int scriptedSleep(unsigned n) {', '''static bool lockWitness;
int scriptedSleep(unsigned n) {
  if (lockWitness && n == 1000) {
    lockWitness = false;
    const unsigned char street[] = {0,2,0,0,0x41,0,0x41,0,0x44,0,0};
    inject(0x82, 0, 0x2000, g_host, 0x210, 6, street, sizeof street);
    g_lock = 0;
    puts("FOREIGN_DURING_LOCK_WAIT");
    return 0;
  }
''')
# The layout switch is read by the actual detectLayout, rather than directly
# assigning its private flags. All other configuration files remain absent.
HARNESS = HARNESS.replace('extern "C" int open(const char *path, int, ...) {',
                        'static char manualLayout[8], manualBox[64];\n'
                        'static void setManual(const char *p) { if (!strcmp(p, \"auto\")) manualLayout[0]=0; '
                        'else snprintf(manualLayout, sizeof manualLayout, \"%s\", p); }\nextern "C" int open(const char *path, int, ...) {\n'
                        '  if (!strcmp(path, "/var/opt/hmi/data/rw/carplaymenu/hud_layout") && manualLayout[0]) return 902;\n'
                        '  if (!strcmp(path, "/var/opt/hmi/data/rw/carplaymenu/hud_box") && manualBox[0]) return 903;')
HARNESS = HARNESS.replace('extern "C" long read(int fd, void *to, unsigned long cap) {',
                        'extern "C" long read(int fd, void *to, unsigned long cap) {\n'
                        '  if (fd == 903) { unsigned n = strlen(manualBox); if (n > cap) n = cap; '
                        'memcpy(to, manualBox, n); return n; }\n'
                        '  if (fd == 902) { unsigned n = strlen(manualLayout); if (n > cap) n = cap; '
                        'memcpy(to, manualLayout, n); return n; }')
HARNESS = HARNESS.replace('else if (!strcmp(cmd, "poll"))', '''else if (!strcmp(cmd, "locked")) { lockWitness = true; g_lock = 1; }
    else if (!strcmp(cmd, "fail")) failNextSend = true;
    else if (!strcmp(cmd, "manual")) setManual(rest);
    else if (!strcmp(cmd, "compact")) { strcpy(manualBox, "36 16 89 69\\n"); chooseBox(); }
    else if (!strcmp(cmd, "poll"))''')

HARNESS = HARNESS.replace('else if (!strncmp(line, \"guide \", 6))',
                        'else if (!strncmp(line, \"manual \", 7)) setManual(p);\n'
                        '      else if (!strncmp(line, \"guide \", 6))')

POSITIVE_6WB = 'foreign 0 101 205e c04 d 00 1f 00 dd 00 9e'
POSITIVE_6WA = 'foreign 0 101 205e c04 d 00 05 00 7e 00 4d'


class HudSendGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('clang++'):
            raise unittest.SkipTest('clang++ not found')
        cls.tmp = tempfile.TemporaryDirectory(prefix='hudsends-')
        source = Path(cls.tmp.name) / 'h.cpp'
        source.write_text(HARNESS)
        cls.exe = Path(cls.tmp.name) / 'h'
        subprocess.run(['clang++', '-std=c++17', '-w', '-I', str(ROOT), '-I', str(ROOT / 'src'),
                        '-o', str(cls.exe), str(source)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_lines(self, commands):
        return subprocess.run([str(self.exe)], input='\n'.join(commands) + '\n', capture_output=True,
                              text=True, check=True).stdout.splitlines()

    def cycle(self, commands, before=()):
        return self.run_lines(list(before) + ['cycle'] + commands + ['stop'])

    def test_foreign_owner_acquired_during_lock_wait_blocks_submission(self):
        out = self.run_lines(['clock 1000', 'locked', 'draw', 'snapshot'])
        self.assertIn('FOREIGN_DURING_LOCK_WAIT', out)
        after = out[out.index('FOREIGN_DURING_LOCK_WAIT') + 1:]
        self.assertFalse(any(s.startswith(('00 ', '02 ')) for s in after), after)

    def test_blocked_clear_does_not_hide_real_foreign_mutation_as_an_echo(self):
        out = self.run_lines(['clock 1000', 'draw', 'foreign 0 2000 101 ' + STREET,
                              'clock 1100', 'clear', 'clock 4000', 'foreign 0 2000 101 204 6 00 01 00',
                              'clock 6500', 'work', 'snapshot'])
        self.assertIn('SNAP shown=0 layout=6wa picture=5/6 self=205e', out)

    def test_failed_submission_does_not_hide_real_foreign_mutation_as_an_echo(self):
        out = self.run_lines(['clock 1000', 'fail', 'draw', 'foreign 0 2000 101 220 6 00 05',
                              'clock 1002', 'work', 'snapshot'])
        self.assertIn('FAILED_SEND', out)
        self.assertIn('SNAP shown=0 layout=6wa picture=5/6 self=205e', out)

    def test_late_positive_sixwb_answer_selects_real_plane(self):
        out = self.cycle(['tick 0', 'clock 6000', POSITIVE_6WB, 'tick 6002', 'snapshot'])
        self.assertIn('SNAP shown=1 layout=6wb picture=31/32 self=205e', out)
        self.assertTrue(any(s.startswith('00 21d 00 1f') or s.startswith('00 21d 00 20') for s in out), out)

    def test_positive_picture_five_corrects_an_automatic_sixwb_selection(self):
        out = self.cycle(['tick 0', 'clock 6000', POSITIVE_6WB, 'tick 6002', 'snapshot',
                          POSITIVE_6WA, 'tick 6004', 'snapshot'])
        self.assertIn('SNAP shown=1 layout=6wb picture=31/32 self=205e', out)
        self.assertIn('SNAP shown=1 layout=6wa picture=5/6 self=205e', out)

    def test_positive_answers_preserve_explicit_layout_switch(self):
        for layout, packet, expected in (
                ('6wa', POSITIVE_6WB, 'SNAP shown=1 layout=6wa picture=5/6 self=205e'),
                ('6wb', POSITIVE_6WA, 'SNAP shown=1 layout=6wb picture=31/32 self=205e')):
            with self.subTest(layout=layout):
                out = self.cycle(['tick 0', 'clock 6000', packet, 'tick 6002', 'snapshot'],
                                 before=['manual ' + layout])
                self.assertIn(expected, out)

    def test_sixwb_plane_wire_geometry_ignores_cached_compact_box(self):
        standard = self.cycle(['tick 0'], before=['manual 6wb'])
        compact = self.cycle(['tick 0'], before=['compact', 'manual 6wb'])
        polygon = lambda lines: [s for s in lines if s.startswith(('00 21d ', '02 21d '))]
        self.assertTrue(polygon(standard), standard)
        self.assertEqual(polygon(compact), polygon(standard))

    def test_last_fit_survives_automatic_layout_correction(self):
        out = self.cycle(['tick 0', 'clock 6000', POSITIVE_6WB, 'tick 6002', 'snapshot',
                          POSITIVE_6WA, 'tick 6004', 'snapshot'],
                         before=['answer 1 126 77', 'poll 0', 'answer 0 0 0',
                                 'registry 0 0', 'registry 101 0'])
        self.assertIn('SNAP shown=1 layout=6wb picture=31/32 self=205e', out)
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')

    def test_manual_compact_box_survives_late_layout_correction(self):
        out = self.cycle(['tick 0', 'clock 6000', POSITIVE_6WB, 'tick 6002', 'snapshot',
                          POSITIVE_6WA, 'tick 6004', 'snapshot'], before=['compact'])
        self.assertIn('SNAP shown=1 layout=6wb picture=31/32 self=205e', out)
        self.assertIn('SNAP shown=1 layout=6wa picture=5/6 self=205e', out)
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')

    def test_manual_layout_changes_are_reloaded_on_a_resource_epoch(self):
        out = self.cycle(['tick 0', 'clock 6000', POSITIVE_6WB, 'tick 6002', 'snapshot',
                          'manual 6wa', 'registry 0 0', 'registry 101 0', 'tick 10000', 'snapshot',
                          'manual auto', 'registry 0 0', 'registry 101 0', 'tick 14000',
                          'clock 20000', POSITIVE_6WB, 'tick 20002', 'snapshot'])
        snapshots = [s for s in out if s.startswith('SNAP ')]
        self.assertEqual(snapshots[:3], [
            'SNAP shown=1 layout=6wb picture=31/32 self=205e',
            'SNAP shown=1 layout=6wa picture=5/6 self=205e',
            'SNAP shown=1 layout=6wb picture=31/32 self=205e'])

    def test_same_address_resource_epoch_repeats_layout_probe(self):
        out = self.cycle(['tick 0', 'registry 0 0', 'registry 101 0', 'tick 10000'])
        # Both accepted wire request forms for handle1f, once per resource epoch.
        probes = [s for s in out if s in ('00 c04 00 1f', '00 c04 00 1f 01')]
        self.assertEqual(len(probes), 4, out)


if __name__ == '__main__':
    unittest.main()
