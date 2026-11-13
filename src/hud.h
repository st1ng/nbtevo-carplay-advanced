// hud.h — "Add route guidance to HUD": CarPlay's next maneuver on the HUD / cluster, over MOST.
//
// A plugin thread (inside HmiMain3D) becomes a client of its own of the head unit's MOST service (DSI MOSTSubSystem.DSYSMOSTServer 4.5,
// the same one the HMI uses) and draws on the cluster's HUDTerminal (FBlock 0x82, instance 0), as the factory navigation does:
//   * where it comes from: the guidance the iPhone sends to CarPlay, decoded by librghook.so in mm-ipod into /var/iap/pps/routeguidance
//     (src/rghook.c);
//   * protocol, DSI client and cluster layout: found by the NBT-AAD project (github.com/Sq00ky/Supra-iDrive6-Android-Auto, milestone-7
//     src/most, src/cluster/hudterm.hpp, hud6wa_profile.hpp), verified by them on an F20 ("6WA" cluster); the code here is ours.
//     Every call is a StartResultAck method (op 6) to the owner of 0x82/00 in the registry; the cluster answers ResultAck (op 0xD).
//     6WA instances: 1 frame, 2 street, 3 distance, 4 unit (texts), 5 and 6 the picture, double-buffered (SwitchVisibility).
//   * the BMW navigation has priority: while another client sends commands to 0x82, nothing is drawn (and the picture is redrawn later).
// Cars with an Ethernet KOMBI (G-series, J29) get the same guidance over SOME/IP from librghook-kombi.so in mm-ipod (src/rgkombi.cpp,
// kombi.h), which the installer chose; the installer's cluster file says whether this plugin draws on MOST too (kombiOnly below).
// Layout of libsys_dsi_colibry.gpp.so.8 (the variant the HMI loads) checked on 006_017_006: CDSIClient with the channel at +0x130, the
// socket at +0x138, the fd at +0x140 (0x14c bytes); the CDSIIStream given to the callback has the id at +0x48, the data at +0x10 and the
// size at +0x08; the data callback is vtable slot 7 (+0x1c), attached 3 and detached 4.
#pragma once

extern "C" {
void *dlopen(const char *, int);
void *dlsym(void *, const char *);
int pthread_create(int *, const void *, void *(*)(void *), void *);
int usleep(unsigned);
int sched_yield(void);
void *calloc(__SIZE_TYPE__, __SIZE_TYPE__);
struct hud_timespec {
  long sec;
  long nsec;
};
int clock_gettime(int, struct hud_timespec *);
}

