// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// rgkombi.cpp — part of librghook-kombi.so (with rghook.c built with RGHOOK_KOMBI): CarPlay directions to an Ethernet KOMBI (G-series,
// J29), from mm-ipod. The installer installs this variant as librghook.so when its probe (libcpprobe.so) finds the KOMBI; MOST cars get
// the plain librghook.so, without any of this.
//
// The KOMBI's route-guidance service (SOME/IP 0xB006, kombi.h) is driven from here, not from the menu plugin, so it works with the
// installer's HUD-only mode (no menu plugin; the default on iDrive 6, which is what the KOMBI cars have) as well as with the full one.
//   * rghook.c calls rgk_kick() after every route-guidance message; the first starts the worker thread (mm-ipod's own threads never
//     wait on the network).
//   * every 500 ms: the state from rg_view_get(), while "CarPlay directions in HUD" is on (carplaymenu/hud_guidance), into
//     kombi::Output (route graph, in-place updates, distance, lanes, cancel).
//   * mm-ipod lives for one CarPlay session (Connectivity starts it per session). A session that ends with an arrow shown would leave
//     it on the cluster, so while one is shown the file kMarker holds this process's pid, and a watcher (rg_watch_once in rghook.c,
//     outliving mm-ipod) waits for the pid to end and then runs libcpprobe.so with CPM_CLEAR=<pid>: the factory cancel, only if the
//     marker still names that pid (a new session's arrow is left alone). A new worker also clears a marker it finds before it draws.
//   * the BMW navigation wins: while it guides a route (seen in the system log, `sloginfo -w`, as NBT-AAD does) nothing is sent and
//     our state is forgotten without a cancel (the factory graph replaces ours); when its route ends, CarPlay's arrow comes back with a
//     whole graph. A CarPlay route started while a BMW route runs takes over (as NBT-AAD: the driver chose it).
// Test hooks (tests/test_rgkombi.py): RGHOOK_KOMBI=a.b.c.d:port, RGHOOK_CFG=<switch folder>, RGHOOK_MARKER=<file>, RGHOOK_NO_WATCH=1,
// RGHOOK_SLOG=<file> (read instead of sloginfo), RGHOOK_SLOG_SETTLE_MS.
// The menu plugin (hud.h) only draws on a MOST HUDTerminal, which a KOMBI car does not have: one sender.
#ifdef KOMBI_HOST
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#else
extern "C" {
char *getenv(const char *);
int getpid(void);
int snprintf(char *, unsigned long, const char *, ...);
int spawn(const char *, int, const int *, const void *, char *const *, char *const *);
int pipe(int *);
int open(const char *, int, ...);
int pthread_create(int *, const void *, void *(*)(void *), void *);
struct rgk_timespec {
  long sec, nsec;
};
int clock_gettime(int, struct rgk_timespec *);
}
#endif
#include "kombi.h"
#include "rgview.h"

#ifndef KOMBI_HOST
extern "C" {  // clang's EABI helpers for aligned block copies; QNX's libc.so.3 does not export them
__attribute__((visibility("hidden"))) void __aeabi_memcpy8(void *d, const void *s, __SIZE_TYPE__ n) {
  memcpy(d, s, n);
}
__attribute__((visibility("hidden"))) void __aeabi_memcpy4(void *d, const void *s, __SIZE_TYPE__ n) {
  memcpy(d, s, n);
}
__attribute__((visibility("hidden"))) void __aeabi_memcpy(void *d, const void *s, __SIZE_TYPE__ n) {
  memcpy(d, s, n);
}
__attribute__((visibility("hidden"))) void __aeabi_memclr8(void *p, __SIZE_TYPE__ n) {
  memset(p, 0, n);
}
__attribute__((visibility("hidden"))) void __aeabi_memclr4(void *p, __SIZE_TYPE__ n) {
  memset(p, 0, n);
}
__attribute__((visibility("hidden"))) void __aeabi_memclr(void *p, __SIZE_TYPE__ n) {
  memset(p, 0, n);
}
}
#endif

