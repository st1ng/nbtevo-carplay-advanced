/* libcppinch.so — preloaded into the head unit's Connectivity process for the CarPlayMenu plugin: knob directions, knob rotation as
 * map zoom (a hidden two-finger CarPlay touchscreen, "knob rotation as map zoom" below) and the two-finger touchpad (iDrive Touch
 * Controller).
 *
 * Knob directions: CCarplayServiceMgr::setKnobEvt(select, home, back, x, y, wheel) is how the HMI's controller reaches the iPhone's
 * knob HID, but the KnobEvent Connectivity builds from the HMI's message has no directions (x = y = 0). The plugin writes each
 * direction (controller tilt or touchpad gesture) to /dev/shmem/carplaymenu-knob; a thread here sends it as setKnobEvt(0,0,0,x,y,0)
 * and a release. The object comes from CCarplayServiceMgr::start (and every setKnobEvt), interposed: libconn_carplaydevctrl.so,
 * loaded at start, calls both through its PLT. The knob HID carries X/Y only with [KnobInput] NudgeSupport=YES in the carplay.ini
 * that start() parses (setKnobEvt itself zeroes x and y otherwise). X and Y are absolute axes of -127..127 in that descriptor
 * (CCarPlayServiceCore::getHIDDescriptor_Knob: 09 30 09 31 15 81 25 7f 75 08 95 02 81 02), so a nudge is sent at full deflection:
 * +-1 reached the iPhone in the car (2026-11-12) and moved nothing. Size and hold time can be tried without a rebuild: the switch
 * file knob_nudge with "<size> <ms>" (read on every nudge).
 *
 * The HMI passes both touchpad fingers on (AuxIn CarPlayTouchPadInput); the iPhone never gets the second one: the CarPlay touchpad
 * HID that Connectivity declares (libconn_carplay.so, NBT Evo 006_017_006) has one contact plus two handwriting channels, and
 * CCarPlayVirtualHID::touchPadUpdate(touch, x, y, len1, data1, q1, len2, data2, q2) reports exactly one contact. This library
 * replaces both by symbol interposition (libconn_carplay calls them through its own GOT/PLT: R_ARM_GLOB_DAT / R_ARM_JUMP_SLOT):
 *
 *   getHIDDescriptor_TouchPad  the original 160-byte descriptor, its one finger collection turned into two, each with a contact
 *                              index (Digitizer Transducer Index, 0x38, 8 bits), as multi-touch CarPlay HIDs declare it (cf. LIVI,
 *                              github.com/f-io/LIVI, livi-cp/src/hid.rs); the handwriting collections stay as they were.
 *   touchPadUpdate             26-byte report: {index 0, tip, x, y} {index 1, tip, x, y} + the 14 handwriting bytes as before.
 *
 * Finger 2 is not in Connectivity's input (its SetTouchPadEvent has one contact): the CarPlayMenu plugin, in the HMI, writes both
 * fingers to /dev/shmem/carplaymenu-pad on every touchpad event (src/cpm.cpp). Finger 2 is scaled by finger 1's own ratio
 * (HMI 0..511 -> what Connectivity receives); a call that turns out to carry finger 2 is skipped, so it does not matter whether the
 * Media app forwards one finger or each finger in turn. Without a fresh file (option off, one finger) finger 2 is reported lifted.
 *
 * Started only while "Remap iDrive touch panel" is on (the plugin's boot.sh shadows /opt/conn/bin/Connectivity with a script that
 * preloads this). Built and checked byte for byte on the Mac, interposition checked against libconn_carplay.so.
 * Log: /dev/shmem/cppinch.log. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern void *malloc(unsigned long);
extern void free(void *);
extern void *memcpy(void *, const void *, unsigned long);
extern void *memset(void *, int, unsigned long);
extern int open(const char *, int, ...);
extern long read(int, void *, unsigned long);
extern long write(int, const void *, unsigned long);
extern int close(int);
extern int snprintf(char *, unsigned long, const char *, ...);
extern int pthread_create(int *, const void *, void *(*)(void *), void *);
extern int usleep(unsigned);
struct pinch_ts {
  long sec;
  long nsec;
};
extern int clock_gettime(int, struct pinch_ts *);

#define PAD_FILE  "/dev/shmem/carplaymenu-pad"
#define PAD_MAGIC 0x50414432u /* "PAD2" */
struct pad {
  u32 magic, seq, ms;
  u16 x1, y1, x2, y2;
  u8 down1, down2, pad[2];
}; /* written by the plugin, HMI units 0..511 */

