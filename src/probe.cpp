// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// libcpprobe.so — the cluster from a shell on hu-omap, as LD_PRELOAD of /bin/cat (the constructor does the work and exits; the host
// command never runs). A freestanding LD_PRELOAD like reboot.c (no crt for a standalone binary in this toolchain).
//
//   LD_PRELOAD=libcpprobe.so /bin/cat                 the installer's detection: prints one line
//       "kombi=1"           the KOMBI's route-guidance port answers (160.48.199.96:30502): G-series / J29, CarPlay directions go there
//       "kombi=0 <reason>"  no Ethernet KOMBI: the MOST HUDTerminal path (F-series)
//     only a TCP connect, closed at once: nothing is sent to the KOMBI.
//   CPM_CLEAR=<pid> [CPM_KOMBI_MARKER=<file>] LD_PRELOAD=libcpprobe.so /bin/cat
//       librghook's watcher (rg_watch_once, rghook.c), after the mm-ipod <pid> has ended. For each marker that still names that pid an
//       arrow it showed may still be up, and goes: the KOMBI's (RG_KOMBI_MARKER): the factory cancel (kombi.h Client::cancel); the
//       MOST HUDTerminal's (RG_MOST_MARKER, librghook-most / -both): the factory's end-of-route state (hud.h drawClear, through our own
//       MOST client). Then the marker goes. Logged to /var/iap/rghook.log. A newer session's marker is left alone.
//   CPM_KOMBI_ADDR=a.b.c.d:port   another KOMBI address (tests)
extern "C" {
char *getenv(const char *);
int open(const char *, int, ...);
long read(int, void *, unsigned long);
long write(int, const void *, unsigned long);
int unlink(const char *);
void _exit(int);
int snprintf(char *, unsigned long, const char *, ...);
}
#include "kombi.h"
#include "rgview.h"
#ifndef KOMBI_HOST
#include "hud.h"  // the MOST client and drawClear (head unit only: the Mac tests cover the KOMBI part)
#endif

// clang's EABI helpers for aligned block copies; QNX's libc.so.3 does not export them
extern "C" {
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

namespace {

void out(int fd, const char *s) {
  unsigned long n = 0;
  while (s[n])
    n++;
  write(fd, s, n);
}

void target(kombi::Client &c) {
  c.init();
  const char *a = getenv("CPM_KOMBI_ADDR");
  unsigned char ip[4];
  unsigned short port;
  if (a && kombi::parseAddr(a, ip, port))
    c.init(ip, port, 3000);
}

void logLine(const char *m) {
  int lf = open("/var/iap/rghook.log", 0x109 /* O_WRONLY | O_CREAT | O_APPEND */, 0644);
  if (lf < 0)
    return;
  out(lf, m);
  out(lf, "\n");
  close(lf);
}

// the marker exists and names pid (its first line)
bool names(const char *marker, const char *pid) {
  char b[16] = { 0 };
  int fd = open(marker, 0);
  if (fd < 0)
    return false;  // cleared by the session itself, or by the next one
  long n = read(fd, b, sizeof b - 1);
  close(fd);
  if (n <= 0)
    return false;
  for (int i = 0; b[i]; i++)
    if (b[i] == '\n')
      b[i] = 0;
  unsigned k = 0;
  while (pid[k] && b[k] == pid[k])
    k++;
  return !pid[k] && !b[k];
}

void clearKombi(const char *pid) {
  const char *marker = getenv("CPM_KOMBI_MARKER");
  if (!marker || !*marker)
    marker = RG_KOMBI_MARKER;
  if (!names(marker, pid))
    return;
  static kombi::Client c;
  static kombi::Graph g;
  target(c);
  g.init();
  bool ok = c.cancel(g);
  if (ok)
    unlink(marker);
  char m[160];
  snprintf(m, sizeof m, "kombi: mm-ipod %s ended with an arrow shown: %s", pid, ok ? "cleared" : "clear FAILED");
  logLine(m);
}

void clearMost(const char *pid) {
#ifndef KOMBI_HOST
  if (!names(RG_MOST_MARKER, pid))
    return;
  hud::g_log = logLine;
  char m[160];
  bool ok = false;
  if (hud::dsiOpen()) {
    for (int i = 0; i < 80 && !(hud::g_registered && hud::g_host); i++)
      usleep(100000);  // 8 s for the client and the registry
    if (hud::g_registered && hud::g_host) {
      hud::drawClear();
      usleep(1000000);  // the cluster's answers
      ok = true;
      unlink(RG_MOST_MARKER);
    }
  }
  snprintf(m, sizeof m, "most: mm-ipod %s ended with an arrow shown: %s", pid, ok ? "cleared" : "clear FAILED (no MOST client)");
  logLine(m);
#else
  (void)pid;
#endif
}

__attribute__((constructor)) void cpProbe() {
  const char *pid = getenv("CPM_CLEAR");
  if (pid && *pid) {
    clearKombi(pid);
    clearMost(pid);
    _exit(0);
  }
  static kombi::Client c;
  target(c);
  out(1, c.probe(1500) ? "kombi=1\n" : "kombi=0 ");
  if (c.detail[0]) {
    out(1, c.detail);
    out(1, "\n");
  }
  _exit(0);
}

}  // namespace
