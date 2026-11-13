// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// rghud.cpp — part of librghook-most.so and librghook-both.so (rghook.c built with RGHOOK_MOST): CarPlay directions drawn on the
// cluster's MOST HUDTerminal from mm-ipod, for installs without the menu plugin (the installer's HUD-only mode, the default on iDrive 6,
// where the menu needs the car coded for apps). With the menu plugin the plugin draws (hud.h in the HMI) and the installer puts the
// plain librghook.so or librghook-kombi.so in place instead, so there is one MOST sender either way.
//   * the same code as the plugin's: src/hud.h (its own MOST client "CarPlayHud" through libsys_dsi_colibry, the iPhone's
//     guidance from the routeguidance file this library writes, the BMW navigation's priority, the test switches); a MOST client in a
//     process of its own is what NBT-AAD's daemon does too.
//   * rghook.c calls rgh_kick() after every route-guidance message; the first starts hud.h (its threads; mm-ipod's never wait on MOST).
//     "CarPlay directions in HUD" is on by being here: the mm-ipod wrapper preloads this library only while hud_guidance is set.
//   * mm-ipod lives for one CarPlay session: while hud.h shows a picture, RG_MOST_MARKER holds our pid, and the watcher
//     (rg_watch_once, rghook.c) has libcpprobe.so clear the HUDTerminal if mm-ipod ended with it still up.
extern "C" {
char *getenv(const char *);
int getpid(void);
int open(const char *, int, ...);
long read(int, void *, unsigned long);
long write(int, const void *, unsigned long);
int close(int);
int unlink(const char *);
int snprintf(char *, unsigned long, const char *, ...);
void *memcpy(void *, const void *, __SIZE_TYPE__);
void *memset(void *, int, __SIZE_TYPE__);
}
#include "hud.h"
#include "rgview.h"

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

namespace {

volatile int g_started;

bool debugLog() {  // rghook.c's cached switch debug_log
  return rg_debug_log() != 0;
}

// the marker follows hud.h's picture: written when one goes up, removed when hud.h clears it
void *markThread(void *) {
  char mine[16];
  snprintf(mine, sizeof mine, "%d\n", getpid());
  bool was = false;
  for (;;) {
    usleep(500000);
    bool shown = hud::g_shown;
    if (shown && !was) {
      rg_file_write(RG_MOST_MARKER, mine);
      rg_watch_once();
    }
    if (!shown && was)
      rg_file_unlink(RG_MOST_MARKER);
    was = shown;
  }
  return 0;
}

}  // namespace

extern "C" void rgh_kick(void) {
  if (__atomic_exchange_n(&g_started, 1, __ATOMIC_ACQ_REL))
    return;
  hud::g_log = rg_log_line;
  hud::g_debug = debugLog;
  rg_log_line("most: CarPlay directions on the MOST HUDTerminal, from mm-ipod (no menu plugin)");
  hud::enable(true);
  int t;
  if (pthread_create(&t, 0, markThread, 0) != 0)
    rg_log_line("most: marker thread failed");
}