static u32 now_ms(void) {
  struct pinch_ts t;
  clock_gettime(2, &t);
  return (u32)(t.sec * 1000 + t.nsec / 1000000);
}
static void log_text(const char *s) {
  int fd = open("/dev/shmem/cppinch.log", 0x109 /* O_WRONLY|O_CREAT|O_APPEND */, 0666);
  if (fd < 0)
    return;
  u32 n = 0;
  while (s[n])
    n++;
  write(fd, s, n);
  write(fd, "\n", 1);
  close(fd);
}
u32 _ZN19CCarPlayServiceCore25getHIDDescriptor_TouchPadEPPh(void *, u8 **);
int _ZN18CCarPlayVirtualHID14touchPadUpdateEiiiiPhiiS0_i(void *, int, int, int, int, u8 *, int, int, u8 *, int);
int _ZN18CCarplayServiceMgr5startEPKc(void *, const char *);
int _ZN18CCarplayServiceMgr10setKnobEvtEbbbiii(void *, int, int, int, int, int, int);
int _ZN18CCarplayServiceMgr14setTouchPadEvtEbiiiPhiiS0_i(void *, int, int, int, int, u8 *, int, int, u8 *, int);
u32 _ZN19CCarPlayServiceCore22getHIDDescriptor_TouchEPPh(void *, u8 **);
int _ZN18CCarPlayVirtualHID11touchUpdateEhii(void *, int, int, int);
static void *original_fn(const char *name) { /* libconn_carplay.so's own definition, never ours (no recursion) */
  void *h = dlopen("libconn_carplay.so", 2);
  void *f = h ? dlsym(h, name) : 0;
  if (f == (void *)_ZN19CCarPlayServiceCore25getHIDDescriptor_TouchPadEPPh ||
      f == (void *)_ZN18CCarPlayVirtualHID14touchPadUpdateEiiiiPhiiS0_i || f == (void *)_ZN18CCarplayServiceMgr5startEPKc ||
      f == (void *)_ZN18CCarplayServiceMgr10setKnobEvtEbbbiii ||
      f == (void *)_ZN18CCarplayServiceMgr14setTouchPadEvtEbiiiPhiiS0_i ||
      f == (void *)_ZN19CCarPlayServiceCore22getHIDDescriptor_TouchEPPh || f == (void *)_ZN18CCarPlayVirtualHID11touchUpdateEhii) {
    log_text("cppinch: dlsym returned our own function: hook disabled");
    return 0;
  }
  return f;
}
static u32 udiv(u32 n, u32 d) { /* no __aeabi_uidiv in the QNX libc */
  if (!d)
    return 0;
  u32 q = 0, r = 0;
  for (int i = 31; i >= 0; i--) {
    r = (r << 1) | ((n >> i) & 1);
    if (r >= d) {
      r -= d;
      q |= 1u << i;
    }
  }
  return q;
}

/* switch debug_log in the settings dir: the per-event lines (touchpad, knob, zoom) only with it; read once, at the first event */
static int flag_set(const char *name);
static int debug_log(void) {
  static int on = -1;
  if (on < 0)
    on = flag_set("debug_log");
  return on;
}

/* with debug_log, the first calls of each kind go to the log, then every 256th: whether the HMI's input reaches Connectivity and
   the HID */
static int log_due(int *n) {
  if (!debug_log())
    return 0;
  int k = ++*n;
  return k <= 12 || (k & 255) == 0;
} /* no % (no __aeabi_idivmod in the QNX libc) */

/* ---- descriptor ------------------------------------------------------------------------------------------------------------------ */
static int g_two; /* the descriptor handed out has two fingers: reports must too */
static u32 g_xmax = 4095, g_ymax = 4095;

/* length of the HID item at d (prefix + data) */
static u32 item_len(u8 b) {
  u32 sz = b & 3;
  return 1 + (sz == 3 ? 4 : sz);
}

/* Builds the two-finger descriptor from the original one; returns its length, 0 if d is not the expected layout.
   d: Usage Page Digitizer, Usage Touch Pad, Collection Application, [finger: Usage Finger, Collection Logical ... End], [char x2], End */
u32 pinch_descriptor(const u8 *d, u32 n, u8 *out, u32 cap) {
  static const u8 head[] = { 0x05, 0x0d, 0x09, 0x05, 0xa1, 0x01 };
  static const u8 fhead[] = { 0x05, 0x0d, 0x09, 0x22, 0xa1, 0x02 };
  static const u8 index[] = {
    0x09, 0x38, 0x15, 0x00, 0x25, 0x01, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02
  }; /* Transducer Index, 0..1, 8 bits */
  if (n < 16)
    return 0;
  for (u32 i = 0; i < sizeof head; i++)
    if (d[i] != head[i])
      return 0;
  for (u32 i = 0; i < sizeof fhead; i++)
    if (d[6 + i] != fhead[i])
      return 0;
  u32 i = 6 + sizeof fhead, depth = 1, end = 0; /* walk items to the finger collection's End Collection */
  while (i < n) {
    u8 b = d[i];
    if (b == 0xa1)
      depth++;
    if (b == 0xc0) {
      if (depth == 1) {
        end = i;
        break;
      }
      depth--;
    }
    i += item_len(b);
  }
  if (!end)
    return 0;
  u32 body = end + 1 - (6 + sizeof fhead); /* finger items after its Collection, End Collection included */
  u32 total = sizeof head + 2 * (sizeof fhead + sizeof index + body) + (n - (end + 1));
  if (total > cap)
    return 0;
  u32 o = 0;
  memcpy(out + o, head, sizeof head);
  o += sizeof head;
  for (int f = 0; f < 2; f++) {
    memcpy(out + o, fhead, sizeof fhead);
    o += sizeof fhead;
    memcpy(out + o, index, sizeof index);
    o += sizeof index;
    memcpy(out + o, d + 6 + sizeof fhead, body);
    o += body;
  }
  memcpy(out + o, d + end + 1, n - (end + 1));
  o += n - (end + 1);
  return o;
}

