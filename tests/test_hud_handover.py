"""HUD ownership and measurement recovery through the real callback and worker.

Only the DSI transport, clock and route file are stubbed. The continuous scripts
keep one worker alive across handovers; they do not emulate cluster graphics.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_hud_init import OTHER
from test_hud_size_recovery import EXHAUST, RECOVERY_HARNESS, ROOT, reply


HARNESS = RECOVERY_HARNESS.replace(
    'static unsigned workerSleeps; static bool workerStep;',
    'static unsigned workerSleeps; static bool workerStep, continuous;\n'
    'static int routeState = 1; int scriptedSleep(unsigned);')
HARNESS = HARNESS.replace('''int usleep(unsigned n) {
  if (workerStep && n >= 300000 && ++workerSleeps == 2) throw 1;
  return 0;
}''', 'int usleep(unsigned n) { return scriptedSleep(n); }')
HARNESS = HARNESS.replace('static int sSend(void *, int) { return 0; }',
                        'static int sSend(void *, int) { return 0; }\nstatic void sNotify(void *, unsigned) {}')
HARNESS = HARNESS.replace('S.streamCtor = sCtor;', 'S.setNotif = sNotify; S.streamCtor = sCtor;')
HARNESS = HARNESS.replace('static unsigned g_id;', 'static unsigned g_id, interruptFkt;')
HARNESS = HARNESS.replace('''  printf("\\n");
  const unsigned char *d = p + 36;''', '''  printf("\\n");
  if (interruptFkt && fkt == interruptFkt) {
    interruptFkt = 0;
    const unsigned char street[] = {0,2,0,0,0x41,0,0x41,0,0x44,0,0};
    inject(0x82, inst, 0x2000, g_host, 0x210, 6, street, sizeof street);
    puts("FOREIGN_INTERRUPT");
  }
  const unsigned char *d = p + 36;''')
HARNESS = HARNESS.replace('return !strcmp(path, kPps) ? 901 : -1;',
                        'return !strcmp(path, kPps) && routeState >= 0 ? 901 : -1;')
HARNESS = HARNESS.replace(
    'const char *guide = "route_state::1\\nmaneuver_index::0\\nmaneuver_type::3\\ndist_to_maneuver::30\\n";',
    'char guide[128]; snprintf(guide, sizeof guide, '
    '"route_state::%d\\nmaneuver_index::0\\nmaneuver_type::3\\ndist_to_maneuver::30\\n", routeState);')

# These helpers construct the messages the real DSI callbacks receive. They do
# not assign the renderer's ownership, measurement or layout state directly.
HELPERS = r'''
static void foreign(const char *s) {
  unsigned inst, src, dst, fkt, op; int k = 0;
  sscanf(s, "%x %x %x %x %x %n", &inst, &src, &dst, &fkt, &op, &k);
  unsigned char d[64]; unsigned n; hex(s + k, d, &n);
  inject(0x82, inst, src, dst, fkt, op, d, n);
}
static void registry(const char *s) {
  unsigned t0=0, t2=0; sscanf(s, "%x %x", &t0, &t2);
  unsigned char data[12]={0}; unsigned count=0;
  if(t0) { unsigned i=4+4*count++; data[i]=t0; data[i+1]=t0>>8; data[i+2]=0; data[i+3]=0x82; }
  if(t2) { unsigned i=4+4*count++; data[i]=t2; data[i+1]=t2>>8; data[i+2]=2; data[i+3]=0x82; }
  data[0]=count; char stream[0x50]={0};
  *(unsigned *)(stream+0x48)=kAttrRegistry; *(unsigned *)(stream+0x08)=4+4*count;
  *(const unsigned char **)(stream+0x10)=data; onData(0, stream);
}
static void registered(const char *s) {
  unsigned self; sscanf(s, "%x", &self);
  unsigned char data[6]={8,0,0,0,(unsigned char)self,(unsigned char)(self>>8)};
  char stream[0x50]={0}; *(unsigned *)(stream+0x48)=kInfoClientResponse; *(unsigned *)(stream+0x08)=6;
  *(const unsigned char **)(stream+0x10)=data; onData(0, stream);
}
static void snapshot() {
  printf("SNAP shown=%d layout=%s picture=%u/%u self=%x\n",
         g_shown, g_6wb ? "6wb" : "6wa", hBufA, hBufB, g_self);
  boxLine();
}
int scriptedSleep(unsigned n) {
  if (continuous && n >= 300000) {
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
      line[strcspn(line, "\n")]=0;
      const char *p = strchr(line, ' ') ? strchr(line, ' ')+1 : "";
      if (!strncmp(line, "tick ", 5)) {
        sscanf(p, "%u", &testNow); printf("TICK %u\n", testNow); return 0;
      }
      if (!strncmp(line, "foreign ", 8)) foreign(p);
      else if (!strncmp(line, "clock ", 6)) sscanf(p, "%u", &testNow);
      else if (!strncmp(line, "answer ", 7)) sscanf(p, "%d %u %u", &g_sizeMode, &g_w, &g_h);
      else if (!strncmp(line, "guide ", 6)) routeState=atoi(p);
      else if (!strncmp(line, "registry ", 9)) registry(p);
      else if (!strncmp(line, "registered ", 11)) registered(p);
      else if (!strcmp(line, "detach")) onDetached(0);
      else if (!strcmp(line, "attached")) onAttached(0);
      else if (!strncmp(line, "setup ", 6)) {
        unsigned inst, fkt; int k=0; sscanf(p, "%x %x %n", &inst, &fkt, &k);
        unsigned char d[32]; unsigned dn; hex(p+k, d, &dn);
        inject(0x82, inst, 0x101, 0x201f, fkt, 0xd, d, dn);
      }
      else if (!strcmp(line, "snapshot")) snapshot();
      else if (!strcmp(line, "stop")) throw 1;
      else { fprintf(stderr, "unknown script command: %s\n", line); abort(); }
    }
    throw 1;
  }
  if (workerStep && n >= 300000 && ++workerSleeps == 2) throw 1;
  return 0;
}
'''
HARNESS = HARNESS.replace('int main() {', HELPERS + '\nint main() {')
HARNESS = HARNESS.replace(
    'else if (!strcmp(cmd, "poll"))',
    '''else if (!strcmp(cmd, "foreign")) foreign(rest);
    else if (!strcmp(cmd, "registry")) registry(rest);
    else if (!strcmp(cmd, "registered")) registered(rest);
    else if (!strcmp(cmd, "detach")) onDetached(0);
    else if (!strcmp(cmd, "attached")) onAttached(0);
    else if (!strcmp(cmd, "clock")) sscanf(rest, "%u", &testNow);
    else if (!strcmp(cmd, "interrupt")) sscanf(rest, "%x", &interruptFkt);
    else if (!strcmp(cmd, "clear")) drawClear();
    else if (!strcmp(cmd, "snapshot")) snapshot();
    else if (!strcmp(cmd, "cycle")) {
      continuous=true; g_enabled=1;
      try { worker(0); } catch (int) {}
      continuous=false; snapshot();
    }
    else if (!strcmp(cmd, "poll"))''')

STREET = '210 6 00 02 00 00 41 00 41 00 44 00 00'  # valid UTF-16BE AAD, unlike our empty road


class HudHandover(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('clang++'):
            raise unittest.SkipTest('clang++ not found')
        cls.tmp = tempfile.TemporaryDirectory(prefix='hudhandover-')
        source = Path(cls.tmp.name) / 'h.cpp'
        source.write_text(HARNESS)
        cls.exe = Path(cls.tmp.name) / 'h'
        subprocess.run(['clang++', '-std=c++17', '-w', '-I', str(ROOT), '-I', str(ROOT / 'src'),
                        '-o', str(cls.exe), str(source)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_lines(self, commands):
        run = subprocess.run([str(self.exe)], input='\n'.join(commands) + '\n', text=True,
                             capture_output=True, check=True)
        return run.stdout.splitlines()

    def cycle(self, commands):
        return self.run_lines(['cycle'] + commands + ['stop'])

    def tick_calls(self, lines, tick):
        start = lines.index('TICK %u' % tick) + 1
        end = next((i for i in range(start, len(lines)) if lines[i].startswith('TICK ')), len(lines))
        return [line for line in lines[start:end] if line[:2] in ('00', '02')]

    def polygons(self, lines):
        return [line for line in lines if ' 21d ' in line and not line.startswith('#')]

    def queries(self, lines):
        return [line for line in lines if ' c04 ' in line and not line.startswith('#')]

    def test_foreign_size_query_cannot_yield_or_select_sixwb(self):
        out = self.cycle(['tick 0', 'clock 6000', 'foreign 0 2000 101 c04 6 00 1f',
                          'tick 6002', 'snapshot', 'tick 6004', 'tick 12000'])
        self.assertTrue(all('layout=6wa picture=5/6' in s for s in out if s.startswith('SNAP ')), out)
        self.assertFalse(any('CarPlay guidance yields' in s for s in out), out)

    def test_foreign_query_on_other_terminal_cannot_yield_primary(self):
        out = self.cycle(['tick 0', 'clock 6000', 'foreign 2 2000 102 c04 6 00 05', 'tick 6002', 'snapshot'])
        self.assertIn('SNAP shown=1 layout=6wa picture=5/6 self=205e', out)
        self.assertFalse(any('CarPlay guidance yields' in s for s in out), out)

    def test_foreign_drawing_on_other_host_cannot_yield_primary(self):
        out = self.cycle(['tick 0', 'clock 6000', 'foreign 0 2000 102 ' + STREET, 'tick 6002', 'snapshot'])
        self.assertIn('SNAP shown=1 layout=6wa picture=5/6 self=205e', out)
        self.assertFalse(any('CarPlay guidance yields' in s for s in out), out)

    def test_real_foreign_drawing_yields_then_resumes_without_erasing_owner(self):
        out = self.cycle(['tick 0', 'clock 6000', 'foreign 0 2000 101 ' + STREET,
                          'tick 6002', 'snapshot', 'tick 11001', 'snapshot'])
        self.assertIn('SNAP shown=0 layout=6wa picture=5/6 self=205e', out)
        self.assertIn('SNAP shown=1 layout=6wa picture=5/6 self=205e', out)
        held = self.tick_calls(out, 6002)
        self.assertFalse(any(' 220 ' in s or ' 21d ' in s or ' 204 ' in s for s in held), held)
        self.assertTrue(self.polygons(self.tick_calls(out, 11001)), out)

    def test_new_map_does_not_clear_foreign_owners_shared_buffers(self):
        # A complete new native setup is distinct from ordinary AAD drawing.
        out = self.cycle(['tick 0', 'clock 6000', 'foreign 0 2000 101 ' + STREET] +
                         ['setup 0 ' + s for s in OTHER] + ['tick 6002', 'snapshot', 'tick 11001'])
        held = self.tick_calls(out, 6002)
        self.assertFalse(any(' 220 ' in s or ' 204 ' in s or ' 210 ' in s for s in held), held)
        self.assertTrue(self.polygons(self.tick_calls(out, 11001)), out)

    def test_echo_on_primary_does_not_hide_foreign_drawing_on_mirror(self):
        out = self.run_lines(['clock 1000', 'mirror 0', 'draw', 'mirror 1',
                              'foreign 2 2000 102 204 6 00 01 01', 'clock 1002', 'work', 'snapshot'])
        self.assertIn('SNAP shown=0 layout=6wa picture=5/6 self=205e', out)

    def test_real_echo_on_same_terminal_does_not_force_yield(self):
        out = self.run_lines(['clock 1000', 'draw', 'foreign 0 2000 101 204 6 00 01 01',
                              'clock 1002', 'work', 'snapshot'])
        self.assertIn('SNAP shown=1 layout=6wa picture=5/6 self=205e', out)
        self.assertFalse(any('CarPlay guidance yields' in s for s in out), out)

    def test_foreign_drawing_mid_batch_stops_the_remaining_mutations(self):
        for now in (0, 1000, 1001, 0xfffffffe, 0xffffffff):
            with self.subTest(now=now):
                out = self.run_lines(['clock %u' % now, 'interrupt 21d', 'draw', 'snapshot'])
                self.assertIn('FOREIGN_INTERRUPT', out)
                after = out[out.index('FOREIGN_INTERRUPT') + 1:]
                self.assertFalse(any(s[:2] in ('00', '02') for s in after), after)

    def test_foreign_drawing_mid_clear_stops_the_remaining_mutations(self):
        for now in (0, 1000, 1001, 0xfffffffe, 0xffffffff):
            with self.subTest(now=now):
                out = self.run_lines(['clock %u' % now, 'draw', 'interrupt 220', 'clear', 'snapshot'])
                self.assertIn('FOREIGN_INTERRUPT', out)
                after = out[out.index('FOREIGN_INTERRUPT') + 1:]
                self.assertFalse(any(s[:2] in ('00', '02') for s in after), after)

    def test_size_reply_does_not_clear_a_foreign_owner(self):
        out = self.run_lines(['poll 0', 'clock 1000', 'fxdraw', 'foreign 0 2000 101 ' + STREET,
                              reply(), 'poll 1200', 'snapshot'])
        after = out[out.index('TICK 1200') + 1:]
        self.assertFalse(any(' 220 ' in s or ' 204 ' in s or ' 210 ' in s for s in after), after)

    def test_same_address_registry_return_remeasures(self):
        out = self.run_lines(['answer 1 126 77', 'poll 0', 'registry 0 0', 'registry 101 0',
                              'answer 1 199 129', 'poll 10000'])
        self.assertEqual(out[-1], 'BOX 6wa')
        self.assertEqual(len(self.queries(out)), 4)

    def test_dsi_detach_and_new_client_remeasure_same_terminal(self):
        out = self.run_lines(['answer 1 126 77', 'poll 0', 'detach', 'attached', 'registered 2060',
                              'answer 1 199 129', 'poll 10000'])
        self.assertEqual(out[-1], 'BOX 6wa')
        self.assertEqual(len(self.queries(out)), 4)

    def test_resource_epoch_redraws_an_unchanged_route_without_size_ack(self):
        out = self.cycle(['answer 1 126 77', 'tick 0', 'answer 0 0 0',
                          'detach', 'attached', 'registered 2060', 'registry 101 0',
                          'tick 10000', 'snapshot'])
        restored = self.tick_calls(out, 10000)
        self.assertTrue(self.queries(restored), restored)
        self.assertTrue(self.polygons(restored), restored)
        self.assertIn('SNAP shown=1 layout=6wa picture=5/6 self=2060', out)

    def test_exhausted_budget_recovers_after_registry_return(self):
        out = self.run_lines(EXHAUST + ['registry 0 0', 'registry 101 0',
                                        'answer 1 126 77', 'poll 100000', 'poll 200000'])
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')
        self.assertEqual(len(self.queries(out)), 12)

    def test_new_route_recovers_exhausted_startup_without_periodic_polling(self):
        ticks = ['tick %u' % n for n in (0, 2000, 3000, 5000, 7000, 9000, 14000, 16000, 26000, 28000)]
        out = self.cycle(['guide -1'] + ticks + ['guide 1', 'answer 1 126 77',
                                               'tick 30000', 'tick 31000', 'snapshot', 'tick 100000'])
        self.assertIn('BOX fit 36 16 89 69', out)
        self.assertEqual(len(self.queries(out)), 16)  # four layout calls, ten startup calls, two recovered calls
        self.assertFalse(self.queries(self.tick_calls(out, 100000)), out)

    def test_foreign_handback_waits_through_loading(self):
        # Loading/Locating retain the old active route. A pending handback must
        # survive that hold instead of depending on a new-route edge.
        for state in (3, 4):
            with self.subTest(route_state=state):
                ticks = ['tick %u' % n for n in
                         (0, 2000, 3000, 5000, 7000, 9000, 14000, 16000, 26000, 28000)]
                out = self.cycle(ticks + ['foreign 0 2000 101 ' + STREET, 'tick 30000',
                                         'guide %u' % state, 'answer 1 126 77', 'tick 34000',
                                         'guide 1', 'tick 35000', 'snapshot', 'tick 100000'])
                self.assertFalse(self.queries(self.tick_calls(out, 34000)), out)
                self.assertEqual(len(self.queries(self.tick_calls(out, 35000))), 2, out)
                self.assertIn('BOX fit 36 16 89 69', out)
                self.assertEqual(len(self.queries(out)), 16)
                self.assertFalse(self.queries(self.tick_calls(out, 100000)), out)

    def test_foreign_handback_recovers_exhausted_measurement(self):
        ticks = ['tick %u' % n for n in (0, 2000, 3000, 5000, 7000, 9000, 14000, 16000, 26000, 28000)]
        out = self.cycle(ticks + ['foreign 0 2000 101 ' + STREET, 'tick 30000',
                                 'answer 1 126 77', 'tick 34000', 'tick 35000', 'snapshot', 'tick 100000'])
        self.assertIn('BOX fit 36 16 89 69', out)
        self.assertEqual(len(self.queries(out)), 16)
        self.assertFalse(self.queries(self.tick_calls(out, 100000)), out)


if __name__ == '__main__':
    unittest.main()