namespace {

typedef unsigned u32;
typedef unsigned short u16;
typedef unsigned char u8;
const char *const kCfg = "/var/opt/hmi/data/rw/carplaymenu";
const char *const kMarker = RG_KOMBI_MARKER;

kombi::Output g_out;
volatile int g_started;

#ifdef KOMBI_HOST
u32 nowMs() {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (u32)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
#else
u32 nowMs() {
  rgk_timespec t;
  clock_gettime(2 /* CLOCK_MONOTONIC */, &t);
  return (u32)(t.sec * 1000 + t.nsec / 1000000);
}
#endif

const char *env(const char *n) {
  const char *v = getenv(n);
  return v && *v ? v : 0;
}
bool exists(const char *path) {
  char b[4];
  return rg_file_read(path, b, sizeof b) >= 0;
}
void logLine(const char *fmt, const char *a = "", int n = 0) {
  char b[200];
  snprintf(b, sizeof b, fmt, a, n);
  rg_log_line(b);
}

// ---- the BMW navigation's routes, from the system log -------------------------------------------------------------------------
// NBT-AAD's FactoryRouteWatch (nav_core.cpp; NavNBTEvoController / MapViewer lines, car-seen on a J29 2026-10-31): "RouteGuidanceStarted"
// = a route is active; it ends with "disable route: routeId N" for the route shown since that start ("NaviRouteVisibleInMap
// sourceHandle = N"). A start is followed ~1 s later by a disable of the OLD preview route, so a bare disable does not end it. Lines in
// the first settle ms after the log opens are its backlog dump and are skipped.
const char *findStr(const char *s, const char *p) {
  for (; *s; s++) {
    u32 i = 0;
    while (p[i] && s[i] == p[i])
      i++;
    if (!p[i])
      return s;
  }
  return 0;
}
struct RouteWatch {
  bool active, haveFirst;
  u32 tFirst, settleMs;
  char route[24];
  void reset() {
    active = haveFirst = false;
    route[0] = 0;
  }
  void release() {
    active = false;
    route[0] = 0;
  }
  void line(const char *s, u32 now) {
    if (!haveFirst) {
      haveFirst = true;
      tFirst = now;
    }
    if (now - tFirst < settleMs)
      return;
    if (findStr(s, "RouteGuidanceStarted")) {
      active = true;
      route[0] = 0;
      return;
    }
    static const char kVis[] = "NaviRouteVisibleInMap sourceHandle = ", kDis[] = "disable route: routeId ";
    const char *p = findStr(s, kVis);
    if (p && active) {
      p += sizeof kVis - 1;
      u32 n = 0;
      while (p[n] >= '0' && p[n] <= '9' && n + 1 < sizeof route) {
        route[n] = p[n];
        n++;
      }
      if (n)
        route[n] = 0;
      return;
    }
    p = findStr(s, kDis);
    if (p && active && route[0]) {
      p += sizeof kDis - 1;
      u32 n = 0;
      while (route[n] && p[n] == route[n])
        n++;
      if (!route[n] && !(p[n] >= '0' && p[n] <= '9'))
        release();
    }
  }
};
// `sloginfo -w` on a pipe (QNX spawn: its stdout = the pipe, non-blocking on our side), or RGHOOK_SLOG's file (tests). Ends with
// mm-ipod (the next write gets SIGPIPE). Restarted 10 s after it ended; no sloginfo: logged once, tried again every 60 s.
struct Slog {
  int fd;
  bool file, noted;
  u32 nextTry;
  u32 n;
  char buf[512];
  RouteWatch w;
  void init() {
    fd = -1;
    file = false;
    noted = false;
    nextTry = 0;
    n = 0;
    w.reset();
    const char *ms = env("RGHOOK_SLOG_SETTLE_MS");
    w.settleMs = 3000;
    if (ms) {
      u32 v = 0;
      while (*ms >= '0' && *ms <= '9')
        v = v * 10 + (u32)(*ms++ - '0');
      w.settleMs = v;
    }
  }
  void start(u32 now) {
    const char *f = env("RGHOOK_SLOG");
    if (f) {
      fd = open(f, 0);
      file = true;
      if (fd < 0)
        nextTry = now + 1000;
      return;
    }
#ifdef KOMBI_HOST
    if (!noted) {
      rg_log_line("kombi: no system log on this host: no BMW-route yield");
      noted = true;
    }
    nextTry = now + 60000;
#else
    int p[2];
    if (pipe(p) != 0) {
      nextTry = now + 60000;
      return;
    }
    fcntl(p[0], kombi::kFSetfl, fcntl(p[0], kombi::kFGetfl, 0) | kombi::kNonblock);
    int map[3] = { -1, p[1], -1 };  // the child's stdin and stderr closed, stdout the pipe
    static u8 inherit[512];         // struct inheritance, all zero: defaults
    static char a0[] = "sloginfo", a1[] = "-w";
    static char *const argv[] = { a0, a1, 0 };
    static char *const envp[] = { (char *)"PATH=/bin:/usr/bin:/proc/boot", 0 };
    static const char *const kPaths[] = { "/usr/bin/sloginfo", "/bin/sloginfo", "/proc/boot/sloginfo" };
    int pid = -1;
    for (u32 i = 0; i < 3 && pid == -1; i++)
      pid = spawn(kPaths[i], 3, map, inherit, argv, envp);
    close(p[1]);
    if (pid == -1) {
      close(p[0]);
      if (!noted) {
        rg_log_line("kombi: sloginfo not started: no BMW-route yield (do not start a BMW route while CarPlay guides)");
        noted = true;
      }
      nextTry = now + 60000;
      return;
    }
    fd = p[0];
    logLine("kombi: BMW-route watch: sloginfo -w (pid %s%d)", "", pid);
#endif
  }
  // reads what is there; false = nothing more for now
  void poll(u32 now) {
    if (fd < 0) {
      if ((int)(now - nextTry) >= 0)
        start(now);
      if (fd < 0)
        return;
    }
    for (int rounds = 0; rounds < 64; rounds++) {
      char t[256];
      long r = rg_fd_read(fd, t, sizeof t);
      if (r == 0 && !file) {  // sloginfo ended
        close(fd);
        fd = -1;
        nextTry = now + 10000;
        w.haveFirst = false;
        rg_log_line("kombi: sloginfo ended: the BMW-route watch restarts in 10 s");
        return;
      }
      if (r <= 0)
        return;  // nothing waiting (or the end of the test file)
      for (long i = 0; i < r; i++) {
        if (t[i] == '\n') {
          buf[n] = 0;
          w.line(buf, now);
          n = 0;
        } else if (n + 1 < sizeof buf)
          buf[n++] = t[i];
      }
    }
  }
};

void *worker(void *) {
  kombi::g_log = rg_log_line;
  kombi::g_debug = rg_debug_log;
  g_out.init();
  const char *a = env("RGHOOK_KOMBI");
  u8 ip[4];
  u16 port;
  if (a && kombi::parseAddr(a, ip, port))
    g_out.c.init(ip, port, 3000);
  const char *cfg = env("RGHOOK_CFG"), *marker = env("RGHOOK_MARKER");
  if (!marker)
    marker = kMarker;
  char enabled[160];
  snprintf(enabled, sizeof enabled, "%s/hud_guidance", cfg ? cfg : kCfg);
  rg_log_line("kombi: CarPlay directions to the Ethernet KOMBI (SOME/IP 0xB006), from mm-ipod");
  char mine[16];
  snprintf(mine, sizeof mine, "%d\n", (int)getpid());
  char old[16];
  if (rg_file_read(marker, old, sizeof old) > 0) {  // a session that ended with an arrow shown, and no watcher cleared it
    for (char *q = old; *q; q++)
      if (*q == '\n')
        *q = 0;
    bool ok = g_out.c.cancel(g_out.g);
    logLine(ok ? "kombi: arrow of an earlier session (pid %s) cleared" : "kombi: arrow of an earlier session (pid %s) not cleared",
            old);
    if (ok)
      rg_file_unlink(marker);
  }
  bool watching = false, prevHave = false, yielded = false;
  static Slog slog;
  slog.init();
  for (;;) {
    usleep(500000);
    rg_view v;
    bool on = exists(enabled), got = on && rg_view_get(&v), have = got && v.active;
    u32 now = nowMs();
    slog.poll(now);
    bool started = have && !prevHave;
    prevHave = have;
    if (slog.w.active && started) {
      slog.w.release();
      rg_log_line("kombi: BMW-route yield released: a new CarPlay route");
    }
    if (slog.w.active) {  // the BMW navigation guides: its graph replaces ours, no cancel
      if (!yielded) {
        rg_log_line("kombi: BMW navigation route: CarPlay arrows step aside");
        g_out.reset();
        g_out.c.closeFd();
        rg_file_unlink(marker);
        yielded = true;
      }
      continue;
    }
    if (yielded) {
      rg_log_line("kombi: BMW route ended: CarPlay arrows back");
      yielded = false;
    }
    if (!have && got && v.hold && g_out.shown)
      continue;  // Loading / Locating between routes: keep the last arrow
    static kombi::Man m;
    if (have)
      kombi::manOf(v, m);
    else
      memset(&m, 0, sizeof m);
    bool was = g_out.shown;
    g_out.tick(have, m, now);
    if (g_out.shown && !was) {
      rg_file_write(marker, mine);
      if (!watching) {
        rg_watch_once();
        watching = true;
      }
    }
    if (!g_out.shown && was && !g_out.clearFailed)
      rg_file_unlink(marker);
    if (g_out.failed) {
      rg_log_line("kombi: output off for this session");
      return 0;
    }
  }
  return 0;
}

}  // namespace

extern "C" void rgk_kick(void) {
  if (__atomic_exchange_n(&g_started, 1, __ATOMIC_ACQ_REL))
    return;
#ifdef KOMBI_HOST
  pthread_t t;
  if (pthread_create(&t, 0, worker, 0) != 0)
    rg_log_line("kombi: worker thread failed");
#else
  int t;
  if (pthread_create(&t, 0, worker, 0) != 0)
    rg_log_line("kombi: worker thread failed");
#endif
}