/* a switch file of the plugin: /var is a link the launcher makes during boot, Connectivity may start before it */
static int flag_set(const char *name) {
  static const char *const dirs[2] = { "/var/opt/hmi/data/rw/carplaymenu/", "/fs/sda1/opt/hmi/data/ID5/carplaymenu/" };
  for (int i = 0; i < 2; i++) {
    char p[128];
    snprintf(p, sizeof p, "%s%s", dirs[i], name);
    int fd = open(p, 0);
    if (fd >= 0) {
      close(fd);
      return 1;
    }
  }
  return 0;
}

/* ---- knob directions -------------------------------------------------------------------------------------------------------------- */
#define KNOB_FILE  "/dev/shmem/carplaymenu-knob"
#define KNOB_MAGIC 0x4b4e4231u /* "KNB1" */
struct knob {
  u32 magic, seq, ms;
  int x, y;
}; /* written by the plugin (cpm.cpp knobWrite); ms 1 = x, y let go */
typedef int (*knob_fn)(void *, int, int, int, int, int, int);
typedef int (*start_fn)(void *, const char *);
static void *volatile g_mgr;
static int read_knob(struct knob *k) {
  int fd = open(KNOB_FILE, 0);
  if (fd < 0)
    return 0;
  long n = read(fd, k, sizeof *k);
  close(fd);
  return n == (long)sizeof *k && k->magic == KNOB_MAGIC;
}
#define NUDGE_SIZE 127 /* the knob descriptor's logical maximum for X and Y */
#define NUDGE_MS   80
/* up to max numbers from a switch file of the plugin ("<a> <b> ..."); returns how many were read (0: no file or none) */
static int switch_numsn(const char *name, int *v, int max) {
  static const char *const dirs[2] = { "/var/opt/hmi/data/rw/carplaymenu/", "/fs/sda1/opt/hmi/data/ID5/carplaymenu/" };
  for (int i = 0; i < 2; i++) {
    char p[128];
    snprintf(p, sizeof p, "%s%s", dirs[i], name);
    int fd = open(p, 0);
    if (fd < 0)
      continue;
    char b[32];
    long n = read(fd, b, sizeof b - 1);
    close(fd);
    int k = 0;
    for (int j = 0; j < max; j++)
      v[j] = 0;
    for (long j = 0; j < n && k < max; j++) {
      if (b[j] >= '0' && b[j] <= '9') {
        v[k] = v[k] * 10 + (b[j] - '0');
        if (j + 1 == n || b[j + 1] < '0' || b[j + 1] > '9')
          k++;
      }
    }
    return k;
  }
  return 0;
}
static int switch_nums(const char *name, int v[2]) {
  return switch_numsn(name, v, 2);
}
/* the test switch knob_nudge: "<size 1..127> <hold ms 10..1000>"; absent or unreadable: the defaults */
static void nudge_params(int *size, int *ms) {
  int v[2];
  int k = switch_nums("knob_nudge", v);
  *size = k >= 1 && v[0] >= 1 && v[0] <= 127 ? v[0] : NUDGE_SIZE;
  *ms = k >= 2 && v[1] >= 10 && v[1] <= 1000 ? v[1] : NUDGE_MS;
}
/* A held direction (the plugin writes it again with ms = 1 when it is let go; the HMI reports the release, car 2026-11-12) repeats:
   the first REPEAT_FIRST ms after the nudge, then one every REPEAT_MS (nudge included), at most REPEAT_MAX ms in case a release never
   comes. The switch file knob_repeat may hold "<first ms> <every ms> <hold ms>"; no_knob_repeat turns it off. (200 ms felt a bit fast.)
   The HMI reports the release up to ~150 ms after the hand lets go (car, 2026-11-13: an extra item after some holds, released 99-150 ms
   after the last repeat), so each repeat is held back REPEAT_HOLD ms past its time and dropped if the release comes in between; the
   first is due REPEAT_FIRST - REPEAT_HOLD after the press, so it still goes out 500 ms after it and the rhythm stays the same. */
