"""The production HUD reader tolerates a snapshot retired during fresh pathname lookup.
No car, firmware fixtures or drawing stubs are needed: the harness calls hud::readGuide itself.
"""
import json
import os
import shutil
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
int open(const char *, int, ...);
long read(int, void *, unsigned long);
long write(int, const void *, unsigned long);
int close(int);
int unlink(const char *);
int usleep(unsigned);
long readlink(const char *, char *, unsigned long);
}
static const char *route;
static int failures, attempts, sleeps;
static unsigned delay, mockNow;
extern "C" int readerOpen(const char *p, int flags, ...) {
  if (!strcmp(p, "/var/iap/pps/routeguidance")) {
    attempts++;
    if (failures) { failures--; return -1; }
    p = route;
  }
  return open(p, flags);
}
extern "C" int readerSleep(unsigned n) {
  sleeps++; delay += n;
  return usleep(n);
}
extern "C" long readerReadlink(const char *p, char *b, unsigned long n) {
  if (!strcmp(p, "/var/iap/pps/routeguidance")) p = route;
  return readlink(p, b, n);
}
#define open readerOpen
#define usleep readerSleep
#define readlink readerReadlink
#define clock_gettime readerClock
#include "src/hud.h"
#undef open
#undef usleep
#undef readlink
#undef clock_gettime
extern "C" int readerClock(int, hud_timespec *t) {
  t->sec = mockNow / 1000;
  t->nsec = (mockNow % 1000) * 1000000;
  return 0;
}
int main(int argc, char **argv) {
  if (argc < 4 || argc > 5) return 2;
  route = argv[1]; failures = atoi(argv[2]);
  unsigned count = (unsigned)atoi(argv[3]), got = 0, active = 0, hold = 0, missing = 0, invalid = 0;
  printf("ready\n"); fflush(stdout);
  if (getchar() == EOF) return 3;
  const char *steps = getenv("TEST_READER_STEPS");
  for (unsigned i = 0; i < count; i++) {
    if (steps) {
      char *end;
      mockNow = (unsigned)strtoul(steps, &end, 10);
      failures = (int)strtol(end + 1, &end, 10);
      steps = *end ? end + 1 : end;
    }
    hud::Guide g;
    if (hud::readGuide(g)) {
      got++;
      if (g.active) active++;
      if (g.hold) hold++;
      // Both fields are from the same immutable generation, never a partial or mixed write.
      if (g.active && (g.dist != g.index || g.type != 3 || strcmp(g.road, "Test Road"))) invalid++;
    }
    if (steps) printf("step %c\n", g.active ? 'A' : g.hold ? 'H' : '-');
    if (hud::g_noFile) missing++;
    if (argc == 5 && i == 0 && unlink(argv[4])) return 4;
  }
  printf("{\"got\":%u,\"active\":%u,\"hold\":%u,\"missing\":%u,\"invalid\":%u,"
         "\"attempts\":%d,\"sleeps\":%d,\"delay\":%u}\n", got, active, hold, missing, invalid, attempts, sleeps, delay);
}
'''


def snapshot(index):
    return ('@routeguidance\nroute_state:n:1\nmaneuver_index:n:%d\nmaneuver_type:n:3\n'
            'dist_to_maneuver:n:%d\nmaneuver_road::Test Road\n' % (index, index))


class HudRouteReader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('clang++'):
            raise unittest.SkipTest('clang++ not found')
        cls.build = tempfile.TemporaryDirectory(prefix='hudreader-build-')
        source = Path(cls.build.name) / 'h.cpp'
        source.write_text(HARNESS)
        cls.exe = Path(cls.build.name) / 'h'
        subprocess.run(['clang++', '-std=c++17', '-w', '-I', str(ROOT), '-I', str(ROOT / 'src'),
                        '-o', str(cls.exe), str(source)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='hudreader-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.public = self.root / 'routeguidance'
        self.session = self.root / 'session'
        self.current = self.root / 'snapshot.1'
        self.current.write_text(snapshot(1))
        self.session.symlink_to(self.current)
        self.public.symlink_to(self.session)

    def read(self, failures=0, count=1, disconnect_after_first=False, steps=None):
        args = [str(self.exe), str(self.public), str(failures), str(count)]
        if disconnect_after_first:
            args.append(str(self.session))
        env = dict(os.environ)
        if steps is not None:
            env['TEST_READER_STEPS'] = ';'.join('%d:%d' % step for step in steps)
            args[3] = str(len(steps))
        result = subprocess.run(args, input='\n', env=env,
                                capture_output=True, text=True, timeout=10, check=True)
        report = json.loads(result.stdout.splitlines()[-1])
        if steps is not None:
            report['steps'] = [line[5:] for line in result.stdout.splitlines() if line.startswith('step ')]
        return report

    def test_transient_lookup_failures_retry_before_declaring_no_route(self):
        for failures in (1, 2):
            with self.subTest(failures=failures):
                result = self.read(failures=failures)
                self.assertEqual(result, dict(got=1, active=1, hold=0, missing=0, invalid=0, attempts=failures + 1,
                                              sleeps=failures, delay=failures * 1000))

    def test_exhausted_retries_hold_only_a_live_session(self):
        result = self.read(failures=100)
        self.assertEqual(result, dict(got=1, active=0, hold=1, missing=0, invalid=0, attempts=3, sleeps=2, delay=2000))
        result = self.read(failures=100, count=2, disconnect_after_first=True)
        self.assertEqual(result, dict(got=1, active=0, hold=1, missing=1, invalid=0, attempts=6, sleeps=4, delay=4000))

    def test_failed_open_on_legacy_regular_file_does_not_hold(self):
        self.public.unlink()
        self.public.write_text(snapshot(1))
        result = self.read(failures=100)
        self.assertEqual(result, dict(got=0, active=0, hold=0, missing=1, invalid=0, attempts=3, sleeps=2, delay=2000))

    def test_missing_ram_target_cannot_hold_indefinitely(self):
        self.current.unlink()  # both disk aliases survive, as after RAM loss
        result = self.read(steps=[(0, 0), (999, 0), (1000, 0), (1001, 0)])
        self.assertEqual(result['steps'], ['H', 'H', '-', '-'])
        self.assertEqual(result['missing'], 2)

    def test_hold_timeout_is_safe_across_clock_wrap(self):
        self.current.unlink()
        result = self.read(steps=[(0xfffffe0b, 0), (498, 0), (499, 0)])
        self.assertEqual(result['steps'], ['H', 'H', '-'])

    def test_successful_open_resets_hold_budget(self):
        result = self.read(steps=[(0, 3), (1000, 3), (1001, 0), (2000, 3), (2999, 3), (3000, 3)])
        self.assertEqual(result['steps'], ['H', '-', 'A', 'H', 'H', '-'])
        self.assertEqual(result['active'], 1)
        self.assertEqual(result['invalid'], 0)

    def test_disconnect_does_not_hold_the_last_route(self):
        self.session.unlink()  # the exit watcher leaves only the dangling public alias
        result = self.read()
        self.assertEqual(result['got'], 0)
        self.assertEqual(result['missing'], 1)
        self.assertEqual(result['attempts'], 3)
        self.assertEqual(result['delay'], 2000)

    def test_fresh_opens_during_snapshot_rotation_keep_complete_routes(self):
        count = 50000
        p = subprocess.Popen([str(self.exe), str(self.public), '0', str(count)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(lambda: p.poll() is None and p.kill())
        self.assertEqual(p.stdout.readline(), 'ready\n')
        errors = []

        def rotate():
            try:
                current = self.current
                for index in range(2, 1502):
                    new = self.root / ('snapshot.%d' % index)
                    new.write_text(snapshot(index))
                    tmp = self.root / 'session.tmp'
                    tmp.symlink_to(new)
                    os.replace(tmp, self.session)
                    current.unlink()
                    current = new
            except BaseException as error:
                errors.append(error)

        writer = threading.Thread(target=rotate)
        writer.start()
        out, err = p.communicate('\n', timeout=20)
        writer.join(timeout=10)
        self.assertFalse(writer.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(p.returncode, 0, err)
        result = json.loads(out.splitlines()[-1])
        self.assertEqual(result['got'], count, result)
        self.assertEqual(result['active'] + result['hold'], count, result)
        self.assertEqual(result['missing'], 0, result)
        self.assertEqual(result['invalid'], 0, result)


if __name__ == '__main__':
    unittest.main()
