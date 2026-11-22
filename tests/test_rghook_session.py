"""Route lifetime: the real decoder and watcher, with QNX spawnle mapped to posix_spawn on the Mac.
Killing mm-ipod must retire its route without deleting a later session's route. No car connection is used.
"""
import os
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

try:
    from test_rgkombi import maneuver, update
except ImportError:
    from tests.test_rgkombi import maneuver, update

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#define read rg_read_hook
#define write rg_write_hook
#define symlink test_symlink
#include "rghook.c"
#undef read
#undef write
#undef symlink
static int watcher, spawn_attempts, short_write, fault;
static int most_kicks;
static volatile int spawn_entered, spawn_release;
static void kill_at_active_publication(void) {
  if (!getenv("TEST_KILL_ACTIVE_PUBLICATION")) return;
  int fd = open(getenv("RGHOOK_PPS"), O_RDONLY);
  if (fd < 0) return;
  char b[4096]; long n = read(fd, b, sizeof b - 1); close(fd);
  if (n < 0) return;
  b[n] = 0;
  if (strstr(b, "route_state:n:1\n")) kill(getpid(), SIGKILL);
}
void rgk_kick(void) {}  /* variant senders are covered separately; here only their watcher is exercised */
void rgh_kick(void) { most_kicks++; }
int spawnle(int mode, const char *path, const char *arg, ...) {
  if (getenv("TEST_SPAWN_FAIL_ALWAYS")) { errno = EAGAIN; return -1; }
  if (getenv("TEST_SPAWN_FAIL_ONCE") && ++spawn_attempts == 1) return -1;
  if (getenv("TEST_BLOCK_SPAWN")) {
    __atomic_store_n(&spawn_entered, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&spawn_release, __ATOMIC_ACQUIRE)) usleep(1000);
  }
  char *args[16]; unsigned n = 0;
  args[n++] = (char *)arg;
  va_list ap; va_start(ap, arg);
  while (n < 15 && (args[n] = va_arg(ap, char *))) n++;
  args[n] = 0;
  char **env = va_arg(ap, char **);
  va_end(ap);
  int pid;
  if (posix_spawn(&pid, path, 0, 0, args, env)) return -1;
  watcher = pid;
  if (getenv("TEST_WATCHER_FILE")) {
    FILE *f = fopen(getenv("TEST_WATCHER_FILE"), "w");
    if (f) { fprintf(f, "%d\n", pid); fclose(f); }
  }
  return pid;
}
static int mac_open(const char *p, int f, ...) {
  if (fault == 1 && f) { errno = EACCES; return -1; }
  return open(p, f ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY, 0644);
}
static long mac_write(int fd, const void *b, size_t n) {
  if (fd != logfd && fault == 2) { errno = EIO; return -1; }
  return write(fd, b, short_write && fd != logfd && n ? n - 1 : n);
}
static int mac_close(int fd) {
  int rc = close(fd);
  if (fault == 3) { errno = EIO; return -1; }
  if (fault == 2) errno = EBADF; /* cleanup must not overwrite the original write errno in the diagnostic */
  return rc;
}
static int mac_rename(const char *a, const char *b) {
  if (fault == 4) { errno = EIO; return -1; }
  if (getenv("TEST_CLAIM_FAIL_ONCE") && !strcmp(b, getenv("RGHOOK_PPS"))) {
    unsetenv("TEST_CLAIM_FAIL_ONCE"); errno = EACCES; return -1;
  }
  struct stat st;
  if (!lstat(a, &st) && !S_ISLNK(st.st_mode)) { errno = EXDEV; return -1; } /* actual QNX /dev/shmem behavior */
  int rc = rename(a, b);
  if (!rc) kill_at_active_publication();
  return rc;
}
int test_symlink(const char *a, const char *b) {
  if (fault == 5) { errno = EPERM; return -1; }
  int rc = symlink(a, b);
  if (!rc) kill_at_active_publication();
  return rc;
}
static int mac_unlink(const char *p) {
  if (getenv("TEST_PUBLIC_UNLINK_FAIL_ONCE") && !strcmp(p, getenv("RGHOOK_PPS"))) {
    unsetenv("TEST_PUBLIC_UNLINK_FAIL_ONCE"); errno = EACCES; return -1;
  }
  return unlink(p);
}
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10; }
static void *start_watcher(void *ready) { *(int *)ready = rg_watch_once(); return 0; }
int main(void) {
  r_open = mac_open; r_write = mac_write; r_read = read; r_close = mac_close; r_unlink = mac_unlink; r_rename = mac_rename;
  inited = 1; logfd = getenv("RGHOOK_LOG") ? open(getenv("RGHOOK_LOG"), O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
  rg_clear();
  char line[8192];
  while (fgets(line, sizeof line, stdin)) {
    if (!strncmp(line, "accepted", 8)) {
      ident_ok = auth_ok = 1; injected = pkt_ctl = 0;
      transport_fd = open("/dev/null", O_WRONLY);
      packet_done();
      close(transport_fd); transport_fd = -1;
    }
    else if (!strncmp(line, "watchrace", 9)) {
      pthread_t t; int ready;
      if (pthread_create(&t, 0, start_watcher, &ready)) return 2;
      while (!__atomic_load_n(&spawn_entered, __ATOMIC_ACQUIRE)) usleep(1000);
      int early = rg_watch_once();
      __atomic_store_n(&spawn_release, 1, __ATOMIC_RELEASE);
      pthread_join(t, 0);
      printf("race %d %d %d\n", early, ready, rg_watch_once());
    }
    else if (!strncmp(line, "short", 5)) short_write = 1;
    else if (!strncmp(line, "fault ", 6)) { fault = atoi(line + 6); short_write = 0; }
    else if (!strncmp(line, "msg ", 4)) {
      char *e; u32 mid = (u32)strtoul(line + 4, &e, 16); while (*e == ' ') e++;
      u8 b[4096]; u32 n = 0;
      while (e[0] > ' ' && e[1] > ' ' && n < sizeof b) { b[n++] = (u8)(hexv(e[0]) << 4 | hexv(e[1])); e += 2; }
      on_rg(mid, b, n);
    }
    printf("ok %d %d %d\n", getpid(), watcher, most_kicks); fflush(stdout);
  }
  return 0;
}
'''


@unittest.skipUnless(shutil.which('clang') and Path('/bin/ksh').exists(), 'clang and /bin/ksh required')
class RouteSession(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='rghook-session-build-')
        d = Path(cls.build.name)
        (d / 'h.c').write_text(HARNESS)
        cls.variants = {}
        for name, defs in [('plain', []), ('kombi', ['-DRGHOOK_KOMBI']), ('most', ['-DRGHOOK_MOST']),
                           ('both', ['-DRGHOOK_KOMBI', '-DRGHOOK_MOST'])]:
            cls.variants[name] = d / name
            subprocess.run(['clang', '-w', '-D_FORTIFY_SOURCE=0', *defs, '-I', str(ROOT / 'src'), '-o', str(cls.variants[name]),
                            str(d / 'h.c')], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='rghook-session-')
        self.addCleanup(self.tmp.cleanup)
        # These are literal path characters, never a shell program in the watcher.
        self.pps = Path(self.tmp.name) / "route ' $(literal)"
        self.log = Path(self.tmp.name) / 'publication.log'
        self.processes = []
        self.watchers = set()
        self.addCleanup(self.stop_all)

    def stop_all(self):
        for p in self.processes:
            if p.poll() is None:
                p.kill()
            p.wait()
            p.stdin.close()
            p.stdout.close()
        for pid in self.watchers:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass

    def start(self, variant='plain', publish=True, **extra_env):
        env = dict(os.environ, RGHOOK_PPS=str(self.pps), RGHOOK_LOG=str(self.log), **extra_env)
        env.pop('RGHOOK_NO_WATCH', None)
        p = subprocess.Popen([str(self.variants[variant])], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.processes.append(p)
        self.command(p, 'noop')  # initialization, independently of the first guidance message
        if publish:
            self.msg(p, 0x5202, maneuver(1, 1, 'Test Road'))
            self.msg(p, 0x5201, update(idx=1, metres=60, text='60 m'))
        return p

    def msg(self, p, mid, payload):
        self.command(p, 'msg %x %s' % (mid, payload.hex()))

    def command(self, p, line):
        p.stdin.write(line + '\n')
        p.stdin.flush()
        reply = p.stdout.readline().split()
        self.assertEqual(reply[:2], ['ok', str(p.pid)])
        watcher = int(reply[2])
        if watcher > 0:
            self.watchers.add(watcher)
        return int(reply[3])

    def wait_removed(self, path):
        deadline = time.monotonic() + 20
        while path.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertFalse(path.exists(), 'route survived the end of its mm-ipod session')

    def test_disconnect_clears_route_even_after_sigkill(self):
        p = self.start()
        self.assertIn('dist_to_maneuver:n:60', self.pps.read_text())
        p.kill()
        p.wait()
        self.wait_removed(self.pps)

    def test_first_active_publication_already_has_a_watcher(self):
        watcher_file = Path(self.tmp.name) / 'watcher.pid'
        p = self.start(publish=False, TEST_KILL_ACTIVE_PUBLICATION='1', TEST_WATCHER_FILE=str(watcher_file))
        p.stdin.write('msg 5201 %s\n' % update(idx=1, metres=60, text='60 m').hex())
        p.stdin.flush()
        self.assertEqual(p.wait(timeout=5), -signal.SIGKILL, 'fault must hit the first reachable active snapshot')
        if watcher_file.exists():
            self.watchers.add(int(watcher_file.read_text()))
        self.assertTrue(watcher_file.exists(), 'active publication preceded watcher startup')
        self.wait_removed(self.pps)

    def test_failed_watcher_never_exposes_first_active_route(self):
        p = self.start(publish=False, TEST_SPAWN_FAIL_ALWAYS='1')
        self.msg(p, 0x5201, update(idx=1, metres=60, text='60 m'))
        self.assertEqual(len(self.watchers), 0)
        if self.pps.exists():
            self.assertNotIn('route_state:n:1', self.pps.read_text())
        p.kill()  # no later callback is available to retry the failed spawn
        p.wait(timeout=5)
        if self.pps.exists():
            self.assertNotIn('route_state:n:1', self.pps.read_text())

    def test_accepted_link_waits_for_watcher_before_starting_most(self):
        p = self.start(variant='most', publish=False)
        self.assertEqual(self.command(p, 'accepted'), 1)
        self.assertEqual(len(self.watchers), 1)
        self.assertFalse(self.pps.exists(), 'link-up does not invent route data')

    def test_failed_claim_cannot_start_early_most_reader(self):
        legacy = '@routeguidance\nroute_state:n:1\ndist_to_maneuver:n:999\n'
        self.pps.write_text(legacy)
        p = self.start(variant='most', publish=False, TEST_CLAIM_FAIL_ONCE='1')
        self.assertEqual(self.command(p, 'accepted'), 0)
        self.assertEqual(len(self.watchers), 0)
        self.assertEqual(self.pps.read_text(), legacy)

    def test_failed_watcher_cannot_start_early_most_reader(self):
        p = self.start(variant='most', publish=False, TEST_SPAWN_FAIL_ALWAYS='1')
        self.assertEqual(self.command(p, 'accepted'), 0)
        self.assertEqual(len(self.watchers), 0)
        self.assertFalse(self.pps.exists())

    def test_starting_watcher_is_not_ready_for_another_thread(self):
        watcher_file = Path(self.tmp.name) / 'watcher.pid'
        p = self.start(publish=False, TEST_BLOCK_SPAWN='1', TEST_WATCHER_FILE=str(watcher_file))
        p.stdin.write('watchrace\n')
        p.stdin.flush()
        self.assertEqual(p.stdout.readline().split(), ['race', '0', '1', '1'])
        reply = p.stdout.readline().split()
        self.assertEqual(reply[:2], ['ok', str(p.pid)])
        self.watchers.add(int(watcher_file.read_text()))

    def test_qnx_ram_rename_is_not_required(self):
        self.start()
        self.assertTrue(self.pps.exists(), 'a QNX EXDEV on regular RAM rename must not prevent publication')
        self.assertIn('dist_to_maneuver:n:60', self.pps.read_text())
        session = self.pps.parent / os.readlink(self.pps)
        self.assertTrue(session.is_symlink())
        self.assertIn('RG publish ready:', self.log.read_text())
        self.assertNotIn('Test Road', self.log.read_text())

    def test_failed_publications_preserve_route_and_report_errno(self):
        p = self.start()
        cases = [(1, 'open', 13), (2, 'write', 5), (3, 'close', 5), (4, 'rename session', 5), (5, 'symlink session', 1)]
        for number, op, error in cases:
            with self.subTest(op=op):
                before = self.pps.read_bytes()
                self.command(p, 'fault %d' % number)
                self.msg(p, 0x5201, update(idx=1, metres=30, text='30 m'))
                self.msg(p, 0x5201, update(idx=1, metres=20, text='20 m'))
                self.assertEqual(self.pps.read_bytes(), before)
                diagnostic = 'RG publish %s failed:' % op
                log = self.log.read_text()
                self.assertEqual(log.count(diagnostic), 1, log)
                self.assertIn('errno=%d ' % error, log.split(diagnostic)[-1])
                self.command(p, 'fault 0')
                self.msg(p, 0x5201, update(idx=1, metres=10, text='10 m'))
                self.assertIn('dist_to_maneuver:n:10', self.pps.read_text())
        self.assertEqual(self.log.read_text().count('RG publish recovered:'), len(cases))

    def test_legacy_route_replacement_does_not_require_public_unlink(self):
        self.pps.write_text('@routeguidance\nroute_state:n:1\ndist_to_maneuver:n:999\n')
        self.start(TEST_PUBLIC_UNLINK_FAIL_ONCE='1')
        self.assertTrue(self.pps.is_symlink())
        self.assertIn('dist_to_maneuver:n:60', self.pps.read_text())

    def test_failed_old_claim_cannot_replace_new_session(self):
        old = self.start(publish=False, TEST_CLAIM_FAIL_ONCE='1')
        self.assertFalse(self.pps.exists())
        self.assertIn('errno=13 ', self.log.read_text(), 'initial public claim fault was not exercised')
        new = self.start()
        new_file = self.pps.resolve()
        self.msg(old, 0x5201, update(idx=1, metres=999, text='999 m'))
        self.assertEqual(self.pps.resolve(), new_file)
        self.assertIn('dist_to_maneuver:n:60', self.pps.read_text())
        self.msg(new, 0x5201, update(idx=1, metres=30, text='30 m'))
        self.assertIn('dist_to_maneuver:n:30', self.pps.read_text())

    def test_reader_keeps_complete_previous_snapshot(self):
        p = self.start()
        before = self.pps.read_bytes()
        with self.pps.open('rb') as reader:
            self.msg(p, 0x5201, update(idx=1, metres=30, text='30 m'))
            self.assertEqual(reader.read(), before)
        self.assertIn('dist_to_maneuver:n:30', self.pps.read_text())

    def test_idle_connected_route_has_no_expiry(self):
        p = self.start()
        before = self.pps.read_bytes()
        time.sleep(10.2)  # one watcher poll without a new navigation message
        self.assertEqual(self.pps.read_bytes(), before)
        self.assertEqual(len(self.watchers), 1)
        p.stdin.close()  # normal exit, unlike the SIGKILL test
        p.wait(timeout=5)
        self.wait_removed(self.pps)

    def test_old_watcher_preserves_new_session(self):
        old = self.start()
        self.assertTrue(self.pps.is_symlink())
        old_session = Path(os.readlink(self.pps))
        old_file = self.pps.resolve()
        new = self.start()
        new_file = self.pps.resolve()
        self.assertNotEqual(old_file, new_file)
        self.msg(old, 0x5201, update(idx=1, metres=999, text='999 m'))
        self.assertEqual(self.pps.resolve(), new_file)
        self.assertIn('dist_to_maneuver:n:60', self.pps.read_text())
        old_file = old_session.resolve()
        old.kill()
        old.wait()
        self.wait_removed(old_file)
        self.assertEqual(self.pps.resolve(), new_file)
        self.assertIn('route_state:n:1', self.pps.read_text())
        self.assertIsNone(new.poll())

    def test_initialization_selects_owner_before_first_guidance(self):
        old = self.start(publish=False)
        new = self.start(publish=False)
        self.msg(old, 0x5201, update(idx=1, metres=999, text='999 m'))
        self.msg(new, 0x5201, update(idx=1, metres=60, text='60 m'))
        self.assertIn('dist_to_maneuver:n:60', self.pps.read_text())
        new_file = self.pps.resolve()
        old.kill()
        old.wait(timeout=5)
        # Wait for the real old watcher, not a synthetic direct cleanup call.
        old_session = self.pps.parent / (self.pps.name + '.carplaymenu.%d' % old.pid)
        self.wait_removed(old_session)
        self.msg(new, 0x5201, update(idx=1, metres=30, text='30 m'))
        self.assertNotEqual(self.pps.resolve(), new_file)
        self.assertIn('dist_to_maneuver:n:30', self.pps.read_text())

    def test_lost_ram_route_cannot_be_read_after_reboot(self):
        self.start()
        self.assertTrue(self.pps.is_symlink())
        self.pps.resolve().unlink()  # reboot loses the RAM target, while /var retains the link
        self.assertTrue(self.pps.is_symlink())
        with self.assertRaises(FileNotFoundError):
            self.pps.read_bytes()

    def test_watcher_retries_failed_spawn(self):
        p = self.start(TEST_SPAWN_FAIL_ONCE='1')
        self.assertEqual(len(self.watchers), 1)
        p.kill()
        p.wait()
        self.wait_removed(self.pps)

    def test_partial_write_keeps_previous_route(self):
        p = self.start()
        before = self.pps.read_bytes()
        self.command(p, 'short')
        self.msg(p, 0x5201, update(idx=1, metres=30, text='30 m'))
        self.assertEqual(self.pps.read_bytes(), before)
        self.assertEqual([f.resolve() for f in self.pps.parent.glob(self.pps.name + '.%d.*' % p.pid)], [self.pps.resolve()])
        self.assertIn('RG publish write failed:', self.log.read_text())
        self.assertIn('errno=0 ', self.log.read_text())

    def test_drawing_variants_also_retire_their_route(self):
        for variant in ('kombi', 'most', 'both'):
            with self.subTest(variant=variant):
                p = self.start(variant=variant)
                self.assertTrue(self.pps.exists())
                p.kill()
                p.wait()
                self.wait_removed(self.pps)


if __name__ == '__main__':
    unittest.main()