#define REPEAT_FIRST 300
#define REPEAT_MS    350
#define REPEAT_HOLD  200
#define REPEAT_MAX   10000
static int repeat_params(int *first, int *every, int *hold) { /* 0 = off */
  if (flag_set("no_knob_repeat"))
    return 0;
  int v[3];
  int k = switch_numsn("knob_repeat", v, 3);
  *first = k >= 1 && v[0] >= 100 && v[0] <= 3000 ? v[0] : REPEAT_FIRST;
  *every = k >= 2 && v[1] >= 100 && v[1] <= 3000 ? v[1] : REPEAT_MS;
  *hold = k >= 3 && v[2] <= 1000 ? v[2] : REPEAT_HOLD;
  return 1;
}
static void *knob_thread(void *a) {
  (void)a;
  struct knob k;
  u32 last = read_knob(&k) ? k.seq : 0; /* an event older than this Connectivity is not replayed */
  knob_fn f = (knob_fn)original_fn("_ZN18CCarplayServiceMgr10setKnobEvtEbbbiii");
  if (!f) {
    log_text("cppinch: original setKnobEvt not found: no knob directions");
    return 0;
  }
  int hx = 0, hy = 0, reps = 0;
  u32 next = 0, until = 0, tpress = 0, tlast = 0; /* the held direction (+-1), its next repeat */
  for (;;) {
    usleep(15000);
    int fresh = read_knob(&k) && k.seq != last;
    void *m = g_mgr;
    if (!fresh) {
      int first, every, hold = REPEAT_HOLD;
      if ((hx || hy) && m && repeat_params(&first, &every, &hold) && (int)(now_ms() - next - (u32)hold) >= 0) {
        if ((int)(now_ms() - until) >= 0) {
          hx = hy = 0;
          continue;
        }
        int size, ms;
        nudge_params(&size, &ms);
        u32 t0 = now_ms();
        tlast = t0;
        f(m, 0, 0, 0, hx * size, hy * size, 0);
        usleep((unsigned)ms * 1000);
        f(m, 0, 0, 0, 0, 0, 0);
        if (++reps <= 3 && debug_log()) {
          char b[64];
          snprintf(b, sizeof b, "cppinch: knob repeat x=%d y=%d", hx * size, hy * size);
          log_text(b);
        }
        next += (u32)every; /* the rhythm of the due times, not of the sends */
        if ((int)(t0 - next) >= 0)
          next = t0; /* fallen behind (a stall): no burst */
      }
      continue;
    }
    u32 gap = k.seq - last;
    last = k.seq;
    if (k.ms & 1) { /* released */
      if ((hx || hy) && debug_log()) {
        char b[96];
        snprintf(b, sizeof b, "cppinch: knob released %u ms after the press, %d repeats, %u ms after the last one",
                 now_ms() - tpress, reps, reps ? now_ms() - tlast : 0);
        log_text(b);
      }
      hx = hy = 0;
      if (gap < 2 || !(k.x || k.y))
        continue; /* else its press was overwritten unread: one nudge */
    }
    if (!m) {
      log_text("cppinch: knob direction before CarPlay started, dropped");
      continue;
    }
    int size, ms;
    nudge_params(&size, &ms);
    hx = k.x < 0 ? -1 : k.x > 0 ? 1 : 0;
    hy = k.y < 0 ? -1 : k.y > 0 ? 1 : 0;
    reps = 0;
    tpress = now_ms();
    f(m, 0, 0, 0, hx * size, hy * size, 0);
    usleep((unsigned)ms * 1000);
    f(m, 0, 0, 0, 0, 0, 0); /* nudge, then centre again */
    if (debug_log()) {
      char b[64];
      snprintf(b, sizeof b, "cppinch: knob nudge x=%d y=%d (%d ms)", hx * size, hy * size, ms);
      log_text(b);
    }
    if (k.ms & 1) {
      hx = hy = 0;
      continue;
    } /* already let go: no repeat */
    int first, every, hold;
    if (!repeat_params(&first, &every, &hold))
      first = REPEAT_FIRST;
    next = now_ms() + (u32)first - (u32)ms;
    until = now_ms() + REPEAT_MAX;
  }
  return 0;
}
static void knob_start(void *mgr) {
  static int started;
  if (mgr && g_mgr != mgr) {
    g_mgr = mgr;
    log_text("cppinch: CarPlay service object captured");
  }
  if (!started) {
    int t;
    started = 1;
    if (pthread_create(&t, 0, knob_thread, 0) != 0)
      log_text("cppinch: knob thread failed");
  }
}
/* CCarplayServiceMgr::start(const char *config): the service object Connectivity uses for the knob; it parses the carplay.ini named
   here once (CConfigReader::parse), and the knob's NudgeSupport is read from that */
int _ZN18CCarplayServiceMgr5startEPKc(void *mgr, const char *a) {
  start_fn f = (start_fn)original_fn("_ZN18CCarplayServiceMgr5startEPKc");
  char b[160];
  snprintf(b, sizeof b, "cppinch: CarPlay start, config %s", a ? a : "(none)");
  log_text(b);
  knob_start(mgr);
  return f ? f(mgr, a) : 0;
}
/* ---- knob rotation as map zoom ("Enable rotation as zoom": switch file knob_zoom) ----------------------------------------------------
   Apple Maps does not zoom from the touchpad: in the car (2026-11-12) a synthetic two-finger pinch on the touchpad HID did nothing (the
   iPhone takes one contact from a CarPlay touchpad), and the knob HID has no zoom. A CarPlay touchscreen with two contacts pinches
   (LIVI's multi-touch touchscreen, github.com/f-io/LIVI, native/crates/livi-cp/src/hid.rs). So the plugin adds a [TouchScreen] section
   without PrimaryInput to Connectivity's carplay.ini (iniwide.h; the touchpad or the knob stays the primary input). Connectivity then
   creates CarPlay's touchscreen HID, whose descriptor is replaced here by a two-finger one (as LIVI's), and a knob rotation (setKnobEvt
   with nothing but a wheel value) becomes a short pinch in the middle of the map instead of a focus step: a positive wheel value spreads
   the fingers (zoom in), a negative one squeezes them (the switch file zoom_reverse swaps them). Finger 1 takes Connectivity's own
   touchscreen path (CCarplayServiceMgr::setTouchEvt, which checks the session, -> CCarPlayVirtualHID::touchUpdate, called through
   libconn_carplay's PLT); touchUpdate below adds finger 2 to the report. Steps that come while a pinch runs are added up: the next
   pinch is bigger (or smaller, or none, if they cancel out). Not tried in a car yet. */
static int g_ts2;                         /* the touchscreen descriptor handed out has two fingers */
static int g_tsw = 800, g_tsh = 480;      /* its X and Y maxima: [Screen] Width and Height */
static volatile int g_tsyn, g_tx2, g_ty2; /* finger 2 while a pinch runs (g_tsyn = touching) */
static volatile int g_zin, g_zout;        /* rotation steps counted by setKnobEvt (its thread only writes) */