namespace hud {

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned u32;
typedef short s16;

void (*g_log)(const char *) = 0;
// Switch debug_log (the library's cached check): the per-event lines (every HUDTerminal message, every maneuver and fill step) only
// with it; none = off. Without it the log keeps start-up, errors (the cluster's op f / op 9 answers) and the rare state changes.
bool (*g_debug)() = 0;
bool verbose() {
  return g_debug && g_debug();
}
const char *const kPps = "/var/iap/pps/routeguidance";

// ---- DSI client of the MOST service ------------------------------------------------------------------------------------------------
typedef void (*FnConnCtor)(void *);
typedef int (*FnConnAdd)(void *, void *);
typedef void (*FnConnStart)(void *);
typedef void (*FnCliCtor)(void *, const char *, const char *, int, int);
typedef void (*FnSetNotif)(void *, unsigned);
typedef void (*FnStreamCtor)(void *, void *, void *, int, unsigned, unsigned, unsigned, const void *, const void *, int);
typedef void (*FnStreamWrite)(void *, const void *, unsigned);
typedef int (*FnStreamSend)(void *, int);
typedef void (*FnStreamDtor)(void *);
struct {
  FnConnCtor connCtor;
  FnConnAdd connAdd;
  FnConnStart connStart;
  FnCliCtor cliCtor;
  FnSetNotif setNotif;
  FnStreamCtor streamCtor;
  FnStreamWrite streamWrite;
  FnStreamSend streamSend;
  FnStreamDtor streamDtor;
  void *vtable;
} S;

enum : u32 {
  kReqRegisterClient = 0xF,
  kReqProcessMessage = 0x2,
  kInfoClientResponse = 0x80000000u,
  kInfoMessage = 0x80000001u,
  kAttrCommState = 0xC0000000u,
  kAttrRegistry = 0xC0000001u
};
// what the HMI's MOST client subscribes to (without it the server sends no messages)
const u32 kSubs[] = { 0xC0000002u, 0xC0000003u, kAttrRegistry, kInfoMessage, 0x80000002u,
                      0x80000003u, 0x80000004u, 0x80000005u,   0x80000006u,  0x80000007u };
enum {
  kFBlock = 0x82,
  kInst = 0,
  kOpStartResultAck = 6,
  kOpResultAck = 0xD
};
enum {
  kAudioMaster = 0x20,
  kVolumeActual = 0x200,
  kOpIncrement = 3,
  kOpDecrement = 4
};

void *g_conn;
char *g_cli;
volatile int g_attached, g_registered, g_lock;
volatile u32 g_client;
volatile u16 g_self, g_host, g_amHost;    // g_amHost: owner of AudioMaster 20/00 ("Media gestures", volume)
volatile u32 g_navMs, g_answers, g_sent;  // last command of another client to 0x82 (BMW navigation), answers from the cluster

u32 nowMs() {
  hud_timespec t;
  clock_gettime(2 /* CLOCK_MONOTONIC */, &t);
  return (u32)(t.sec * 1000 + t.nsec / 1000000);
}
// sleeps instead of spinning: with QNX's fixed priorities a sched_yield() loop may never let the lock's owner run
void lock() {
  while (__sync_lock_test_and_set(&g_lock, 1))
    usleep(1000);
}
void unlock() {
  __sync_lock_release(&g_lock);
}
void logf1(const char *fmt, u32 a, u32 b = 0, u32 c = 0) {
  char m[160];
  snprintf(m, sizeof m, fmt, a, b, c);
  if (g_log)
    g_log(m);
}

// request buffer: little-endian integers aligned from the start of the payload (DSI stream)
struct Buf {
  u8 b[512];
  u32 n;
  Buf() : n(0) {
  }
  void align(u32 a) {
    while (n % a && n < sizeof b)
      b[n++] = 0;
  }
  void u8_(u8 v) {
    if (n < sizeof b)
      b[n++] = v;
  }
  void u16_(u16 v) {
    align(2);
    u8_((u8)v);
    u8_((u8)(v >> 8));
  }
  void u32_(u32 v) {
    align(4);
    for (int i = 0; i < 4; i++)
      u8_((u8)(v >> (8 * i)));
  }
  void bytes(const u8 *p, u32 k) {
    for (u32 i = 0; i < k; i++)
      u8_(p[i]);
  }
};

bool request(u32 id, const Buf &p) {  // only with the client connected; serialised by g_lock
  if (!g_cli || !g_attached)
    return false;
  unsigned long long st[(0x400) / 8];
  memset(st, 0, sizeof st);
  S.streamCtor(st, g_cli + 0x130, g_cli + 0x138, *(int *)(g_cli + 0x140), 0x100, 7, id, g_cli + 0x110, g_cli + 0x118, 0);
  if (p.n)
    S.streamWrite(st, p.b, p.n);
  int rc = S.streamSend(st, -1);
  S.streamDtor(st);
  return rc >= 0;
}

void onAttached(void *) {
  g_attached = 1;
  g_registered = 0;
  Buf p;
  const char *nm = "CarPlayHud";
  u32 k = 0;
  while (nm[k])
    k++;
  p.u32_(k + 1);
  p.bytes((const u8 *)nm, k + 1);
  p.align(4);
  lock();
  request(kReqRegisterClient, p);
  for (u32 s : kSubs)
    S.setNotif(g_cli, s);
  unlock();
  if (g_log)
    g_log("CarPlayMenu: HUD: attached to MOSTSubSystem.DSYSMOSTServer, registering");
}
void onDetached(void *) {
  g_attached = 0;
  g_registered = 0;
  if (g_log)
    g_log("CarPlayMenu: HUD: detached from the MOST service");
}

u32 rd32(const u8 *p) {
  return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}
u16 rd16(const u8 *p) {
  return (u16)(p[0] | p[1] << 8);
}

// Our calls come back to every subscriber with the head unit's node address as the source (0x2000, not our client's): a call
// equal (function and payload) to one of the last ones we sent is ours, not the BMW navigation (like NBT-AAD's RecentCalls).
struct Recent {
  u16 fkt;
  u32 n, h, t;
};
Recent g_recent[32];
volatile u32 g_recentN;
u32 hashOf(const u8 *d, u32 n) {
  u32 h = 2166136261u;
  for (u32 i = 0; i < n; i++)
    h = (h ^ d[i]) * 16777619u;
  return h;
}
void noteSent(u16 fkt, const u8 *d, u32 n) {
  Recent &r = g_recent[g_recentN++ % 32];
  r.fkt = fkt;
  r.n = n;
  r.h = hashOf(d, n);
  r.t = nowMs();
}
bool isOurs(u16 fkt, const u8 *d, u32 n) {
  u32 h = hashOf(d, n), now = nowMs();
  for (u32 i = 0; i < 32; i++) {
    const Recent &r = g_recent[i];
    if (r.t && r.fkt == fkt && r.n == n && r.h == h && now - r.t < 5000)
      return true;
  }
  return false;
}

// Debugging a cluster (F54 vs the 6WA profile): with debug_log, the first 300 HUDTerminal messages in both directions go to the log
// with their first data bytes (answers and errors to our calls, and the BMW navigation's own drawing commands when it guides).
// Without it only the cluster's errors to our calls (always) and, for 10 s after a test channel batch (g_probeMs), everything.
volatile u32 g_dbg, g_probeMs;
void dbgLine(const char *head, u16 fkt, u8 op, const u8 *d, u32 n, bool always = false) {
  if (g_dbg >= 600 || !g_log)
    return;
  if (!always && !(g_probeMs && nowMs() - g_probeMs < 10000) && !verbose())
    return;
  g_dbg++;
  char m[240];
  int k = snprintf(m, sizeof m, "CarPlayMenu: HUD %s fkt %03x op %x len %u:", head, fkt, op, n);
  for (u32 i = 0; i < n && i < 64 && k < (int)sizeof m - 4; i++)
    k += snprintf(m + k, sizeof m - k, " %02x", d[i]);
  g_log(m);
}
void dbgOut(u16 fkt, const u8 *d, u32 n) {
  dbgLine("out ", fkt, kOpStartResultAck, d, n);
}
void dbgMsg(const char *dir, u16 src, u16 dst, u16 fkt, u8 op, const u8 *p, u32 len) {
  u32 dn = len >= 32 ? rd32(p + 28) : 0;
  if (32 + dn > len)
    dn = len > 32 ? len - 32 : 0;
  char h[40];
  snprintf(h, sizeof h, "%s %04x>%04x", dir, src, dst);
  dbgLine(h, fkt, op, p + 32, dn, dst == g_self && (op == 0xF || op == 9));  // ErrorAck / Error to our calls: always
}
void onData(void *, void *is) {
  if (!is)
    return;
  const char *s = (const char *)is;
  u32 id = *(const u32 *)(s + 0x48), len = *(const u32 *)(s + 0x08);
  const u8 *p = *(const u8 *const *)(s + 0x10);
  if (!p)
    return;
  if (id == kInfoClientResponse && len >= 6) {
    g_client = rd32(p);
    g_self = rd16(p + 4);
    if (g_client == 0xffffffffu) {
      if (g_log)
        g_log("CarPlayMenu: HUD: MOST registration refused");
      return;
    }
    g_registered = 1;
    lock();
    S.setNotif(g_cli, kAttrCommState);
    unlock();
    logf1("CarPlayMenu: HUD: MOST client %u, device %04x", g_client, g_self);
  } else if (id == kAttrRegistry && len >= 4) {
    u32 cnt = rd32(p);
    u16 host = 0, am = 0;
    for (u32 i = 0; i < cnt && 4 + i * 4 + 4 <= len; i++) {
      u16 key = rd16(p + 4 + i * 4 + 2);
      if (key == ((kFBlock << 8) | kInst) && !host)
        host = rd16(p + 4 + i * 4);
      if (key == ((kAudioMaster << 8) | 0) && !am)
        am = rd16(p + 4 + i * 4);
    }
    if (host != g_host) {
      g_host = host;
      logf1("CarPlayMenu: HUD: HUDTerminal 82/00 at device %04x", host);
    }
    if (am != g_amHost) {
      g_amHost = am;
      logf1("CarPlayMenu: MOST: AudioMaster 20/00 at device %04x", am);
    }
  } else if (id == kInfoMessage && len >= 32) {  // MessageType: tx, tag, val, dst, src, fblock, inst, fkt, op, tag, len, data
    u16 dst = rd16(p + 12), src = rd16(p + 14);
    u8 fb = p[16], op = p[20];
    if (fb != kFBlock)
      return;
    if (op == kOpResultAck && dst == g_self)
      g_answers++;
    u32 varTag = rd32(p + 4), varVal = rd32(p + 8);
    u16 fkt = rd16(p + 18);
    dbgMsg(src == g_self ? "echo" : dst == g_self ? "in  " : "seen", src, dst, fkt, op, p, len);
    bool normal = varTag == 2 && !(varVal >= 4 && varVal <= 9);  // not the send status of a message of ours
    bool drawing = (fkt >= 0x200 && fkt <= 0x22B) || (fkt >= 0xC03 && fkt <= 0xC09);
    u32 dn = rd32(p + 28);
    const u8 *dd = p + 32;
    if (32 + dn > len)
      dn = 0;
    if (op <= 8 && src != g_self && normal && drawing && !isOurs(fkt, dd, dn)) {  // another client (the BMW navigation) drawing
      if (!g_navMs) {
        char m[120];
        snprintf(m, sizeof m, "CarPlayMenu: HUD: other client on 82/00: %04x -> %04x fkt %03x op %u", src, dst, fkt, op);
        if (g_log)
          g_log(m);
      }
      g_navMs = nowMs() | 1;
    }
  }
}

void *connThread(void *) {
  S.connStart(g_conn);
  if (g_log)
    g_log("CarPlayMenu: HUD: DSI connection ended");
  return 0;
}

bool dsiOpen() {
  void *lib = dlopen("libsys_dsi_colibry.gpp.so.8", 2);
  if (!lib)
    lib = dlopen("/opt/sys/lib/libsys_dsi_colibry.gpp.so.8", 2);
  if (!lib) {
    if (g_log)
      g_log("CarPlayMenu: HUD: libsys_dsi_colibry.gpp.so.8 not found");
    return false;
  }
  S.connCtor = (FnConnCtor)dlsym(lib, "_ZN14CDSIConnectionC1Ev");
  S.connAdd = (FnConnAdd)dlsym(lib, "_ZN14CDSIConnection3addER10CDSIClient");
  S.connStart = (FnConnStart)dlsym(lib, "_ZN14CDSIConnection5startEv");
  S.cliCtor = (FnCliCtor)dlsym(lib, "_ZN10CDSIClientC1EPKcS1_ii");
  S.setNotif = (FnSetNotif)dlsym(lib, "_ZN10CDSIClient15setNotificationEj");
  S.streamCtor = (FnStreamCtor)dlsym(
      lib, "_ZN11CDSIOStreamC1ER11CDSIChannelP13CDSITCPSocketiN3DSI11RequestTypeENS4_7CommandEjRK8SPartyIDS9_i");
  S.streamWrite = (FnStreamWrite)dlsym(lib, "_ZN11CDSIOStream5writeEPKvj");
  S.streamSend = (FnStreamSend)dlsym(lib, "_ZN11CDSIOStream4sendEi");
  S.streamDtor = (FnStreamDtor)dlsym(lib, "_ZN11CDSIOStreamD1Ev");
  S.vtable = dlsym(lib, "_ZTV10CDSIClient");
  if (!S.connCtor || !S.connAdd || !S.connStart || !S.cliCtor || !S.setNotif || !S.streamCtor || !S.streamWrite || !S.streamSend ||
      !S.streamDtor || !S.vtable) {
    if (g_log)
      g_log("CarPlayMenu: HUD: DSI symbols missing");
    return false;
  }
  g_conn = calloc(1, 0x400);
  g_cli = (char *)calloc(1, 0x400);
  if (!g_conn || !g_cli)
    return false;
  S.connCtor(g_conn);
  S.cliCtor(g_cli, "DSYSMOSTServer", "MOSTSubSystem", 4, 5);
  void **vt = (void **)calloc(16, sizeof(void *));  // CDSIClient vtable: 8 slots (0x28 bytes with the header)
  memcpy(vt, (char *)S.vtable + 8, 8 * sizeof(void *));
  vt[3] = (void *)&onAttached;
  vt[4] = (void *)&onDetached;
  vt[7] = (void *)&onData;
  *(void ***)g_cli = vt;
  S.connAdd(g_conn, g_cli);
  int t;
  return pthread_create(&t, 0, connThread, 0) == 0;
}

}  // namespace hud
#include "hudshapes.h"
namespace hud {

// ---- HUDTerminal: calls -------------------------------------------------------------------------------------------------------------
enum {
  kSetVisibility = 0x204,
  kSwitchVisibility = 0x20E,
  kSetText = 0x210,
  kSetLineWidth = 0x219,
  kSetFillColor = 0x21B,
  kAddPolygon = 0x21D,
  kRemoveAll = 0x220
};
// Where the picture is. The shapes below are drawn in the 6WA box (70,20)-(170,120) (F20, NBT-AAD). The MINI F54 cluster shows a smaller
// area: its BMW navigation draws turn arrows in about x 37..83, y 1..68 (captured in the car, HMI ID5_1703-490, 2026-11-11), and a test
// arrow there was visible while ours in the 6WA box was not (text was). On a MINI (its layout package is present) every point is mapped
// to (35,15)-(85,65); the file hud_box_6wa in the settings folder keeps the 6WA box.
bool g_boxMini;
u8 g_fillDim = 3;  // the context tone: grey (F54; 0x0A, the first choice, is transparent there); hud_mono draws everything in kFill
// Test switches (files in the settings folder, read when the HUD starts; all off by default, the drawing without them is unchanged):
//   hud_convex   the convex pieces of before instead of whole polygons. Whole polygons (hudshapes.h mergePieces, a few calls a picture
//                instead of up to 96, which is what makes the two effects below cheap) are always on since 2026-11-13: the F54 terminal
//                fills them cleanly (hud_probe.sh concave) and the pictures look the same (test switch hud_concave until then).
//   hud_fill     progress fill: from the step's start the maneuver fills with the effect colour from the bottom up, in 8 steps, full
//                at the turn. Range (mib2q-carplay-rgi's bargraph rule): the iPhone's step length (maneuver_step_length), at most
//                the cap; unknown -> the cap. Optional content: "<cap metres> <colour index>", default "300 5".
//                F54 fill colours at night (car, 2026-11-13, hud_probe.sh colours): 0 4 6 7 white, 2 5 11..31 orange, 3 8 grey,
//                1 9 10 nothing (10, the first default, only cut the white away). The BMW navigation uses 4 and 5; day colours unchecked.
//   hud_blink    in the last fifth of that range the maneuver blinks, 600 ms per phase, route colour <-> effect colour: both phases
//                sit in the two picture buffers and SwitchVisibility swaps them, one MOST call a phase.
//   hud_colour   the effect colour index (menu "HUD effects > Effect colour"); else the second number of hud_fill; else 5.
// hud_fill and hud_blink are menu options since 2026-11-13 ("HUD effects").
// Colour indexes are the cluster's palette (F54 at night: 4 white = the route, 3 grey = the context tone, 5 orange; 10 = 0x0A transparent).
bool g_concave, g_fillOn, g_blinkOn;
u32 g_fxCap = 300;
u8 g_fxColour = 5;  // orange (F54), as the BMW navigation uses
// a switch file: present -> true; up to two numbers in it go to a and b
bool readSwitch(const char *name, u32 *a, u32 *b) {
  char path[96];
  snprintf(path, sizeof path, "/var/opt/hmi/data/rw/carplaymenu/%s", name);
  int fd = open(path, 0);
  if (fd < 0)
    return false;
  char t[32];
  long n = read(fd, t, sizeof t - 1);
  close(fd);
  if (n < 0)
    n = 0;
  t[n] = 0;
  u32 *out[2] = { a, b };
  u32 k = 0;
  for (char *q = t; *q && k < 2;) {
    if (*q >= '0' && *q <= '9') {
      u32 v = 0;
      while (*q >= '0' && *q <= '9')
        v = v * 10 + (u32)(*q++ - '0');
      if (out[k])
        *out[k] = v;
      k++;
    } else
      q++;
  }
  return true;
}
void chooseBox() {
  int mono = open("/var/opt/hmi/data/rw/carplaymenu/hud_mono", 0);
  if (mono >= 0) {
    close(mono);
    g_fillDim = 4;
  }
  u32 cap = g_fxCap, col = g_fxColour;
  g_concave = !readSwitch("hud_convex", 0, 0);
  g_fillOn = readSwitch("hud_fill", &cap, &col);
  g_blinkOn = readSwitch("hud_blink", 0, 0);  // the menu writes "1": no colour in it
  readSwitch("hud_colour", &col, 0);
  if (cap >= 50 && cap <= 5000)
    g_fxCap = cap;
  if (col < 20)
    g_fxColour = (u8)col;
  logf1("CarPlayMenu: HUD: test switches concave %u fill %u blink %u", g_concave, g_fillOn, g_blinkOn);
  if (g_fillOn || g_blinkOn)
    logf1("CarPlayMenu: HUD: effect cap %u m, colour %u", g_fxCap, g_fxColour);
  int f = open("/opt/hmi/data/ro/mini/id5/widget/generated/Spec_State_Merged.pbw", 0);
  if (f < 0)
    f = open("/opt/hmi/ID5/data/ro/mini/id5/widget/generated/Spec_State_Merged.pbw", 0);
  g_boxMini = f >= 0;
  if (f >= 0)
    close(f);
  int o = open("/var/opt/hmi/data/rw/carplaymenu/hud_box_6wa", 0);
  if (o >= 0) {
    close(o);
    g_boxMini = false;
  }
  if (g_log)
    g_log(g_boxMini ? "CarPlayMenu: HUD: picture box MINI (35,15)-(85,65)" : "CarPlayMenu: HUD: picture box 6WA (70,20)-(170,120)");
}
enum {
  hFrame = 1,
  hStreet = 2,
  hDist = 3,
  hUnit = 4,
  hBufA = 5,
  hBufB = 6,
  kFill = 4
};  // 6WA profile (F20)

// a MOST message through DSYSMOSTServer: destination, FBlock, instance, function, OpType, data
bool send(u16 dst, u8 fb, u8 inst, u16 fkt, u8 op, const u8 *d, u32 n) {
  if (!g_registered || !dst)
    return false;
  Buf p;
  p.u32_(g_client);
  p.u32_(0);
  p.u32_(1);
  p.u32_(0);  // transmission, TXControl variant, value
  p.u16_(dst);
  p.u16_(g_self);
  p.u8_(fb);
  p.u8_(inst);
  p.u16_(fkt);
  p.u8_(op);
  p.u32_(++g_sent);
  p.u32_(n);
  p.bytes(d, n);
  lock();
  bool ok = request(kReqProcessMessage, p);
  unlock();
  return ok;
}
bool call(u16 fkt, const u8 *d, u32 n) {
  if (!g_registered || !g_host)
    return false;
  dbgOut(fkt, d, n);
  noteSent(fkt, d, n);
  bool ok = send(g_host, kFBlock, kInst, fkt, kOpStartResultAck, d, n);
  usleep(20000);  // like the navigation: one call at a time, no bursts
  return ok;
}
// "Media gestures": AudioMaster VolumeActual Increment (steps > 0) or Decrement, with the number of steps (u8)
bool volume(int steps) {
  u8 n = (u8)(steps > 0 ? (steps > 255 ? 255 : steps) : (-steps > 255 ? 255 : -steps));
  return send(g_amHost, kAudioMaster, 0, kVolumeActual, steps > 0 ? kOpIncrement : kOpDecrement, &n, 1);
}
void be16(u8 *o, u16 v) {
  o[0] = (u8)(v >> 8);
  o[1] = (u8)v;
}
bool callH(u16 fkt, u16 h) {
  u8 d[2];
  be16(d, h);
  return call(fkt, d, 2);
}
bool callHB(u16 fkt, u16 h, u8 b) {
  u8 d[3];
  be16(d, h);
  d[2] = b;
  return call(fkt, d, 3);
}
bool switchVis(u16 hide, u16 show) {
  u8 d[4];
  be16(d, hide);
  be16(d + 2, show);
  return call(kSwitchVisibility, d, 4);
}
// handle, 00, UTF-16BE, 00 00. The street names come from the iPhone in UTF-8; the MINI F54 cluster shows Latin with diacritics and
// Cyrillic (tested in the car with "ŽĐ Бул"), so they are converted, not stripped. Up to 32 characters; outside the BMP, control
// characters and broken sequences become a space.
bool setText(u16 h, const char *s) {
  u8 d[2 + 1 + 32 * 2 + 2];
  u32 n = 0;
  be16(d, h);
  n = 2;
  d[n++] = 0;
  const u8 *q = (const u8 *)s;
  for (u32 k = 0; *q && k < 32; k++) {
    u32 c = *q++, more = 0;
    if (c >= 0xf0) {
      c &= 0x07;
      more = 3;
    } else if (c >= 0xe0) {
      c &= 0x0f;
      more = 2;
    } else if (c >= 0xc0) {
      c &= 0x1f;
      more = 1;
    } else if (c >= 0x80) {
      c = ' ';
    }
    for (; more && (*q & 0xc0) == 0x80; more--)
      c = c << 6 | (*q++ & 0x3f);
    if (more || c < 0x20 || c > 0xffff || c == ',')
      c = ' ';
    d[n++] = (u8)(c >> 8);
    d[n++] = (u8)c;
  }
  d[n++] = 0;
  d[n++] = 0;
  return call(kSetText, d, n);
}
// filled (type 3). Convex pieces of up to kPiecePts points, or with whole (a whole polygon, up to kPolyPts: not hulled)
bool polygon(u16 h, u16 idx, const Pt *p, u32 k, bool whole = false) {
  static Pt q[kPolyPts];
  if (k > (whole ? kPolyPts : kPiecePts))
    return false;
  for (u32 i = 0; i < k; i++)
    q[i] = p[i];
  if (g_boxMini) {
    k = whole ? toMiniPoly(q, k) : toMini(q, k);
    if (k < 3)
      return true;
  }  // half size, the MINI cluster's picture area
  static u8 d[6 + 4 * kPolyPts];
  be16(d, h);
  be16(d + 2, idx);
  d[4] = 3;
  d[5] = (u8)k;
  for (u32 i = 0; i < k; i++) {
    be16(d + 6 + 4 * i, (u16)q[i].x);
    be16(d + 8 + 4 * i, (u16)q[i].y);
  }
  return call(kAddPolygon, d, 6 + 4 * k);
}

// Maneuver pictures: hudshapes.h (all 54 iAP2 / CPManeuverType types, junction side roads).

// ---- guidance state (librghook's file) --------------------------------------------------------------------------------------------
struct Guide {
  bool active, hold;
  u32 type, index, dist, step, units, side, jtype, nang, nl;
  int angle, ang[8];
  char distS[24], road[96];
  Lane lanes[8];
};
bool keyIs(const char *l, const char *k) {
  u32 i = 0;
  while (k[i] && l[i] == k[i])
    i++;
  return !k[i] && l[i] == ':';
}
const char *valOf(const char *l) {
  const char *c = l;
  while (*c && *c != ':')
    c++;
  if (*c)
    c++;
  while (*c && *c != ':')
    c++;
  return *c ? c + 1 : c;
}
// parses a decimal number with an optional leading "-" (reported through neg)
u32 num(const char *s, int *neg = 0) {
  bool m = *s == '-';
  if (m)
    s++;
  u32 v = 0;
  while (*s >= '0' && *s <= '9')
    v = v * 10 + (u32)(*s++ - '0');
  if (neg)
    *neg = m;
  return v;
}
void copyS(char *d, u32 cap, const char *s) {
  u32 i = 0;
  while (s[i] && s[i] != '\n' && i + 1 < cap) {
    d[i] = s[i];
    i++;
  }
  d[i] = 0;
}
bool g_noFile;  // no file: no iPhone session with guidance
bool readGuide(Guide &g) {
  memset(&g, 0, sizeof g);
  int fd = open(kPps, 0);
  g_noFile = fd < 0;
  if (fd < 0)
    return false;
  char b[2048];
  long n = read(fd, b, sizeof b - 1);
  close(fd);
  if (n <= 0)
    return false;
  b[n] = 0;
  u32 rs = 0;
  bool man = false;
  char cur[40] = "";
  for (char *l = b; *l;) {
    char *e = l;
    while (*e && *e != '\n')
      e++;
    char sv = *e;
    *e = 0;
    const char *v = valOf(l);
    if (keyIs(l, "route_state"))
      rs = num(v);
    else if (keyIs(l, "dist_to_maneuver"))
      g.dist = num(v);
    else if (keyIs(l, "dist_to_maneuver_string"))
      copyS(g.distS, sizeof g.distS, v);
    else if (keyIs(l, "dist_to_maneuver_units"))
      g.units = num(v);
    else if (keyIs(l, "maneuver_step_length"))
      g.step = num(v);  // rghook.c: the maneuver's step length (0x5202 TLV 5), metres
    else if (keyIs(l, "maneuver_junction_type"))
      g.jtype = num(v);
    else if (keyIs(l, "maneuver_junction_angles")) {  // "-90,0,90"
      g.nang = 0;
      for (const char *q = v; *q && g.nang < 8;) {
        int sg = 1;
        if (*q == '-') {
          sg = -1;
          q++;
        }
        int x = 0;
        bool dig = false;
        while (*q >= '0' && *q <= '9') {
          x = x * 10 + (*q++ - '0');
          dig = true;
        }
        if (dig)
          g.ang[g.nang++] = sg * x;
        while (*q && *q != ',' && *q != '\n')
          q++;
        if (*q == ',')
          q++;
        else
          break;
      }
    } else if (keyIs(l, "lanes")) {  // "status/highlight/angle,angle;..." left to right (rghook.c)
      g.nl = 0;
      for (const char *q = v; *q && *q != '\n' && g.nl < 8;) {
        Lane &L = g.lanes[g.nl++];
        L.status = num(q);
        L.hl = 1000;
        L.na = 0;
        while (*q >= '0' && *q <= '9')
          q++;
        if (*q == '/') {
          q++;
          int ng;
          u32 h = num(q, &ng);
          L.hl = ng ? -(int)h : (int)h;
          if (*q == '-')
            q++;
          while (*q >= '0' && *q <= '9')
            q++;
        }
        if (*q == '/')
          q++;
        while (*q && *q != ';' && *q != '\n') {
          int ng;
          u32 a = num(q, &ng);
          if (*q == '-')
            q++;
          bool dig = *q >= '0' && *q <= '9';
          while (*q >= '0' && *q <= '9')
            q++;
          if (dig && L.na < 4)
            L.ang[L.na++] = ng ? -(int)a : (int)a;
          if (*q == ',')
            q++;
          else
            break;
        }
        while (*q && *q != ';' && *q != '\n')
          q++;
        if (*q == ';')
          q++;
      }
    } else if (keyIs(l, "maneuver_index")) {
      g.index = num(v);
      man = true;
    } else if (keyIs(l, "maneuver_type"))
      g.type = num(v);
    else if (keyIs(l, "maneuver_exit_angle")) {
      int ng;
      u32 a = num(v, &ng);
      g.angle = ng ? -(int)a : (int)a;
    } else if (keyIs(l, "maneuver_driving_side"))
      g.side = num(v);
    else if (keyIs(l, "maneuver_road"))
      copyS(g.road, sizeof g.road, v);
    else if (keyIs(l, "current_road"))
      copyS(cur, sizeof cur, v);
    *e = sv;
    l = sv ? e + 1 : e;
  }
  if (!g.road[0])
    copyS(g.road, sizeof g.road, cur);
  g.active = man && (rs == 1 || rs == 5 || rs == 6);  // route active, recalculating, heading to the route
  g.hold = rs == 3 || rs == 4;                        // Loading / Locating (seen in the car between routes): keep the last picture
  return true;
}
void splitDistText(const Guide &g, char *num_, char *unit) {  // "450 m" -> "450" + "m"; without the iPhone's text, metres
  num_[0] = unit[0] = 0;
  if (g.distS[0]) {
    u32 i = 0, k = 0;
    while (g.distS[i] == ' ')
      i++;
    while (g.distS[i] && g.distS[i] != ' ' && k < 8)
      num_[k++] = g.distS[i++];
    num_[k] = 0;
    while (g.distS[i] == ' ')
      i++;
    copyS(unit, 8, g.distS + i);
  } else if (g.dist >= 1000) {
    snprintf(num_, 9, "%u.%u", g.dist / 1000, (g.dist % 1000) / 100);
    copyS(unit, 8, "km");
  } else {
    snprintf(num_, 9, "%u", g.dist);
    copyS(unit, 8, "m");
  }
}
// iAP2 distance units (RouteGuidance): 0 km, 1 mi, 2 m, 3 yd, 4 ft (in the car: "4" with 0 for 3961 m, "50" with 2 for 50 m)
void splitDist(const Guide &g, char *num_, char *unit) {
  splitDistText(g, num_, unit);
  if (g.distS[0] && !unit[0]) {
    static const char *const kU[5] = { "km", "mi", "m", "yd", "ft" };
    if (g.units < 5)
      copyS(unit, 8, kU[g.units]);
  }
}

// ---- drawing ------------------------------------------------------------------------------------------------------------------------
volatile int g_enabled;
bool g_shown, g_fresh = true;
u16 g_visible = hBufB;
u32 g_lastType = ~0u, g_lastIndex = ~0u;
int g_lastAngle, g_lastJ;
char g_lastRoad[96], g_lastNum[9], g_lastUnit[8];

u16 otherBuf(u16 b) {
  return b == hBufA ? hBufB : hBufA;
}
u8 toneColour(u32 t) {
  return t == 2 ? g_fxColour : t == 1 ? g_fillDim : (u8)kFill;
}
Shape g_shape;
Polys g_polys;  // large: not on the worker thread's stack
bool g_blinking;
u32 g_level = ~0u, g_blinkTick;  // hud_fill / hud_blink state: the level drawn, both buffers hold a phase
// one picture into buffer buf (emptied first). level: the progress fill 0..8 (hud_fill); high: the blink's bright phase, the whole
// maneuver in the effect colour. The lanes are added after the effect, so they never fill or blink.
void drawPicture(u16 buf, const Guide &g, u32 level, bool high) {
  Shape &s = g_shape;
  s = shapeOf(g.type, g.angle, g.side, g.jtype, g.ang, g.nang);
  if (high)
    allTone(s, 0, 2);
  else
    applyFill(s, level);
  addLanes(s, g.lanes, g.nl, g.side == 1);  // the iPhone's lanes under the arrow, while it shows them
  callH(kRemoveAll, buf);
  callHB(kSetLineWidth, buf, 0);
  callHB(kSetFillColor, buf, kFill);  // as the BMW navigation does
  u32 tone = 0;
  if (g_concave) {  // whole polygons (always, unless the test switch hud_convex)
    mergePieces(s, g_polys);
    for (u32 i = 0; i < g_polys.n; i++) {
      const Poly &q = g_polys.q[i];
      if (q.t != tone) {
        tone = q.t;
        callHB(kSetFillColor, buf, toneColour(tone));
      }
      polygon(buf, (u16)i, q.p, q.n, true);
    }
  } else {
    for (u32 i = 0; i < s.n; i++) {
      if (s.t[i] != tone) {
        tone = s.t[i];
        callHB(kSetFillColor, buf, toneColour(tone));
      }
      polygon(buf, (u16)i, s.p[i], s.k[i]);
    }
  }
}
void drawManeuver(const Guide &g, const char *dn, const char *du, u32 level = 0) {
  u16 hidden = otherBuf(g_visible);
  drawPicture(hidden, g, level, false);
  switchVis(g_visible, hidden);
  callH(kRemoveAll, g_visible);
  g_visible = hidden;
  if (g_fresh) {
    callHB(kSetVisibility, hStreet, 1);
    callHB(kSetVisibility, hDist, 1);
    callHB(kSetVisibility, hUnit, 1);
    g_fresh = false;
  }
  setText(hStreet, g.road);
  setText(hDist, dn);
  setText(hUnit, du);
  callHB(kSetVisibility, hFrame, 1);
  g_shown = true;
}
void drawClear() {  // the state the BMW navigation leaves the terminal in at the end of a route
  callHB(kSetVisibility, hFrame, 0);
  callH(kRemoveAll, hBufA);
  callH(kRemoveAll, hBufB);
  if (g_visible != hBufB) {
    switchVis(hBufA, hBufB);
    g_visible = hBufB;
  }
  setText(hStreet, "");
  setText(hDist, "");
  setText(hUnit, "");
  callHB(kSetVisibility, hStreet, 0);
  g_shown = false;
  g_fresh = true;
  g_lastType = g_lastIndex = ~0u;
  g_blinking = false;
  g_level = ~0u;
}

// Test channel: /dev/shmem/carplaymenu-hudcmd, one HUDTerminal call per line, "fkt hexbytes" (e.g. "21d 0005000003040070007800800078"),
// run once and the file deleted; to try a cluster's layout without rebuilding the plugin. Lines starting with # are skipped. Each
// batch restarts the 600-line HUD debug log, so its calls and the cluster's answers land in the log (tools/hud_probe.sh).
const char *const kCmd = "/dev/shmem/carplaymenu-hudcmd";
// value of one hex digit, -1 if c is not one
int hexv(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
void runCmds() {
  int fd = open(kCmd, 0);
  if (fd < 0)
    return;
  static char b[8192];
  long n = read(fd, b, sizeof b - 1);
  close(fd);
  unlink(kCmd);
  if (n <= 0)
    return;
  b[n] = 0;
  g_dbg = 0;  // log this batch and the cluster's answers, even after the first 600 lines and without debug_log
  g_probeMs = nowMs() | 1;
  u32 ran = 0;
  for (char *l = b; *l;) {
    char *e = l;
    while (*e && *e != '\n')
      e++;
    char sv = *e;
    *e = 0;
    if (*l && *l != '#') {
      u32 fkt = 0;
      char *q = l;
      while (hexv(*q) >= 0)
        fkt = fkt << 4 | (u32)hexv(*q++);
      u8 d[128];
      u32 k = 0;
      int hi = -1;
      for (; *q && k < sizeof d; q++) {
        int v = hexv(*q);
        if (v < 0)
          continue;
        if (hi < 0)
          hi = v;
        else {
          d[k++] = (u8)(hi << 4 | v);
          hi = -1;
        }
      }
      if (fkt) {
        call((u16)fkt, d, k);
        ran++;
      }
    }
    *e = sv;
    l = sv ? e + 1 : e;
  }
  logf1("CarPlayMenu: HUD: test channel ran %u calls", ran);
}

// The installer's choice (carplaymenu/cluster: most / kombi / both). "kombi": librghook-kombi.so sends the directions to the Ethernet
// KOMBI and nothing is drawn here, even on a car that also has a MOST HUDTerminal; "most", "both" or no file: drawn here as before.
bool kombiOnly() {
  int fd = open("/var/opt/hmi/data/rw/carplaymenu/cluster", 0);
  if (fd < 0)
    return false;
  char b[8] = { 0 };
  long n = read(fd, b, sizeof b - 1);
  close(fd);
  (void)n;
  return b[0] == 'k';
}

void *worker(void *) {
  u32 lastLog = 0;
  bool kombi = kombiOnly();
  if (kombi && g_log)
    g_log("CarPlayMenu: HUD: cluster 'kombi': the directions go to the KOMBI from librghook-kombi.so, nothing drawn on MOST");
  for (;;) {
    // without a route every 1 s (the file only exists in a session with guidance); 300 ms while an effect runs (the blink's 600 ms phase)
    usleep(g_noFile && !g_shown ? 1000000 : g_shown && (g_fillOn || g_blinkOn) ? 300000 : 500000);
    if (!g_registered || !g_host)
      continue;
    runCmds();
    if (kombi)
      continue;
    bool nav = g_navMs && nowMs() - g_navMs < 5000;  // the BMW navigation uses the terminal: it has priority
    if (nav) {
      if (g_shown) {
        g_shown = false;
        g_fresh = true;
        g_lastType = g_lastIndex = ~0u;
        g_blinking = false;
        g_level = ~0u;
        if (g_log)
          g_log("CarPlayMenu: HUD: BMW navigation is guiding, CarPlay guidance yields");
      }
      continue;
    }
    Guide g;
    bool got = g_enabled && readGuide(g);
    bool have = got && g.active;
    if (!have && got && g.hold && g_shown)
      continue;
    if (!have) {
      if (g_shown) {
        drawClear();
        if (g_log)
          g_log("CarPlayMenu: HUD: cleared");
      }
      continue;
    }
    char dn[9], du[8];
    splitDist(g, dn, du);
    int jsum = (int)g.jtype * 7919 + (int)g.nang;
    for (u32 i = 0; i < g.nang; i++)
      jsum = jsum * 31 + g.ang[i];
    jsum = jsum * 131 + (int)g.nl;  // the lanes: a change redraws the picture
    for (u32 i = 0; i < g.nl; i++) {
      jsum = jsum * 31 + (int)g.lanes[i].status * 7 + g.lanes[i].hl;
      for (u32 j = 0; j < g.lanes[i].na; j++)
        jsum = jsum * 31 + g.lanes[i].ang[j];
    }
    bool same = g.type == g_lastType && g.index == g_lastIndex && g.angle == g_lastAngle && jsum == g_lastJ;
    bool roadSame = true;
    for (u32 i = 0; i < sizeof g_lastRoad; i++) {
      if (g.road[i] != g_lastRoad[i]) {
        roadSame = false;
        break;
      }
      if (!g.road[i])
        break;
    }
    // test switches hud_fill / hud_blink: the range is the step length, at most the cap (mib2q-carplay-rgi's bargraph denominator)
    u32 level = 0;
    bool blinkZone = false;
    // the start marker (type 11, "start route") neither fills nor blinks: it is not a turn to count down to
    if ((g_fillOn || g_blinkOn) && g.type != 11) {
      u32 zone = g.step && g.step < g_fxCap ? g.step : g_fxCap, d = g.dist < zone ? g.dist : zone;
      if (g_fillOn)
        level = (u32)udiv((int)(zone - d) * 8, (int)zone);
      blinkZone = g_blinkOn && g.dist <= (u32)udiv((int)zone, 5);
    }
    bool newMan = !same || !roadSame || !g_shown, first = !g_shown;  // first: a route's first picture, logged without debug_log too
    if (newMan || blinkZone != g_blinking || (!blinkZone && g_fillOn && level != g_level)) {
      if (blinkZone) {  // low phase on show (route colour), high phase in the other buffer
        drawManeuver(g, dn, du, 0);
        drawPicture(otherBuf(g_visible), g, 0, true);
        g_blinking = true;
        g_blinkTick = 0;
      } else {
        drawManeuver(g, dn, du, level);
        g_blinking = false;
      }
      g_level = level;
      if (newMan && (first || verbose()))
        logf1("CarPlayMenu: HUD: maneuver %u type %u, %u m", g.index, g.type, g.dist);
      else if (!newMan && verbose())
        logf1("CarPlayMenu: HUD: fill level %u, blink %u, %u m", level, blinkZone, g.dist);
    } else {
      if (g_blinking && ++g_blinkTick % 2 == 0) {
        u16 o = otherBuf(g_visible);
        switchVis(g_visible, o);
        g_visible = o;
      }  // 600 ms
      bool dSame = true, uSame = true;
      for (u32 i = 0; i < 9; i++) {
        if (dn[i] != g_lastNum[i]) {
          dSame = false;
          break;
        }
        if (!dn[i])
          break;
      }
      for (u32 i = 0; i < 8; i++) {
        if (du[i] != g_lastUnit[i]) {
          uSame = false;
          break;
        }
        if (!du[i])
          break;
      }
      if (!dSame)
        setText(hDist, dn);
      if (!uSame)
        setText(hUnit, du);
    }
    g_lastType = g.type;
    g_lastIndex = g.index;
    g_lastAngle = g.angle;
    g_lastJ = jsum;
    memcpy(g_lastRoad, g.road, sizeof g_lastRoad);
    memcpy(g_lastNum, dn, sizeof g_lastNum);
    memcpy(g_lastUnit, du, sizeof g_lastUnit);
    if (nowMs() - lastLog > 30000 && verbose()) {
      lastLog = nowMs();
      logf1("CarPlayMenu: HUD: %u calls sent, %u answered by the cluster", g_sent, g_answers);
    }
  }
  return 0;
}

bool g_started;
// Opens the MOST client and the drawing thread (once): for the HUD and for the "Media gestures" volume.
void start() {
  if (g_started)
    return;
  g_started = true;
  chooseBox();
  if (!dsiOpen()) {
    if (g_log)
      g_log("CarPlayMenu: HUD: MOST client not started");
    return;
  }
  int t;
  if (pthread_create(&t, 0, worker, 0) != 0 && g_log)
    g_log("CarPlayMenu: HUD: worker thread failed");
}
// Switches the drawing on/off; off only makes the thread clear the HUD.
void enable(bool on) {
  g_enabled = on ? 1 : 0;
  if (on)
    start();
}

}  // namespace hud
