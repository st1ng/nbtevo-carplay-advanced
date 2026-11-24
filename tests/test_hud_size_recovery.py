"""The real HUD header, with a deterministic clock and MOST replies (no head unit)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_hud_init import HARNESS, ROOT


RECOVERY_HARNESS = HARNESS.replace(
    'static unsigned g_id;',
    '''static unsigned testNow;
extern "C" int clock_gettime(int, hud_timespec *t) {
  t->sec = testNow / 1000; t->nsec = (testNow % 1000) * 1000000; return 0;
}
static unsigned g_id;''')
RECOVERY_HARNESS = RECOVERY_HARNESS.replace('extern "C" {', 'static unsigned workerSleeps; static bool workerStep;\nextern "C" {', 1)
RECOVERY_HARNESS = RECOVERY_HARNESS.replace('int usleep(unsigned) { return 0; }', '''int usleep(unsigned n) {
  if (workerStep && n >= 300000 && ++workerSleeps == 2) throw 1;
  return 0;
}''')
RECOVERY_HARNESS = RECOVERY_HARNESS.replace('int main() {', '''extern "C" int open(const char *path, int, ...) {
  return !strcmp(path, kPps) ? 901 : -1;
}
extern "C" long read(int fd, void *to, unsigned long cap) {
  const char *guide = "route_state::1\\nmaneuver_index::0\\nmaneuver_type::3\\ndist_to_maneuver::30\\n";
  unsigned long n = strlen(guide); if (n > cap) n = cap;
  if (fd != 901) return -1;
  memcpy(to, guide, n); return n;
}
extern "C" int close(int) { return 0; }
int main() {''')
RECOVERY_HARNESS = RECOVERY_HARNESS.replace(
    'else if (!strcmp(cmd, "size"))',
    '''else if (!strcmp(cmd, "poll")) {
      sscanf(rest, "%u", &testNow); printf("TICK %u\\n", testNow); measureBox(); boxLine();
    }
    else if (!strcmp(cmd, "answer")) sscanf(rest, "%d %u %u", &g_sizeMode, &g_w, &g_h);
    else if (!strcmp(cmd, "rx")) {
      unsigned src, dst, inst, op; int k = 0;
      sscanf(rest, "%x %x %x %x %n", &src, &dst, &inst, &op, &k);
      unsigned char d[64]; unsigned n; hex(rest + k, d, &n);
      inject(0x82, (unsigned char)inst, (unsigned short)src, (unsigned short)dst, 0xc04, (unsigned char)op, d, n);
    }
    else if (!strcmp(cmd, "owner")) {
      unsigned host, inst; sscanf(rest, "%x %x", &host, &inst); g_host = host; g_inst = inst;
    }
    else if (!strcmp(cmd, "map")) {
      hudh::Map m = { hFrame, hStreet, hDist, hUnit, (unsigned short)a, (unsigned short)(a + 1), hBar };
      useMap(m, "test map");
    }
    else if (!strcmp(cmd, "fixed")) g_boxFixed = a;
    else if (!strcmp(cmd, "sixwb")) g_6wb = a;
    else if (!strcmp(cmd, "state"))
      printf("STATE shown=%d fresh=%d blink=%d type=%u\\n", g_shown, g_fresh, g_blinking, g_lastType);
    else if (!strcmp(cmd, "inspect")) boxLine();
    else if (!strcmp(cmd, "work")) {
      workerStep = true; workerSleeps = 0; g_enabled = 1; g_blinkOn = true;
      printf("WORKBEGIN\\n");
      try { worker(0); } catch (int) {}
      workerStep = false; printf("WORKEND\\n"); boxLine();
    }
    else if (!strcmp(cmd, "fxdraw")) {
      Guide g; memset(&g, 0, sizeof g); g.active = true; g.type = 3;
      drawManeuver(g, "30", "m"); drawPicture(otherBuf(g_visible), g, 0, true);
      g_blinking = true; g_lastType = 3; g_lastIndex = 0;
    }
    else if (!strcmp(cmd, "size"))''')


def reply(handle=5, width=126, height=77, src=0x101, dst=0x205e, inst=0, marker=None):
    data = [handle >> 8, handle & 255]
    if marker is not None:
        data.append(marker)
    data.extend([width >> 8, width & 255, height >> 8, height & 255])
    return 'rx %x %x %x d %s' % (src, dst, inst, ' '.join('%02x' % b for b in data))


EXHAUST = ['poll %u' % n for n in (0, 2000, 3000, 5000, 7000, 9000, 14000, 16000, 26000, 28000)]


class HudSizeRecovery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('clang++'):
            raise unittest.SkipTest('clang++ not found')
        cls.tmp = tempfile.TemporaryDirectory(prefix='hudsize-')
        source = Path(cls.tmp.name) / 'h.cpp'
        source.write_text(RECOVERY_HARNESS)
        cls.exe = Path(cls.tmp.name) / 'h'
        subprocess.run(['clang++', '-std=c++17', '-w', '-I', str(ROOT), '-I', str(ROOT / 'src'),
                        '-o', str(cls.exe), str(source)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_lines(self, commands):
        return subprocess.run([str(self.exe)], input='\n'.join(commands) + '\n', text=True,
                              capture_output=True, check=True).stdout.splitlines()

    def queries(self, lines):
        return [line for line in lines if not line.startswith('#') and ' c04 ' in line]

    def test_success_stops_further_queries(self):
        out = self.run_lines(['answer 1 126 77', 'poll 0', 'poll 1000', 'poll 10000'])
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')
        self.assertEqual(len(self.queries(out)), 2)

    def test_response_window_and_first_retry_gap(self):
        out = self.run_lines(['poll %u' % n for n in (0, 1999, 2000, 2999, 3000)])
        ticks = [i for i, line in enumerate(out) if line.startswith('TICK')]
        counts = [len(self.queries(out[start:end])) for start, end in zip(ticks, ticks[1:] + [len(out)])]
        self.assertEqual(counts, [2, 0, 0, 0, 2])

    def test_five_attempts_total_then_no_more_requests(self):
        out = self.run_lines(EXHAUST + ['poll 28001', 'poll 100000', 'poll 200000'])
        self.assertEqual(len(self.queries(out)), 10)
        self.assertEqual(out[-1], 'BOX 6wa')

    def test_late_reply_during_response_window(self):
        out = self.run_lines(['poll 0', reply(), 'poll 500', 'poll 10000'])
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')
        self.assertEqual(len(self.queries(out)), 2)

    def test_callback_does_not_fit_or_draw(self):
        out = self.run_lines(['poll 0', reply(), 'inspect', 'state'])
        self.assertEqual([line for line in out if line.startswith('BOX')], ['BOX 6wa', 'BOX 6wa'])
        self.assertFalse(any(' 21d ' in line or ' 220 ' in line for line in out if not line.startswith('#')))

    def test_late_reply_during_retry_gap(self):
        out = self.run_lines(['poll 0', 'poll 2000', reply(), 'poll 2500', 'poll 10000'])
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')
        self.assertEqual(len(self.queries(out)), 2)

    def test_late_reply_after_fifth_attempt(self):
        out = self.run_lines(EXHAUST + [reply(), 'poll 50000', 'poll 100000'])
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')
        self.assertEqual(len(self.queries(out)), 10)

    def test_unrequested_reply_cannot_choose_box(self):
        out = self.run_lines([reply(), 'poll 0'])
        self.assertEqual(out[-1], 'BOX 6wa')
        self.assertEqual(len(self.queries(out)), 2)

    def test_real_anonymous_errors_cannot_abort_or_complete_request(self):
        out = self.run_lines(['poll 0', 'rx 101 205e 0 f 05', 'rx 101 205e 0 9 05',
                              'poll 100', reply(), 'poll 200'])
        self.assertEqual(len(self.queries(out)), 2)
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')

    def test_old_mock_handle_errors_do_not_block_late_success(self):
        out = self.run_lines(['poll 0', 'rx 101 205e 0 f 00 1f 05 00',
                              'rx 101 205e 0 9 00 1f 05 00', reply(), 'poll 500'])
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')
        self.assertEqual(len(self.queries(out)), 2)

    def test_wrong_identity_is_not_a_size_answer(self):
        for kwargs in ({'src': 0x102}, {'dst': 0x205f}, {'inst': 2}, {'handle': 6}):
            with self.subTest(kwargs=kwargs):
                out = self.run_lines(['poll 0', reply(**kwargs), 'poll 100', reply(), 'poll 200'])
                boxes = [line for line in out if line.startswith('BOX')]
                self.assertEqual(boxes, ['BOX 6wa', 'BOX 6wa', 'BOX fit 36 16 89 69'])
                self.assertEqual(len(self.queries(out)), 2)

    def test_malformed_and_unusable_replies_do_not_end_recovery(self):
        packets = ['00', '00 05 00 7e 00', '00 05 00 00 7e 00 4d', '00 05 02 00 7e 00 4d',
                   '00 05 01 00 7e 00 4d 00', '00 05 00 00 00 00',
                   '00 05 00 17 00 4d', '00 05 ff ff 00 4d', '00 05 00 7e 00 00']
        for packet in packets:
            with self.subTest(packet=packet):
                out = self.run_lines(['poll 0', 'rx 101 205e 0 d ' + packet, 'poll 100', reply(), 'poll 200'])
                boxes = [line for line in out if line.startswith('BOX')]
                self.assertEqual(boxes, ['BOX 6wa', 'BOX 6wa', 'BOX fit 36 16 89 69'])
                self.assertEqual(len(self.queries(out)), 2)

    def test_valid_seven_byte_answer(self):
        out = self.run_lines(['poll 0', reply(marker=1), 'poll 500'])
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')

    def test_map_change_discards_old_pending_and_late_answers(self):
        out = self.run_lines(['poll 0', reply(), 'map 21', 'poll 1', reply(), 'poll 2',
                              reply(handle=21), 'poll 3'])
        boxes = [line for line in out if line.startswith('BOX')]
        self.assertEqual(boxes, ['BOX 6wa', 'BOX 6wa', 'BOX 6wa', 'BOX fit 36 16 89 69'])
        self.assertEqual(self.queries(out), ['00 c04 00 05', '00 c04 00 05 01', '00 c04 00 15', '00 c04 00 15 01'])

    def test_host_and_terminal_change_starts_fresh_series(self):
        out = self.run_lines(['poll 0', reply(), 'owner 102 2', 'poll 1', reply(), 'poll 2',
                              reply(src=0x102, inst=2), 'poll 3'])
        boxes = [line for line in out if line.startswith('BOX')]
        self.assertEqual(boxes, ['BOX 6wa', 'BOX 6wa', 'BOX 6wa', 'BOX fit 36 16 89 69'])
        self.assertEqual(len(self.queries(out)), 4)

    def test_recovery_while_shown_invalidates_both_blink_buffers(self):
        out = self.run_lines(['poll 0', 'fxdraw', 'state', reply(), 'poll 500', 'state'])
        self.assertIn('STATE shown=1 fresh=0 blink=1 type=3', out)
        self.assertEqual(out[-2:], ['BOX fit 36 16 89 69', 'STATE shown=0 fresh=1 blink=0 type=4294967295'])
        after_reply = out[out.index('TICK 500'):]
        self.assertIn('00 220 00 05', after_reply)
        self.assertIn('00 220 00 06', after_reply)

    def test_actual_worker_recovers_a_shown_route_and_redraws_both_blink_phases(self):
        out = self.run_lines(['poll 0', 'fxdraw', 'state', reply(), 'work', 'state'])
        self.assertEqual(out[-2:], ['BOX fit 36 16 89 69', 'STATE shown=1 fresh=0 blink=1 type=3'])
        work = out[out.index('WORKBEGIN') + 1:out.index('WORKEND')]
        polygons = [line for line in work if not line.startswith('#') and ' 21d ' in line]
        self.assertTrue(any(line.startswith('00 21d 00 05') for line in polygons))
        self.assertTrue(any(line.startswith('00 21d 00 06') for line in polygons))
        for line in polygons:
            data = [int(v, 16) for v in line.split()[2:]]
            points = [(data[6 + i * 4] << 8 | data[7 + i * 4], data[8 + i * 4] << 8 | data[9 + i * 4])
                      for i in range(data[5])]
            self.assertTrue(all(0 <= x < 126 and 0 <= y < 77 for x, y in points), points)

    def test_map_change_rearms_an_exhausted_budget(self):
        out = self.run_lines(EXHAUST + ['answer 1 126 77', 'map 21', 'poll 28001', 'poll 50000'])
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')
        self.assertEqual(len(self.queries(out)), 12)

    def test_manual_box_and_sixwb_are_preserved(self):
        for setup, box in ((['box 1', 'fixed 1'], 'BOX mini'), (['sixwb 1'], 'BOX 6wa')):
            with self.subTest(setup=setup):
                out = self.run_lines(setup + ['answer 1 126 77', 'poll 0', 'poll 500'])
                self.assertEqual(out[-1], box)

    def test_failure_keeps_previous_fit(self):
        out = self.run_lines(['answer 1 126 77', 'poll 0', 'answer 0 0 0', 'map 21', 'poll 1',
                              'poll 2001', 'poll 3001'])
        self.assertEqual(out[-1], 'BOX fit 36 16 89 69')

    def test_response_window_and_gap_survive_clock_wrap(self):
        out = self.run_lines(['poll %u' % n for n in (4294967000, 1703, 1704, 2703, 2704)])
        ticks = [i for i, line in enumerate(out) if line.startswith('TICK')]
        counts = [len(self.queries(out[start:end])) for start, end in zip(ticks, ticks[1:] + [len(out)])]
        self.assertEqual(counts, [2, 0, 0, 0, 2])


if __name__ == '__main__':
    unittest.main()