/* the stock touchscreen descriptor (CCarPlayServiceCore::getHIDDescriptor_Touch, template at 0x11db1c of libconn_carplay.so in
   1703 and 006): Touch Screen, one finger {Touch, 7 bits, X, Y (16 bits)}; X and Y maxima (bytes 0x27/0x28, 0x34/0x35) filled in */
static const u8 ts_stock[62] = { 0x05, 0x0d, 0x09, 0x04, 0xa1, 0x01, 0x05, 0x0d, 0x09, 0x22, 0xa1, 0x02, 0x05, 0x0d, 0x09, 0x33,
                                 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02, 0x75, 0x07, 0x95, 0x01, 0x81, 0x01,
                                 0x05, 0x01, 0x09, 0x30, 0x15, 0x00, 0x26, 0x00, 0x00, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02, 0x09,
                                 0x31, 0x15, 0x00, 0x26, 0x00, 0x00, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02, 0xc0, 0xc0 };
int ts_stock_layout(const u8 *d, u32 n) { /* 1: d is that descriptor (any maxima) */
  if (n != sizeof ts_stock)
    return 0;
  for (u32 i = 0; i < n; i++)
    if (i != 0x27 && i != 0x28 && i != 0x34 && i != 0x35 && d[i] != ts_stock[i])
      return 0;
  return 1;
}
/* two fingers, each {Transducer Index 0..1 (8 bits), Touch, 7 bits, X 0..xmax, Y 0..ymax (16 bits)}: 6 bytes a finger (ts_report).
   Returns the length (129), 0 if cap is too small. */
u32 ts_descriptor(u32 xmax, u32 ymax, u8 *out, u32 cap) {
  static const u8 head[6] = { 0x05, 0x0d, 0x09, 0x04, 0xa1, 0x01 }; /* Digitizer, Touch Screen, Collection Application */
  const u8 finger[61] = { 0x05,
                          0x0d,
                          0x09,
                          0x22,
                          0xa1,
                          0x02, /* Finger, Collection Logical */
                          0x15,
                          0x00,
                          0x25,
                          0x01,
                          0x09,
                          0x38,
                          0x75,
                          0x08,
                          0x95,
                          0x01,
                          0x81,
                          0x02, /* Transducer Index 0..1, 8 bits */
                          0x09,
                          0x33,
                          0x75,
                          0x01,
                          0x95,
                          0x01,
                          0x81,
                          0x02, /* Touch, 1 bit */
                          0x75,
                          0x07,
                          0x95,
                          0x01,
                          0x81,
                          0x01, /* 7 bits constant */
                          0x05,
                          0x01,
                          0x09,
                          0x30,
                          0x15,
                          0x00,
                          0x26,
                          (u8)xmax,
                          (u8)(xmax >> 8),
                          0x75,
                          0x10,
                          0x95,
                          0x01,
                          0x81,
                          0x02, /* X */
                          0x09,
                          0x31,
                          0x15,
                          0x00,
                          0x26,
                          (u8)ymax,
                          (u8)(ymax >> 8),
                          0x75,
                          0x10,
                          0x95,
                          0x01,
                          0x81,
                          0x02, /* Y */
                          0xc0 };
  u32 n = sizeof head + 2 * sizeof finger + 1;
  if (n > cap)
    return 0;
  memcpy(out, head, sizeof head);
  memcpy(out + 6, finger, sizeof finger);
  memcpy(out + 6 + sizeof finger, finger, sizeof finger);
  out[n - 1] = 0xc0;
  return n;
}
/* the 12-byte report of that descriptor */
void ts_report(u8 *r, int t1, int x1, int y1, int t2, int x2, int y2) {
  r[0] = 0;
  r[1] = (u8)(t1 & 1);
  r[2] = (u8)x1;
  r[3] = (u8)(x1 >> 8);
  r[4] = (u8)y1;
  r[5] = (u8)(y1 >> 8);
  r[6] = 1;
  r[7] = (u8)(t2 & 1);
  r[8] = (u8)x2;
  r[9] = (u8)(x2 >> 8);
  r[10] = (u8)y2;
  r[11] = (u8)(y2 >> 8);
}
#define ZOOM_FRAMES 9 /* reports while the fingers move (the distance in eighths: a shift) */
#define ZOOM_HOLD   3 /* reports at the end, so the gesture ends without speed (no coasting) */
#define ZOOM_MS     16
/* half the distance between the fingers in report i (0..ZOOM_FRAMES-1) of a pinch of `steps` rotation steps on a screen h high:
   spreading (steps > 0) from h/8 to h/8 + h/16 a step (x1.5 a step, at most 4 steps), squeezing the same way back */
int zoom_half_a(int steps, int i, int a) { /* the same from a start half distance a (x1.5 a step) */
  int s = steps < 0 ? -steps : steps;
  if (s < 1)
    s = 1;
  if (s > 4)
    s = 4;
  int b = a + ((s * a) >> 1);
  if (steps < 0) {
    int t = a;
    a = b;
    b = t;
  }
  return a + (((b - a) * i) >> 3);
}
int zoom_half(int steps, int i, int h) {
  return zoom_half_a(steps, i, h >> 3);
}
/* Where the pinch goes. Default: in the top bar, both fingers on a horizontal line through (10/16 of the width, 1/16 of the height),
   h/8 apart (half distance) at the start. Car, 2026-11-13: through the middle of the screen (9/16, 8/16), horizontal or vertical
   (100 px, the fingers moving 50 px each), a pinch outside a map was a tap on the list row under it; in the top bar it taps nothing
   (the title) and Apple Maps still zooms. The test switch zoom_at "<x> <y> <dir> <a>" moves it: the centre at x/16 of the width and
   y/16 of the height, dir 0 horizontal, 1 vertical, 2 diagonal (3:4 slope), a the start half distance in pixels (10..300). Fingers
   are kept on the screen. */
#define ZOOM_X16 10
#define ZOOM_Y16 1
struct zoom_at {
  int cx, cy, dir, a;
};
void zoom_place(int w, int h, const int *v, int k, struct zoom_at *z) {
  z->cx = ((k >= 1 && v[0] >= 1 && v[0] <= 15 ? v[0] : ZOOM_X16) * w) >> 4;
  z->cy = ((k >= 2 && v[1] >= 1 && v[1] <= 15 ? v[1] : ZOOM_Y16) * h) >> 4;
  z->dir = k >= 3 && v[2] >= 0 && v[2] <= 2 ? v[2] : 0;
  z->a = k >= 4 && v[3] >= 10 && v[3] <= 300 ? v[3] : h >> 3;
}
static int clampi(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}
/* finger offsets from the centre for half distance d */
void zoom_offset(int dir, int d, int *dx, int *dy) {
  if (dir == 1) {
    *dx = 0;
    *dy = d;
  } else if (dir == 2) {
    *dx = (d * 4) / 5;
    *dy = (d * 3) / 5;
  } else {
    *dx = d;
    *dy = 0;
  }
}
typedef int (*tevt_fn)(void *, int, int, int, int);
/* one pinch (zoom_place), finger 1 left of / above the centre */
static void zoom_pinch(void *mgr, int steps) {
  static tevt_fn f;
  if (!f)
    f = (tevt_fn)original_fn("_ZN18CCarplayServiceMgr11setTouchEvtEiibi");
  if (!f) {
    log_text("cppinch: zoom: setTouchEvt not found");
    return;
  }
  int v[4];
  int k = switch_numsn("zoom_at", v, 4);
  struct zoom_at z;
  zoom_place(g_tsw, g_tsh, v, k, &z);
  int x1 = z.cx, y1 = z.cy, r = 1, w1 = g_tsw - 1, h1 = g_tsh - 1;
  for (int i = 0; i < ZOOM_FRAMES + ZOOM_HOLD; i++) {
    int dx, dy;
    zoom_offset(z.dir, zoom_half_a(steps, i < ZOOM_FRAMES ? i : ZOOM_FRAMES - 1, z.a), &dx, &dy);
    x1 = clampi(z.cx - dx, 0, w1);
    y1 = clampi(z.cy - dy, 0, h1);
    g_tx2 = clampi(z.cx + dx, 0, w1);
    g_ty2 = clampi(z.cy + dy, 0, h1);
    g_tsyn = 1;
    r &= f(mgr, x1, y1, 1, 0) != 0;
    usleep(ZOOM_MS * 1000);
  }
  g_tsyn = 0;
  f(mgr, x1, y1, 0, 0); /* both lifted */
  if (r && !debug_log())
    return; /* "sent" only with debug_log; "not sent" always */
  char b[140];
  snprintf(b, sizeof b, "cppinch: zoom: %s %d step(s) at %d,%d dir %d from %d, %s", steps > 0 ? "in" : "out",
           steps > 0 ? steps : -steps, z.cx, z.cy, z.dir, z.a, r ? "sent" : "not sent (no CarPlay session or no touchscreen HID)");
  log_text(b);
}
static void *zoom_thread(void *a) {
  (void)a;
  int seen_in = g_zin, seen_out = g_zout;
  for (;;) {
    usleep(15000);
    int zi = g_zin, zo = g_zout, n = (zi - seen_in) - (zo - seen_out);
    seen_in = zi;
    seen_out = zo;
    void *m = g_mgr;
    if (n && m)
      zoom_pinch(m, n < -4 ? -4 : n > 4 ? 4 : n);
  }
  return 0;
}
/* CCarplayServiceMgr::setKnobEvt(select, home, back, x, y, wheel): rotation, press and back from the HMI, passed on unchanged, except
   a rotation with "Enable rotation as zoom" once the two-finger touchscreen exists */
int _ZN18CCarplayServiceMgr10setKnobEvtEbbbiii(void *mgr, int select, int home, int back, int x, int y, int wheel) {
  static knob_fn f;
  if (!f)
    f = (knob_fn)original_fn("_ZN18CCarplayServiceMgr10setKnobEvtEbbbiii");
  knob_start(mgr);
  if (wheel && !select && !home && !back && !x && !y && flag_set("knob_zoom")) {
    static int started, told;
    if (g_ts2) {
      if (!started) {
        int t;
        started = 1;
        if (pthread_create(&t, 0, zoom_thread, 0) != 0)
          log_text("cppinch: zoom thread failed");
      }
      if ((wheel > 0) != flag_set("zoom_reverse"))
        g_zin++;
      else
        g_zout++;
      return 1;
    }
    if (!told) {
      told = 1;
      log_text("cppinch: zoom: no two-finger touchscreen ([TouchScreen] in carplay.ini, Connectivity restarted?): rotation passed on");
    }
  }
  return f ? f(mgr, select, home, back, x, y, wheel) : 0;
}

typedef u32 (*desc_fn)(void *, u8 **);
/* CCarPlayServiceCore::getHIDDescriptor_Touch(unsigned char **) -> length (only called with [TouchScreen] in carplay.ini): two fingers
   with "Enable rotation as zoom" */
u32 _ZN19CCarPlayServiceCore22getHIDDescriptor_TouchEPPh(void *core, u8 **out) {
  desc_fn f = (desc_fn)original_fn("_ZN19CCarPlayServiceCore22getHIDDescriptor_TouchEPPh");
  if (!f) {
    log_text("cppinch: original getHIDDescriptor_Touch not found");
    return 0;
  }
  u32 n = f(core, out);
  if (!flag_set("knob_zoom"))
    return n;
  if (!n || !out || !*out || !ts_stock_layout(*out, n)) {
    log_text("cppinch: touchscreen descriptor not the expected one, left alone");
    return n;
  }
  u8 *d = *out;
  u32 xm = d[0x27] | d[0x28] << 8, ym = d[0x34] | d[0x35] << 8;
  u8 *nd = (u8 *)malloc(160);
  u32 m = nd ? ts_descriptor(xm, ym, nd, 160) : 0;
  if (!m) {
    if (nd)
      free(nd);
    return n;
  }
  free(d);
  *out = nd;
  g_ts2 = 1;
  g_tsw = (int)xm;
  g_tsh = (int)ym;
  char b[96];
  snprintf(b, sizeof b, "cppinch: touchscreen HID with two fingers (%u -> %u bytes, x 0..%u, y 0..%u)", n, m, xm, ym);
  log_text(b);
  return m;
}
typedef int (*tupd_fn)(void *, int, int, int);
typedef int (*post_fn)(void *, const u8 *, u32);
/* CCarPlayVirtualHID::touchUpdate(touch, x, y): 1 = sent. With the two-finger descriptor: finger 1 as given, finger 2 the pinch's */
int _ZN18CCarPlayVirtualHID11touchUpdateEhii(void *vhid, int touch, int x, int y) {
  static int n;
  if (!g_ts2) {
    tupd_fn f = (tupd_fn)original_fn("_ZN18CCarPlayVirtualHID11touchUpdateEhii");
    return f ? f(vhid, touch, x, y) : 0;
  }
  void *dev = *(void **)((u8 *)vhid + 4); /* the touchscreen HID device, as the original reads it */
  static post_fn post;
  if (!post)
    post = (post_fn)original_fn("HIDDevicePostReport");
  if (!dev || !post)
    return 0;
  u8 r[12];
  ts_report(r, touch & 1, x, y, g_tsyn, g_tx2, g_ty2);
  int ok = post(dev, r, 12) == 0;
  if (log_due(&n)) {
    char b[96];
    snprintf(b, sizeof b, "cppinch: touchUpdate #%d %d,%d %d / %d,%d %d -> %d", n, x, y, touch & 1, g_tx2, g_ty2, g_tsyn, ok);
    log_text(b);
  }
  return ok;
}

/* CCarplayServiceMgr::setTouchPadEvt(touch, x, y, len1, data1, q1, len2, data2, q2): Connectivity's touchpad input, passed on (called
   through libconn_carplaydevctrl.so's PLT like setKnobEvt); 0 = not sent (no CarPlay session or no touchpad HID) */
typedef int (*tpevt_fn)(void *, int, int, int, int, u8 *, int, int, u8 *, int);
int _ZN18CCarplayServiceMgr14setTouchPadEvtEbiiiPhiiS0_i(void *mgr, int touch, int x, int y, int len1, u8 *data1, int q1, int len2,
                                                         u8 *data2, int q2) {
  static tpevt_fn f;
  static int n;
  if (!f)
    f = (tpevt_fn)original_fn("_ZN18CCarplayServiceMgr14setTouchPadEvtEbiiiPhiiS0_i");
  int r = f ? f(mgr, touch, x, y, len1, data1, q1, len2, data2, q2) : 0;
  if (log_due(&n)) {
    char b[96];
    snprintf(b, sizeof b, "cppinch: setTouchPadEvt #%d touch=%d x=%d y=%d -> %d", n, touch, x, y, r);
    log_text(b);
  }
  return r;
}

/* CCarPlayServiceCore::getHIDDescriptor_TouchPad(unsigned char **) -> length; the buffer is malloc'ed (freed by the caller) */
u32 _ZN19CCarPlayServiceCore25getHIDDescriptor_TouchPadEPPh(void *core, u8 **out) {
  desc_fn f = (desc_fn)original_fn("_ZN19CCarPlayServiceCore25getHIDDescriptor_TouchPadEPPh");
  if (!f) {
    log_text("cppinch: original getHIDDescriptor_TouchPad not found");
    return 0;
  }
  u32 n = f(core, out);
  if (flag_set("no_pinch")) {
    log_text("cppinch: no_pinch: one-finger touchpad kept");
    return n;
  }
  if (!n || !out || !*out || n != 0xa0) {
    log_text("cppinch: touchpad descriptor not the expected one, left alone");
    return n;
  }
  u8 *d = *out;
  g_xmax = d[0x27] | d[0x28] << 8;
  g_ymax = d[0x3d] | d[0x3e] << 8; /* logical maxima the original filled in */
  u8 *nd = (u8 *)malloc(320);
  u32 m = nd ? pinch_descriptor(d, n, nd, 320) : 0;
  if (!m) {
    if (nd)
      free(nd);
    log_text("cppinch: descriptor layout not recognised, one finger kept");
    return n;
  }
  free(d);
  *out = nd;
  g_two = 1;
  char b[96];
  snprintf(b, sizeof b, "cppinch: touchpad HID with two fingers (%u -> %u bytes, x 0..%u, y 0..%u)", n, m, g_xmax, g_ymax);
  log_text(b);
  return m;
}

/* ---- report ---------------------------------------------------------------------------------------------------------------------- */
/* builds the 26-byte two-finger report (exported for the Mac test) */
void pinch_report(u8 *r, int touch, int x, int y, int tip2, int x2, int y2, int len1, const u8 *data1, int q1, int len2,
                  const u8 *data2, int q2) {
  memset(r, 0, 26);
  r[0] = 0;
  r[1] = (u8)(touch & 1);
  r[2] = (u8)x;
  r[3] = (u8)(x >> 8);
  r[4] = (u8)y;
  r[5] = (u8)(y >> 8);
  r[6] = 1;
  r[7] = (u8)(tip2 & 1);
  r[8] = (u8)x2;
  r[9] = (u8)(x2 >> 8);
  r[10] = (u8)y2;
  r[11] = (u8)(y2 >> 8);
  if (data1 && len1 > 0)
    memcpy(r + 12, data1, len1 > 4 ? 4 : (u32)len1);
  r[16] = 1;
  r[17] = (u8)len1;
  r[18] = (u8)q1;
  if (data2 && len2 > 0)
    memcpy(r + 19, data2, len2 > 4 ? 4 : (u32)len2);
  r[23] = 1;
  r[24] = (u8)len2;
  r[25] = (u8)q2;
}

static int read_pad(struct pad *p) {
  int fd = open(PAD_FILE, 0);
  if (fd < 0)
    return 0;
  long k = read(fd, p, sizeof *p);
  close(fd);
  return k == (long)sizeof *p && p->magic == PAD_MAGIC && now_ms() - p->ms < 400;
}
static int is_near(int a, int b) {
  int d = a - b;
  return d < 0 ? -d < 48 : d < 48;
}

typedef int (*upd_fn)(void *, int, int, int, int, u8 *, int, int, u8 *, int);
/* CCarPlayVirtualHID::touchPadUpdate(touch, x, y, len1, data1, q1, len2, data2, q2): 1 = sent */
int _ZN18CCarPlayVirtualHID14touchPadUpdateEiiiiPhiiS0_i(void *vhid, int touch, int x, int y, int len1, u8 *data1, int q1, int len2,
                                                         u8 *data2, int q2) {
  static int n;
  if (!g_two) {
    upd_fn f = (upd_fn)original_fn("_ZN18CCarPlayVirtualHID14touchPadUpdateEiiiiPhiiS0_i");
    int r = f ? f(vhid, touch, x, y, len1, data1, q1, len2, data2, q2) : 0;
    if (log_due(&n)) {
      char b[96];
      snprintf(b, sizeof b, "cppinch: touchPadUpdate #%d one finger, dev %p -> %d", n, *(void **)((u8 *)vhid + 8), r);
      log_text(b);
    }
    return r;
  }
  void *dev = *(void **)((u8 *)vhid + 8); /* the HID device, as the original reads it */
  if (log_due(&n)) {
    char b[96];
    snprintf(b, sizeof b, "cppinch: touchPadUpdate #%d two fingers, dev %p", n, dev);
    log_text(b);
  }
  if (!dev)
    return 0;
  static post_fn post;
  if (!post)
    post = (post_fn)original_fn("HIDDevicePostReport");
  if (!post)
    return 0;
  struct pad p;
  int tip2 = 0, x2 = 0, y2 = 0;
  if (read_pad(&p) && p.down2) {
    /* scale HMI units to Connectivity's by finger 1's ratio (defaults to the descriptor's range) */
    u32 sx = p.x1 > 16 ? udiv((u32)x << 10, p.x1) : udiv(g_xmax << 10, 511);
    u32 sy = p.y1 > 16 ? udiv((u32)y << 10, p.y1) : udiv(g_ymax << 10, 511);
    x2 = (int)((p.x2 * sx) >> 10);
    y2 = (int)((p.y2 * sy) >> 10);
    int f2x = (int)((p.x2 * sx) >> 10), f2y = (int)((p.y2 * sy) >> 10);
    /* this call carries finger 2 (the Media app forwarding each finger in turn): finger 1's call reports both */
    if (p.down1 && is_near(x, f2x) && is_near(y, f2y) && !(is_near(p.x1, p.x2) && is_near(p.y1, p.y2)))
      return 1;
    if (x2 > (int)g_xmax)
      x2 = (int)g_xmax;
    if (y2 > (int)g_ymax)
      y2 = (int)g_ymax;
    tip2 = 1;
  }
  u8 r[26];
  pinch_report(r, touch, x, y, tip2, x2, y2, len1, data1, q1, len2, data2, q2);
  return post(dev, r, 26) == 0;
}
