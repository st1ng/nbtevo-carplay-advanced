/* rghook.c: CarPlay route guidance for the NBT Evo's iAP2 stack (QNX mm-ipod, "Media 1.0.70", 2017), which predates it.
   LD_PRELOAD into mm-ipod. Only read()/write() on the iAP2 transport descriptor are touched (found by its first iAP2 bytes,
   or $RGHOOK_DEV). The driver itself is not changed:

   outgoing  IdentificationInformation (0x1D01): adds 0x5200/0x5203 to MessagesSentByAccessory, 0x5201/0x5202/0x5204 to
             MessagesReceivedFromDevice and a RouteGuidanceDisplayComponent (param 30), then fixes lengths and checksums.
   inject    StartRouteGuidanceUpdates (0x5200) once IdentificationAccepted (0x1D02) arrives. The packet takes the next link
             sequence number; every later packet of the driver is renumbered (+delta) and every acknowledgement number the phone
             sends back is shifted (-delta), so both ends see a gap-free link.
   incoming  packets are buffered and handed to the driver whole (fields can be rewritten safely). RouteGuidanceUpdate (0x5201),
             RouteGuidanceManeuverUpdate (0x5202) and LaneGuidanceInformation (0x5204) are decoded: the current state goes to
             $RGHOOK_PPS (default /var/iap/pps/routeguidance, PPS-style "key::value" lines), every message to $RGHOOK_LOG.

   kombi     librghook-kombi.so only (built with RGHOOK_KOMBI, installed as librghook.so on cars with an Ethernet KOMBI: G-series,
             J29): the same guidance also goes to the KOMBI's route-guidance service, from a worker thread (rgkombi.cpp, kombi.h);
             rg_view_get() below hands it the state under st_lock. The plain librghook.so (MOST cars) has no network code.

   Parameter layout: same as the community implementation for VW/Audi MIB2 (github.com/WaylandAce/mib2q-carplay-rgi, rgd_tlv.h). */
#include "rgview.h"
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned long size_t;
typedef int s32;
typedef short s16;
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern int snprintf(char *, unsigned long, const char *, ...);
extern char *getenv(const char *);
extern void *memcpy(void *, const void *, unsigned long);
extern void *memset(void *, int, unsigned long);
extern void *memmove(void *, const void *, unsigned long);
extern int strcmp(const char *, const char *);
extern unsigned long strlen(const char *);
extern int sched_yield(void);
extern int getpid(void);
extern int symlink(const char *, const char *);
/* Freestanding QNX build; host harnesses use their libc's thread-local errno accessor. */
#if defined(__APPLE__)
extern int *__error(void);
#define RG_ERRNO (*__error())
#elif defined(__linux__)
extern int *__errno_location(void);
#define RG_ERRNO (*__errno_location())
#else
extern int *__get_errno_ptr(void);
#define RG_ERRNO (*__get_errno_ptr())
#endif
static void *libc(const char *n) {
  return dlsym(dlopen("libc.so.3", 2), n);
}
static long (*r_read)(int, void *, size_t);
static long (*r_write)(int, const void *, size_t);
static int (*r_open)(const char *, int, ...);
static int (*r_close)(int);
static int (*r_unlink)(const char *);
static int (*r_rename)(const char *, const char *);

#define RG_COMP_ID 0x0F0E /* identifier of our RouteGuidanceDisplayComponent (unique among the head unit's components) */
#define SYN        0x80
#define ACK        0x40
static int transport_fd = -1, logfd = -2, ctl_sess = -1, inited, injected;
static u32 delta, last_out_seq, last_in_seq;
static volatile int lock_word;
/* Sleeps instead of spinning: mm-ipod's threads have different fixed priorities, and a higher one looping on sched_yield() never let
   the holder (the reader thread, writing StartRouteGuidanceUpdates under the lock) run again: mm-ipod at 100% CPU, link stalled. */
extern int usleep(unsigned);
static void lock(void) {
  while (__atomic_exchange_n(&lock_word, 1, __ATOMIC_ACQUIRE))
    usleep(1000);
}
static void unlock(void) {
  __atomic_store_n(&lock_word, 0, __ATOMIC_RELEASE);
}
/* the decoded state (st) between the reader thread (on_rg) and the KOMBI worker (rg_view_get) */
static volatile int st_word;
static void st_lock(void) {
  while (__atomic_exchange_n(&st_word, 1, __ATOMIC_ACQUIRE))
    usleep(1000);
}
static void st_unlock(void) {
  __atomic_store_n(&st_word, 0, __ATOMIC_RELEASE);
}
/* write() on the other fds goes to the next one in the chain, not straight to libc: librghook.so comes FIRST in LD_PRELOAD (the
   CarPlayMenu plugin's script, src/cpm.cpp), and a library preloaded after it may need to see those writes. QNX libc has no useful
   RTLD_NEXT: the next one is looked up in the LD_PRELOAD libraries after this one (dlsym on each one's handle). */
long write(int, const void *, size_t);
static void *next_sym(const char *n, void *self) {
  const char *e = getenv("LD_PRELOAD");
  char path[256];
  int seen = 0;
  while (e && *e) {
    unsigned k = 0;
    while (e[k] && e[k] != ':' && k + 1 < sizeof path) {
      path[k] = e[k];
      k++;
    }
    path[k] = 0;
    e += k;
    if (*e == ':')
      e++;
    if (!k)
      continue;
    if (!seen) {
      unsigned m = 0;
      while (path[m])
        m++;
      if (m >= 12 && !strcmp(path + m - 12, "librghook.so"))
        seen = 1;
      continue;
    }
    void *h = dlopen(path, 2);
    void *f = h ? dlsym(h, n) : 0;
    if (f && f != self)
      return f;
  }
  return libc(n);
}
static void rg_clear(void);
static void init(void) {
  if (inited)
    return;
  inited = 1;
  r_read = libc("read");
  r_write = next_sym("write", (void *)write);
  r_open = libc("open");
  r_close = libc("close");
  r_unlink = libc("unlink");
  r_rename = libc("rename");
  rg_clear();
}
static volatile int log_opening;
static void log_text(const char *s) {
  if (logfd == -2) { /* opened (and truncated) once: the KOMBI / MOST worker may log first, with the route guidance thread */
    while (__atomic_exchange_n(&log_opening, 1, __ATOMIC_ACQUIRE))
      usleep(1000);
    if (logfd == -2) {
      const char *p = getenv("RGHOOK_LOG");
      logfd = r_open(p ? p : "/var/iap/rghook.log", 0x301, 0644);
    }
    __atomic_store_n(&log_opening, 0, __ATOMIC_RELEASE);
  }
  if (logfd >= 0)
    r_write(logfd, s, strlen(s));
}
/* A switch file in the plugin's settings folder ($RGHOOK_CFG in tests; /var may not be linked yet when mm-ipod starts): 1 = present. */
static int cfg_switch(const char *name) {
  init();
  const char *cfg = getenv("RGHOOK_CFG");
  for (int i = 0; i < 3; i++) {
    const char *d = i == 0 ? cfg : i == 1 ? "/var/opt/hmi/data/rw/carplaymenu" : "/fs/sda1/opt/hmi/data/ID5/carplaymenu";
    if (!d || !*d)
      continue;
    char p[192];
    snprintf(p, sizeof p, "%s/%s", d, name);
    int fd = r_open(p, 0);
    if (fd >= 0) {
      r_close(fd);
      return 1;
    }
  }
  return 0;
}
/* Switch debug_log: the per-message lines (every route guidance message, the lanes, the control message ids) only with it. Read once. */
int rg_debug_log(void) {
  static volatile int on = -1;
  if (on < 0)
    on = cfg_switch("debug_log");
  return on;
}
/* Test switch keep_start_marker: the start marker (type 11) stays shown even when the iPhone already lists the first turn after it
   (cur_view). Read once. */
static int keep_start_marker(void) {
  static volatile int on = -1;
  if (on < 0)
    on = cfg_switch("keep_start_marker");
  return on;
}
static u8 checksum(const u8 *b, u32 n) {
  u32 s = 0;
  while (n--)
    s += *b++;
  return (u8)(0x100 - (s & 0xff));
}
static u16 be16(const u8 *p) {
  return (u16)(p[0] << 8 | p[1]);
}
static void wbe16(u8 *p, u32 v) {
  p[0] = (u8)(v >> 8);
  p[1] = (u8)v;
}

/* ---------------- identification patch ---------------- */
static u32 tlv(u8 *o, u32 id, const void *d, u32 n) {
  wbe16(o, 4 + n);
  wbe16(o + 2, id);
  if (n)
    memcpy(o + 4, d, n);
  return 4 + n;
}
static u32 tlv16(u8 *o, u32 id, u32 v) {
  u8 b[2];
  wbe16(b, v);
  return tlv(o, id, b, 2);
}
static u32 rg_component(u8 *o) {
  u8 in[256];
  u32 k = 0;
  static const char nm[] = "Head-Up Display";
  k += tlv16(in + k, 0, RG_COMP_ID);
  k += tlv(in + k, 1, nm, sizeof nm); /* identifier, name (UTF-8, NUL-terminated) */
  k += tlv16(in + k, 2, 0x100);
  k += tlv16(in + k, 3, 0x100);
  k += tlv16(in + k, 4, 0x100);
  k += tlv16(in + k, 5, 0x100); /* max road/destination/after-road/description lengths */
  k += tlv16(in + k, 6, 6);     /* max guidance maneuver storage */
  k += tlv16(in + k, 7, 0x100);
  k += tlv16(in + k, 8, 6); /* lane guidance description length, storage */
  return tlv(o, 30, in, k);
}
/* msg = control message (40 40 len id params); returns new length (written to out) */
static u32 patch_ident(const u8 *msg, u32 n, u8 *out) {
  u32 o = 6, i = 6, have = 0;
  memcpy(out, msg, 6);
  while (i + 4 <= n) {
    u32 ln = be16(msg + i), id = be16(msg + i + 2);
    if (ln < 4 || i + ln > n)
      break;
    if (id == 30)
      have = 1;
    if (id == 6 || id == 7) {
      static const u16 snd[] = { 0x5200, 0x5203 }, rcv[] = { 0x5201, 0x5202, 0x5204 };
      const u16 *a = id == 6 ? snd : rcv;
      u32 na = id == 6 ? 2 : 3;
      memcpy(out + o, msg + i, ln);
      u32 add = 0;
      for (u32 j = 0; j < na; j++) {
        u32 dup = 0;
        for (u32 q = 4; q + 1 < ln; q += 2)
          if (be16(msg + i + q) == a[j])
            dup = 1;
        if (!dup) {
          wbe16(out + o + ln + add, a[j]);
          add += 2;
        }
      }
      wbe16(out + o, ln + add);
      o += ln + add;
    } else {
      memcpy(out + o, msg + i, ln);
      o += ln;
    }
    i += ln;
  }
  if (!have)
    o += rg_component(out + o);
  wbe16(out + 2, o);
  return o;
}

/* ---------------- route guidance decode ---------------- */
static struct {
  u32 route_state, maneuver_state, dist, dist_units, rem, rem_units, eta, cur_idx, has_cur, next_idx, has_next;
  char road[128], dest[128], dist_s[32], rem_s[32], src[64];
  struct {
    u32 used, type, dist, side, jtype, nang;
    s32 exit_angle;
    s16 ang[8];
    char road[128], desc[128], exit[16];
  } man[32];
  /* lane guidance: RouteGuidanceUpdate 16 LaneGuidanceIndex, 18 LaneGuidanceShowing; LaneGuidanceInformation (0x5204) sets, by index */
  u32 lane_idx, has_lane_idx, lane_show, has_lane_show;
  struct {
    u32 used, idx, n;
    struct {
      u32 pos, status, na;
      s32 hl;
      s16 ang[4];
    } l[8];
  } lanes[8];
} st;
static void cpstr(char *d, u32 cap, const u8 *s, u32 n) {
  u32 k = 0;
  while (k < n && k + 1 < cap && s[k]) {
    d[k] = (char)s[k];
    k++;
  }
  d[k] = 0;
}
static u32 uint_be(const u8 *p, u32 n) {
  u32 v = 0;
  for (u32 i = 0; i < n && i < 4; i++)
    v = v << 8 | p[n > 4 ? n - 4 + i : i];
  return v;
} /* BE, low 32 bits */
/* The distance in the iPhone's units, from metres (for a maneuver the iPhone sent no distance text for yet; cur_view). Units: 0 km,
   1 mi, 2 m, 3 yd, 4 ft. Integer only (no soft-float in the car's libc): one decimal for km and mi. */
static void fmt_dist(char *o, u32 cap, u32 m, u32 units) {
  if (units == 1 || units == 3 || units == 4) {
    u32 ft = (m * 3281 + 500) / 1000, yd = (m * 1094 + 500) / 1000;
    if (units == 3 && yd < 500)
      snprintf(o, cap, "%u yd", yd < 100 ? (yd + 5) / 10 * 10 : (yd + 25) / 50 * 50);
    else if (units != 3 && ft < 1000)
      snprintf(o, cap, "%u ft", ft < 100 ? (ft + 5) / 10 * 10 : (ft + 25) / 50 * 50);
    else {
      u32 t = (m * 10 + 804) / 1609; /* tenths of a mile, rounded */
      snprintf(o, cap, "%u.%u mi", t / 10, t % 10);
    }
  } else if (m < 1000)
    snprintf(o, cap, "%u m", m < 100 ? (m + 5) / 10 * 10 : (m + 25) / 50 * 50);
  else {
    u32 t = (m + 50) / 100;
    snprintf(o, cap, "%u.%u km", t / 10, t % 10);
  }
}
/* The maneuver to show and its distance (issue #9, F25 X3: "the first direction after a reroute feels missed"). After a reroute the
   iPhone's current list (0x5201 field 13) is [0, 1]: the start marker (type 11, 0 m) and the first real turn; it moves on to [1, 2]
   only later, so the HUD showed the start marker, sometimes twice, instead of the turn. When the current maneuver is the start
   marker, the car is at it (30 m or less) and the iPhone already lists, and has sent, the next one, that one is shown: with its step
   length (0x5202 field 5, from the start marker to it) as the distance, in the iPhone's units, or with the iPhone's own distance when
   the step length is unknown. Test switch keep_start_marker: the start marker as before. Called under st_lock (or from on_rg). */
static u32 cur_view(u32 *dist, char *ds, u32 cap) {
  u32 x = st.cur_idx % 32, nx = st.next_idx % 32;
  *dist = st.dist;
  snprintf(ds, cap, "%s", st.dist_s);
  if (!st.has_next || st.next_idx == st.cur_idx || !st.man[x].used || st.man[x].type != 11 || st.dist > 30 || !st.man[nx].used ||
      st.man[nx].type == 11 || keep_start_marker())
    return st.cur_idx;
  u32 m = st.man[nx].dist;
  if (m) {
    *dist = m;
    fmt_dist(ds, cap, m, st.dist_units);
  }
  static u32 logged = 0xffffffff;
  if (rg_debug_log() && logged != (st.cur_idx << 16 | st.next_idx)) {
    logged = st.cur_idx << 16 | st.next_idx;
    char b[120];
    snprintf(b, sizeof b, "RG start marker %u at %u m: showing maneuver %u (type %u, %u m)\n", st.cur_idx, st.dist, st.next_idx,
             st.man[nx].type, *dist);
    log_text(b);
  }
  return st.next_idx;
}
/* Each session owns a different target: a late watcher cannot erase a new route, and RAM never survives a reboot. */
static int route_file(char *out, unsigned cap) {
  const char *base = getenv("RGHOOK_PPS");
  int n = snprintf(out, cap, "%s.%d", base ? base : "/dev/shmem/carplaymenu-routeguidance", getpid());
  return n > 0 && (unsigned)n < cap;
}
static const char *publish_error;
static int publish_errno;
static void publish_fail(const char *op, const char *path, int error, long result, unsigned expected) {
  if (publish_error == op && publish_errno == error)
    return;
  publish_error = op;
  publish_errno = error;
  char b[420];
  snprintf(b, sizeof b, "RG publish %s failed: path=%s errno=%d result=%ld expected=%u\n", op, path, error, result, expected);
  log_text(b);
}
static int route_link(char *out, unsigned cap) {
  const char *p = getenv("RGHOOK_PPS");
  int n = snprintf(out, cap, "%s.carplaymenu.%d", p ? p : "/var/iap/pps/routeguidance", getpid());
  return n > 0 && (unsigned)n < cap;
}
static int session_claimed;
static volatile int watch_state; /* 0 idle, 1 spawning, 2 ready */
static void publish(void) {
  const char *p = getenv("RGHOOK_PPS");
  if (!p)
    p = "/var/iap/pps/routeguidance";
  static u32 sequence;
  static int reported;
  static char previous[176];
  char base[160], file[176], link[192], tmp[196], b[2048];
  if (!route_file(base, sizeof base) || !route_link(link, sizeof link)) {
    publish_fail("path", p, 0, -1, 0);
    return;
  }
  if (!session_claimed || __atomic_load_n(&watch_state, __ATOMIC_ACQUIRE) != 2)
    return;
  snprintf(file, sizeof file, "%s.%u", base, ++sequence);
  snprintf(tmp, sizeof tmp, "%s.tmp", link);
  u32 cur = st.cur_idx, dist = st.dist;
  char ds[32];
  if (st.has_cur)
    cur = cur_view(&dist, ds, sizeof ds);
  else
    snprintf(ds, sizeof ds, "%s", st.dist_s);
  int k =
      snprintf(b, sizeof b,
               "@routeguidance\nroute_state:n:%u\nmaneuver_state:n:%u\ndist_to_maneuver:n:%u\ndist_to_maneuver_units:n:%u\n"
               "dist_to_maneuver_string::%s\ndistance_remaining:n:%u\ndistance_remaining_string::%s\ncurrent_road::%s\ndestination:"
               ":%s\nsource::%s\n",
               st.route_state, st.maneuver_state, dist, st.dist_units, ds, st.rem, st.rem_s, st.road, st.dest, st.src);
  if (st.has_cur) {
    u32 x = cur % 32;
    k += snprintf(b + k, sizeof b - k,
                  "maneuver_index:n:%u\nmaneuver_type:n:%u\nmaneuver_exit_angle:n:%d\nmaneuver_driving_side:n:%u\nmaneuver_road::%"
                  "s\nmaneuver_description::%s\n",
                  cur, st.man[x].type, st.man[x].exit_angle, st.man[x].side, st.man[x].road, st.man[x].desc);
    /* JunctionType (0 intersection, 1 roundabout) and the angles of the junction's roads, for the side roads on the HUD (hudshapes.h) */
    k += snprintf(b + k, sizeof b - k, "maneuver_junction_type:n:%u\nmaneuver_junction_angles::", st.man[x].jtype);
    for (u32 q = 0; q < st.man[x].nang && k < (int)sizeof b - 16; q++)
      k += snprintf(b + k, sizeof b - k, q ? ",%d" : "%d", (int)st.man[x].ang[q]);
    k += snprintf(b + k, sizeof b - k, "\n");
  }
  /* The lanes the iPhone shows now, left to right: "status/highlight/angle,angle;..." (status 0 not good, 1 good, 2 preferred;
     highlight 1000 = none; angles as the exit angle, 0 straight on, positive to the right). Only while it shows them. */
  if (st.has_lane_idx) {
    k += snprintf(b + k, sizeof b - k, "lane_guidance_index:n:%u\nlane_guidance_showing:n:%u\n", st.lane_idx,
                  st.has_lane_show ? st.lane_show : 1);
    u32 x = st.lane_idx % 8;
    if ((!st.has_lane_show || st.lane_show) && st.lanes[x].used && st.lanes[x].idx == st.lane_idx && st.lanes[x].n) {
      k += snprintf(b + k, sizeof b - k, "lanes::");
      for (u32 q = 0; q < st.lanes[x].n && k < (int)sizeof b - 48; q++) {
        const __typeof__(st.lanes[0].l[0]) *L = &st.lanes[x].l[q];
        k += snprintf(b + k, sizeof b - k, "%s%u/%d/", q ? ";" : "", L->status, (int)L->hl);
        for (u32 a = 0; a < L->na; a++)
          k += snprintf(b + k, sizeof b - k, a ? ",%d" : "%d", (int)L->ang[a]);
      }
      k += snprintf(b + k, sizeof b - k, "\n");
    }
  }
  if (k <= 0 || k >= (int)sizeof b) {
    publish_fail("format", file, 0, k, sizeof b - 1);
    return;
  }
  /* QNX /dev/shmem rename returns EXDEV. Commit a complete, unique RAM object through a disk symlink instead. */
  int fd = r_open(file, 0x301, 0644);
  if (fd < 0) {
    publish_fail("open", file, RG_ERRNO, fd, k);
    return;
  }
  long written = r_write(fd, b, (u32)k);
  int error = written < 0 ? RG_ERRNO : 0;
  int closed = r_close(fd), close_error = closed < 0 ? RG_ERRNO : 0;
  if (written != k || closed != 0) {
    publish_fail(written != k ? "write" : "close", file, written != k ? error : close_error, written != k ? written : closed,
                 written != k ? k : 0);
    r_unlink(file);
    return;
  }
  if (r_unlink(tmp) < 0 && RG_ERRNO != 2 /* ENOENT */) {
    publish_fail("unlink temporary link", tmp, RG_ERRNO, -1, 0);
    r_unlink(file);
    return;
  }
  if (symlink(file, tmp) != 0) {
    publish_fail("symlink session", tmp, RG_ERRNO, -1, 0);
    r_unlink(file);
    return;
  }
  if (r_rename(tmp, link) != 0) {
    publish_fail("rename session", link, RG_ERRNO, -1, 0);
    r_unlink(tmp);
    r_unlink(file);
    return;
  }
  char old[176];
  snprintf(old, sizeof old, "%s", previous);
  snprintf(previous, sizeof previous, "%s", file);
  int cleanup_error = old[0] && r_unlink(old) < 0 ? RG_ERRNO : 0;
  if (cleanup_error && cleanup_error != 2) {
    publish_fail("unlink old snapshot", old, cleanup_error, -1, 0);
    return;
  }
  if (!reported || publish_error) {
    char msg[420];
    snprintf(msg, sizeof msg, "RG publish %s: session=%s snapshot=%s bytes=%d\n", publish_error ? "recovered" : "ready", link, file,
             k);
    log_text(msg);
    reported = 1;
  }
  publish_error = 0;
}
/* LaneGuidanceInformation (0x5204): 1 LaneGuidanceIndex, 2 LaneInformation (one per lane, repeated), 3 description. Inside a
   LaneInformation: 0 position (u16, a new lane), 1 status (u8), 2 angles (s16 each, the TLV repeated or a list), 3 highlight angle (s16,
   1000 = none). Layout: libesoiap2's parseLaneInformation, as read by the community MIB2 implementation (rgd_tlv.c). Kept sorted by
   position, at most 8 lanes and 4 angles each. */
static void on_lanes(u32 idx, const u8 *p, u32 n) {
  u32 x = idx % 8, i = 0, cur = 0xffffffff;
  char b[200];
  int k;
  memset(&st.lanes[x], 0, sizeof st.lanes[x]);
  st.lanes[x].used = 1;
  st.lanes[x].idx = idx;
  while (i + 4 <= n) {
    u32 ln = be16(p + i), id = be16(p + i + 2);
    if (ln < 4 || i + ln > n)
      break;
    if (id == 2) {
      const u8 *g = p + i + 4;
      u32 gn = ln - 4, j = 0;
      cur = 0xffffffff;
      while (j + 4 <= gn) {
        u32 sl = be16(g + j), sid = be16(g + j + 2);
        if (sl < 4 || j + sl > gn)
          break;
        const u8 *d = g + j + 4;
        u32 dn = sl - 4;
        if (sid == 0 || cur == 0xffffffff) {
          if (st.lanes[x].n >= 8)
            break;
          cur = st.lanes[x].n++;
          st.lanes[x].l[cur].hl = 1000;
          st.lanes[x].l[cur].pos = sid == 0 && dn ? uint_be(d, dn) : cur;
        }
        if (sid == 1 && dn)
          st.lanes[x].l[cur].status = d[0];
        else if (sid == 2)
          for (u32 q = 0; q + 2 <= dn && st.lanes[x].l[cur].na < 4; q += 2)
            st.lanes[x].l[cur].ang[st.lanes[x].l[cur].na++] = (s16)be16(d + q);
        else if (sid == 3 && dn >= 2)
          st.lanes[x].l[cur].hl = (s16)be16(d);
        j += sl;
      }
    }
    i += ln;
  }
  for (u32 a = 1; a < st.lanes[x].n; a++)
    for (u32 c = a; c && st.lanes[x].l[c - 1].pos > st.lanes[x].l[c].pos; c--) { /* left to right */
      __typeof__(st.lanes[0].l[0]) t = st.lanes[x].l[c];
      st.lanes[x].l[c] = st.lanes[x].l[c - 1];
      st.lanes[x].l[c - 1] = t;
    }
  k = snprintf(b, sizeof b, "lanes %u:", idx);
  for (u32 q = 0; q < st.lanes[x].n && k < 160; q++) {
    k += snprintf(b + k, sizeof b - k, " %u/%d/", st.lanes[x].l[q].status, (int)st.lanes[x].l[q].hl);
    for (u32 a = 0; a < st.lanes[x].l[q].na; a++)
      k += snprintf(b + k, sizeof b - k, a ? ",%d" : "%d", (int)st.lanes[x].l[q].ang[a]);
  }
  snprintf(b + k, sizeof b - k, "\n");
  if (rg_debug_log())
    log_text(b);
}
/* The exit number in a maneuver description ("Take exit 23B onto I-95": "23B"): the word "exit" followed by a token that starts
   with a digit. "" when there is none ("Take the exit", "exit onto"). */
static void exit_no(const char *d, char *o, u32 cap) {
  o[0] = 0;
  for (u32 i = 0; d[i]; i++) {
    if ((d[i] | 32) != 'e' || (d[i + 1] | 32) != 'x' || (d[i + 2] | 32) != 'i' || (d[i + 3] | 32) != 't')
      continue;
    if (i && (((d[i - 1] | 32) >= 'a' && (d[i - 1] | 32) <= 'z') || (d[i - 1] >= '0' && d[i - 1] <= '9')))
      continue;
    u32 j = i + 4;
    while (d[j] == ' ' || d[j] == '#')
      j++;
    if (d[j] < '0' || d[j] > '9')
      continue;
    u32 k = 0;
    while (((d[j] >= '0' && d[j] <= '9') || ((d[j] | 32) >= 'a' && (d[j] | 32) <= 'z')) && k + 1 < cap)
      o[k++] = d[j++];
    o[k] = 0;
    return;
  }
}
/* ExitInfo text -> the exit number only ("Exit 23B" -> "23B", "23B" -> "23B"); "" when it has no digit */
static void exit_from(char *o, u32 cap, const char *t) {
  exit_no(t, o, cap);
  if (o[0])
    return;
  while (*t == ' ' || *t == '#')
    t++;
  u32 k = 0;
  while (((*t >= '0' && *t <= '9') || ((*t | 32) >= 'a' && (*t | 32) <= 'z')) && k + 1 < cap)
    o[k++] = *t++;
  o[k] = 0;
  if (o[0] < '0' || o[0] > '9')
    o[0] = 0;
}
static void on_rg_locked(u32 mid, const u8 *p, u32 n);
static void kick(void) {
  if (!session_claimed || __atomic_load_n(&watch_state, __ATOMIC_ACQUIRE) != 2)
    return;
#ifdef RGHOOK_KOMBI
  rgk_kick();
#endif
#ifdef RGHOOK_MOST
  rgh_kick();
#endif
}
static void on_rg(u32 mid, const u8 *p, u32 n) {
  if (session_claimed)
    rg_watch_once();
  st_lock();
  on_rg_locked(mid, p, n);
  st_unlock();
  kick();
}
static void on_rg_locked(u32 mid, const u8 *p, u32 n) {
  char b[600];
  int k = snprintf(b, sizeof b, "RG 0x%04X", mid);
  u32 idx = 0xffffffff;
  u32 i = 0;
  while (i + 4 <= n) {
    u32 ln = be16(p + i), id = be16(p + i + 2);
    if (ln < 4 || i + ln > n)
      break;
    const u8 *d = p + i + 4;
    u32 dn = ln - 4;
    int str = dn > 1 && d[dn - 1] == 0;
    for (u32 q = 0; str && q + 1 < dn; q++)
      if (d[q] < 0x20 && d[q] != '\n')
        str = 0;
    if (k < 520) {
      if (str)
        k += snprintf(b + k, sizeof b - k, " %u=\"%s\"", id, (const char *)d);
      else {
        k += snprintf(b + k, sizeof b - k, " %u=", id);
        for (u32 q = 0; q < dn && q < 12 && k < 560; q++)
          k += snprintf(b + k, sizeof b - k, "%02x", d[q]);
      }
    }
    if (mid == 0x5201) {
      switch (id) {
        case 1:
          st.route_state = uint_be(d, dn);
          break;
        case 2:
          st.maneuver_state = uint_be(d, dn);
          break;
        case 3:
          cpstr(st.road, sizeof st.road, d, dn);
          break;
        case 4:
          cpstr(st.dest, sizeof st.dest, d, dn);
          break;
        case 5:
          st.eta = uint_be(d, dn);
          break;
        case 7:
          st.rem = uint_be(d, dn);
          break;
        case 8:
          cpstr(st.rem_s, sizeof st.rem_s, d, dn);
          break;
        case 9:
          st.rem_units = uint_be(d, dn);
          break;
        case 10:
          st.dist = uint_be(d, dn);
          break;
        case 11:
          cpstr(st.dist_s, sizeof st.dist_s, d, dn);
          break;
        case 12:
          st.dist_units = uint_be(d, dn);
          break;
        case 13: /* ManeuverCurrentList: the maneuvers the iPhone shows now, u16 each; the first is the current one */
          if (dn >= 2) {
            st.cur_idx = be16(d);
            st.has_cur = 1;
            st.has_next = dn >= 4;
            st.next_idx = dn >= 4 ? be16(d + 2) : 0;
          }
          break;
        case 19:
          cpstr(st.src, sizeof st.src, d, dn);
          break;
        case 16:
          if (dn >= 1) {
            st.lane_idx = uint_be(d, dn);
            st.has_lane_idx = 1;
          }
          break;
        case 18:
          if (dn >= 1) {
            st.lane_show = uint_be(d, dn) != 0;
            st.has_lane_show = 1;
          }
          break;
      }
    }
    if (mid == 0x5202 || mid == 0x5204) {
      if (id == 1)
        idx = uint_be(d, dn);
    }
    i += ln;
  }
  if (mid == 0x5204 && idx != 0xffffffff)
    on_lanes(idx, p, n);
  if (mid == 0x5202 && idx != 0xffffffff) {
    u32 x = idx % 32;
    memset(&st.man[x], 0, sizeof st.man[x]);
    st.man[x].used = 1;
    i = 0;
    while (i + 4 <= n) {
      u32 ln = be16(p + i), id = be16(p + i + 2);
      if (ln < 4 || i + ln > n)
        break;
      const u8 *d = p + i + 4;
      u32 dn = ln - 4;
      if (id == 2)
        cpstr(st.man[x].desc, sizeof st.man[x].desc, d, dn);
      else if (id == 3)
        st.man[x].type = uint_be(d, dn);
      else if (id == 4)
        cpstr(st.man[x].road, sizeof st.man[x].road, d, dn);
      else if (id == 5)
        st.man[x].dist = uint_be(d, dn);
      else if (id == 11)
        st.man[x].exit_angle = (s16)uint_be(d, dn);
      else if (id == 9)
        st.man[x].jtype = uint_be(d, dn);
      else if (id == 10)
        for (u32 q = 0; q + 2 <= dn && st.man[x].nang < 8; q += 2)
          st.man[x].ang[st.man[x].nang++] = (s16)be16(d + q); /* JunctionElementAngle, one per road */
      else if (id == 8)
        st.man[x].side = uint_be(d, dn); /* DrivingSide: 0 right-hand traffic, 1 left-hand */
      else if (id != 1 && id != 6 && id != 7 && dn >= 2 && dn <= 18 && st.man[x].exit[0] == 0) {
        /* any other text field with a digit in it is taken as the ExitInfo the iPhone sends when SupportsExitInfo was asked for
           ("23B", "Exit 23B"): printable, ends in NUL (binary and number fields do not) */
        u32 q = 0, dig = 0;
        while (q + 1 < dn && d[q] >= 0x20 && d[q] < 0x7f) {
          if (d[q] >= '0' && d[q] <= '9')
            dig = 1;
          q++;
        }
        if (q + 1 == dn && d[q] == 0 && dig)
          exit_from(st.man[x].exit, sizeof st.man[x].exit, (const char *)d);
      }
      i += ln;
    }
  }
  snprintf(b + k, sizeof b - k, "\n");
  if (rg_debug_log())
    log_text(b);
  static u32 logged_state = 0xffffffff; /* without debug_log: only the route's state changes (started, ended) */
  if (mid == 0x5201 && st.route_state != logged_state) {
    logged_state = st.route_state;
    if (!rg_debug_log()) {
      snprintf(b, sizeof b, "RG route_state %u\n", st.route_state);
      log_text(b);
    }
  }
  publish();
}

/* ---------------- link layer ---------------- */
static void fix_packet(u8 *pk, u32 n) {
  wbe16(pk + 2, n);
  pk[8] = checksum(pk, 8);
  if (n > 9)
    pk[n - 1] = checksum(pk + 9, n - 10);
}
static void inject_start(void) {
  u8 m[64], pk[80];
  u32 k = 6;
  k += tlv16(m + k, 0, RG_COMP_ID);
  k += tlv(m + k, 1, 0, 0);
  k += tlv(m + k, 2, 0, 0);
  k += tlv(m + k, 3, 0, 0); /* component, request SourceName, SourceSupportsRouteGuidance, SupportsExitInfo */
  m[0] = 0x40;
  m[1] = 0x40;
  wbe16(m + 2, k);
  wbe16(m + 4, 0x5200);
  pk[0] = 0xff;
  pk[1] = 0x5a;
  pk[4] = ACK;
  pk[5] = (u8)(last_out_seq + 1);
  pk[6] = (u8)last_in_seq;
  pk[7] = (u8)ctl_sess;
  memcpy(pk + 9, m, k);
  fix_packet(pk, 9 + k + 1);
  r_write(transport_fd, pk, 9 + k + 1);
  last_out_seq = (last_out_seq + 1) & 0xff;
  delta = (delta + 1) & 0xff;
  injected = 1;
  char b[96];
  snprintf(b, sizeof b, "inject StartRouteGuidanceUpdates seq=%u (delta now %u)\n", last_out_seq, delta);
  log_text(b);
}

static void log_mid(const char *dir, u32 mid, u32 ml);
static int ident_ok, auth_ok; /* see packet_done */
static u8 out_buf[70000];
long write(int fd, const void *pv, size_t n) {
  init();
  const u8 *p = (const u8 *)pv;
  if (transport_fd < 0 && getenv("RGHOOK_DEV") == 0 && n >= 2 && p[0] == 0xff && (p[1] == 0x5a || p[1] == 0x55)) {
    transport_fd = fd;
    char b[64];
    snprintf(b, sizeof b, "transport fd %d\n", fd);
    log_text(b);
  }
  if (fd != transport_fd || n > sizeof out_buf - 2048)
    return r_write(fd, pv, n);
  lock();
  u32 i = 0, o = 0;
  while (i < n) {
    if (i + 9 <= n && p[i] == 0xff && p[i + 1] == 0x5a && checksum(p + i, 8) == p[i + 8]) {
      u32 ln = be16(p + i + 2);
      if (ln < 9 || i + ln > n) {
        memcpy(out_buf + o, p + i, n - i);
        o += n - i;
        break;
      }
      u8 *pk = out_buf + o;
      memcpy(pk, p + i, ln);
      u32 nl = ln;
      if (pk[4] & SYN) {
        delta = 0;
        injected = 0;
        ctl_sess = -1;
        ident_ok = auth_ok = 0;
        for (u32 q = 9 + 10; q + 3 <= ln - 1; q += 3)
          if (pk[q + 1] == 0 && ctl_sess < 0)
            ctl_sess = pk[q];
        char b[64];
        snprintf(b, sizeof b, "link SYN, control session %d\n", ctl_sess);
        log_text(b);
      } else {
        if (ln > 9 + 6 && (int)pk[7] == ctl_sess && pk[9] == 0x40 && pk[10] == 0x40 && be16(pk + 13) == 0x1D01) {
          static u8 m[8192];
          u32 ml = be16(pk + 11);
          if (ml > ln - 10)
            ml = ln - 10;
          u32 nm = patch_ident(pk + 9, ml, m);
          memcpy(pk + 9, m, nm);
          nl = 9 + nm + 1;
          char b[96];
          snprintf(b, sizeof b, "IdentificationInformation patched %u -> %u bytes\n", ml, nm);
          log_text(b);
        }
        if (ln > 9 + 6 && (int)pk[7] == ctl_sess && pk[9] == 0x40 && pk[10] == 0x40)
          log_mid("out", be16(pk + 13), be16(pk + 11));
        pk[5] = (u8)(pk[5] + delta);
        if (nl > 9)
          last_out_seq = pk[5];
        fix_packet(pk, nl);
      }
      o += nl;
      i += ln;
    } else {
      out_buf[o++] = p[i++];
    }
  }
  long r = r_write(fd, out_buf, o);
  unlock();
  return r < 0 ? r : (long)n;
}

/* Incoming bytes go to the driver in the same read() (its BT transport waits for the device's "data available" notification:
   bytes kept here would only be delivered with the next packet). Packet headers are rewritten in place; only a header split
   across reads is held_buf back (at most 8 bytes, the rest of that packet is on its way), so a header is always whole when rewritten.
   Payload bytes are copied aside (pkt_buf) to decode the control message once the packet is complete. */
static u8 held_buf[8192], pkt_buf[70000], hdr[9];
static u32 held_len, pkt_left, pkt_buf_len, pkt_len;
static int pkt_ctl;
/* The 2017 driver leaks the receive buffer of every message it does not know ("Invalid packet"): after ~20 route guidance
   messages it stops reading the link. Route guidance packets are therefore decoded here and handed to the driver as a minimal
   message it subscribed to and handles normally (default: an empty PowerUpdate 0xAE01; $RGHOOK_SUBST, hex, changes it).
   Same sequence number, so the driver acknowledges it and the link numbering is untouched. */
static u32 subst_id(void) {
  static u32 v;
  if (!v) {
    const char *s = getenv("RGHOOK_SUBST");
    v = 0xAE01;
    if (s) {
      u32 x = 0;
      for (; *s; s++) {
        int c = *s;
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
          break;
        x = x << 4 | (u32)d;
      }
      if (x)
        v = x;
    }
  }
  return v;
}
static int is_rg(u32 mid) {
  return mid == 0x5201 || mid == 0x5202 || mid == 0x5204;
}
static u32 nlogged; /* with debug_log, control message ids in both directions go to the log, the first 400 (debugging a phone) */
static void log_mid(const char *dir, u32 mid, u32 ml) {
  if (nlogged >= 400 || !rg_debug_log())
    return;
  nlogged++;
  char b[48];
  snprintf(b, sizeof b, "%s %04x len %u\n", dir, mid, ml);
  log_text(b);
}
/* StartRouteGuidanceUpdates waits for both IdentificationAccepted (0x1D02) and AuthenticationSucceeded (0xAA05): a real iPhone (2026-11-11,
   MINI F54, wireless) identifies first and authenticates afterwards, and ignored the request sent between the two (no reply at all). */
static void packet_done(void) {
  int accepted = 0;
  if (pkt_ctl && pkt_buf_len >= 7 && pkt_buf[0] == 0x40 && pkt_buf[1] == 0x40) {
    u32 mid = be16(pkt_buf + 4), ml = be16(pkt_buf + 2);
    if (ml > pkt_buf_len - 1)
      ml = pkt_buf_len - 1;
    log_mid("in ", mid, ml);
    if (mid == 0x1D02) {
      ident_ok = 1;
      log_text("IdentificationAccepted\n");
    } else if (mid == 0xAA05) {
      auth_ok = 1;
      log_text("AuthenticationSucceeded\n");
    } else if (mid == 0xAA06)
      log_text("AuthenticationFailed\n");
    else if (mid == 0x1D03)
      log_text("IdentificationRejected\n");
    else if ((mid == 0x5201 || mid == 0x5202 || mid == 0x5204) && ml >= 6)
      on_rg(mid, pkt_buf + 6, ml - 6);
  }
  accepted = ident_ok && auth_ok;
  if (accepted && !injected) {
    lock();
    inject_start();
    unlock();
#ifdef RGHOOK_MOST
    /* The MOST client starts with the CarPlay session, not with the first route message: its loop also takes the HUD test channel
       (hud-probe, tools/hud_probe.sh) and "HUD debug log", which in a HUD-only install had no client while no route guided. */
    if (session_claimed && rg_watch_once())
      rgh_kick();
#endif
  }
}
static u32
scan_in(u8 *b, u32 n) { /* returns how many bytes to hand to the driver (the rest is an incomplete packet start kept in held_buf) */
  u32 i = 0;
  while (i < n) {
    if (pkt_left) {
      u32 m = n - i < pkt_left ? n - i : pkt_left;
      if (pkt_buf_len + m <= sizeof pkt_buf) {
        memcpy(pkt_buf + pkt_buf_len, b + i, m);
        pkt_buf_len += m;
      }
      pkt_left -= m;
      i += m;
      if (!pkt_left)
        packet_done();
      continue;
    }
    if (b[i] != 0xff) {
      i++;
      continue;
    }
    if (i + 1 >= n || (b[i + 1] == 0x5a && i + 9 > n)) {
      u32 k = n - i;
      if (k <= sizeof held_buf) {
        memcpy(held_buf, b + i, k);
        held_len = k;
        return i;
      }
      i++;
      continue;
    }
    if (b[i + 1] != 0x5a || checksum(b + i, 8) != b[i + 8] || be16(b + i + 2) < 9) {
      i++;
      continue;
    }
    u8 *pk = b + i;
    u32 ln = be16(pk + 2);
    int ctl = (int)pk[7] == ctl_sess && !(pk[4] & SYN) && ln > 9;
    if (ctl && ln >= 9 + 6 + 1) {
      if (i + 15 > n) {
        u32 k = n - i;
        if (k <= sizeof held_buf) {
          memcpy(held_buf, b + i, k);
          held_len = k;
          return i;
        }
      } /* need the message id */
      else if (pk[9] == 0x40 && pk[10] == 0x40 && is_rg(be16(pk + 13))) {
        if (i + ln > n) {
          u32 k = n - i;
          if (k <= sizeof held_buf) {
            memcpy(held_buf, b + i, k);
            held_len = k;
            return i;
          }
        } /* whole packet first (small) */
        else {
          u32 mid = be16(pk + 13), ml = be16(pk + 11);
          if (ml > ln - 10)
            ml = ln - 10;
          if (ml >= 6)
            on_rg(mid, pk + 15, ml - 6);
          if (delta)
            pk[6] = (u8)(pk[6] - delta);
          last_in_seq = pk[5];
          wbe16(pk + 9 + 2, 6);
          wbe16(pk + 9 + 4, subst_id());
          fix_packet(pk, 16); /* -> 40 40 00 06 <subst>, 16-byte packet */
          memmove(pk + 16, pk + ln, n - (i + ln));
          n -= ln - 16;
          i += 16;
          continue;
        }
      }
    }
    if (!(pk[4] & SYN)) {
      if (delta) {
        pk[6] = (u8)(pk[6] - delta);
        pk[8] = checksum(pk, 8);
      } /* acknowledgement of our renumbered packets -> driver's numbering */
      if (ln > 9)
        last_in_seq = pk[5];
    }
    pkt_ctl = ctl;
    pkt_buf_len = 0;
    pkt_left = ln > 9 ? ln - 9 : 0;
    memcpy(hdr, pk, 9);
    i += 9;
  }
  return n;
}
/* Select the owner at initialization, before guidance can arrive in either overlapping session.
   close() is not hooked: mm-ipod calls it before libc can be looked up (it crashed). */
static void rg_clear(void) {
  const char *p = getenv("RGHOOK_PPS");
  session_claimed = 0;
  st_lock();
  memset(&st, 0, sizeof st);
  st_unlock();
  p = p ? p : "/var/iap/pps/routeguidance";
  char link[192], tmp[200];
  if (!route_link(link, sizeof link)) {
    publish_fail("claim path", p, 0, -1, 0);
    return;
  }
  snprintf(tmp, sizeof tmp, "%s.claim", link);
  if (r_unlink(link) < 0 && RG_ERRNO != 2 /* ENOENT */) {
    publish_fail("unlink session", link, RG_ERRNO, -1, 0);
    return;
  }
  if (r_unlink(tmp) < 0 && RG_ERRNO != 2 /* ENOENT */) {
    publish_fail("unlink claim", tmp, RG_ERRNO, -1, 0);
    return;
  }
  if (symlink(link, tmp) != 0) {
    publish_fail("symlink claim", tmp, RG_ERRNO, -1, 0);
    return;
  }
  if (r_rename(tmp, p) != 0) {
    publish_fail("rename claim", p, RG_ERRNO, -1, 0);
    r_unlink(tmp);
    return;
  }
  /* Never retry a failed claim on a later message: an older session could steal a newer owner's alias. */
  session_claimed = 1;
}

/* ---------------- for the KOMBI sender (rgview.h) ---------------- */
static void copy_z(char *d, u32 cap, const char *s) {
  u32 k = 0;
  while (s[k] && k + 1 < cap) {
    d[k] = s[k];
    k++;
  }
  d[k] = 0;
}
int rg_view_get(struct rg_view *v) {
  init();
  memset(v, 0, sizeof *v);
  st_lock();
  int have = st.has_cur;
  u32 rs = st.route_state;
  v->hold = rs == 3 || rs == 4;
  if (have) {
    u32 dist, cur = cur_view(&dist, v->dist_s, sizeof v->dist_s), x = cur % 32; /* the start marker skipped as in publish() */
    v->active = rs == 1 || rs == 5 || rs == 6;
    v->index = cur;
    v->type = st.man[x].type;
    v->side = st.man[x].side;
    v->angle = st.man[x].exit_angle;
    if (st.man[x].exit[0])
      copy_z(v->exitno, sizeof v->exitno, st.man[x].exit);
    else
      exit_no(st.man[x].desc, v->exitno, sizeof v->exitno);
    v->roadfb = st.man[x].road[0] ? 0 : 1;
    v->dist = dist;
    v->units = st.dist_units;
    copy_z(v->road, sizeof v->road, st.man[x].road[0] ? st.man[x].road : st.road);
    if (st.has_lane_idx) { /* the lanes the iPhone shows now, as publish() writes them */
      u32 l = st.lane_idx % 8;
      if ((!st.has_lane_show || st.lane_show) && st.lanes[l].used && st.lanes[l].idx == st.lane_idx)
        for (u32 q = 0; q < st.lanes[l].n && q < 8; q++) {
          struct rg_lane *L = &v->lanes[v->nl++];
          L->status = st.lanes[l].l[q].status;
          L->hl = st.lanes[l].l[q].hl;
          L->na = st.lanes[l].l[q].na;
          for (u32 a = 0; a < L->na && a < 4; a++)
            L->ang[a] = st.lanes[l].l[q].ang[a];
        }
    }
  }
  st_unlock();
  return have;
}
int rg_file_read(const char *path, char *buf, unsigned cap) {
  init();
  int fd = r_open(path, 0);
  if (fd < 0)
    return -1;
  long n = r_read(fd, buf, cap ? cap - 1 : 0);
  r_close(fd);
  if (n < 0)
    n = 0;
  if (cap)
    buf[n] = 0;
  return (int)n;
}
int rg_file_write(const char *path, const char *text) {
  init();
  int fd = r_open(path, 0x301, 0644);
  if (fd < 0)
    return -1;
  r_write(fd, text, strlen(text));
  r_close(fd);
  return 0;
}
void rg_file_unlink(const char *path) {
  init();
  r_unlink(path);
}
long rg_fd_read(int fd, void *buf, unsigned n) {
  init();
  return r_read(fd, buf, n);
}
/* The watcher (ksh, P_NOWAITO so it outlives mm-ipod; mm-ipod lives for one CarPlay session): polls our pid every 10 s, then has
   the route file removed; drawing variants also have libcpprobe.so clear their marked arrows. RGHOOK_NO_WATCH: none (tests). */
extern int spawnle(int, const char *, const char *, ...);
int rg_watch_once(void) {
  int expected = 0;
  if (!__atomic_compare_exchange_n(&watch_state, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    return expected == 2;
  if (getenv("RGHOOK_NO_WATCH")) {
    __atomic_store_n(&watch_state, 2, __ATOMIC_RELEASE);
    return 1;
  }
  char file[160], link[192], pid[16];
  if (!route_file(file, sizeof file) || !route_link(link, sizeof link)) {
    __atomic_store_n(&watch_state, 0, __ATOMIC_RELEASE);
    return 0;
  }
  snprintf(pid, sizeof pid, "%d", getpid());
  static const char sc[] =
      "while kill -0 \"$1\" 2>/dev/null; do sleep 10; done; rm -f \"$2\" \"$2\".* \"$3\" \"$3.tmp\""
#if defined(RGHOOK_KOMBI) || defined(RGHOOK_MOST)
      "; [ -f \"$4\" ] && CPM_CLEAR=\"$1\" CPM_KOMBI_MARKER=\"$5\" LD_PRELOAD=\"$4\" /bin/cat < /dev/null > /dev/null 2>&1"
#endif
      ;
  static const char lib[] = "/var/opt/hmi/data/rw/Plugins/CarPlayMenu/libcpprobe.so";
  const char *mk = getenv("RGHOOK_MARKER");
  static char *const envp[] = { (char *)"PATH=/bin:/usr/bin:/proc/boot", 0 };
  char b[96];
  if (spawnle(3 /* P_NOWAITO */, "/bin/ksh", "ksh", "-c", sc, "rghook-watch", pid, file, link, lib,
              mk && *mk ? mk : RG_KOMBI_MARKER, (char *)0, envp) == -1) {
    __atomic_store_n(&watch_state, 0, __ATOMIC_RELEASE);
    log_text("watcher not started: route cleanup will retry on the next guidance message\n");
    return 0;
  }
  snprintf(b, sizeof b, "watcher started for pid %s (clears its route when mm-ipod ends)\n", pid);
  log_text(b);
  __atomic_store_n(&watch_state, 2, __ATOMIC_RELEASE);
  return 1;
}
void rg_log_line(const char *s) {
  char b[300];
  snprintf(b, sizeof b, "%s\n", s);
  log_text(b);
}
long read(int fd, void *buf, size_t n) {
  init();
  if (fd != transport_fd || transport_fd < 0)
    return r_read(fd, buf, n);
  u8 *b = (u8 *)buf;
  for (;;) {
    u32 h = held_len;
    if (h >= n)
      h = 0; /* held_buf bytes go first (n is the driver's buffer, far larger than 8) */
    if (h) {
      memcpy(b, held_buf, h);
      held_len = 0;
    }
    long k = r_read(fd, b + h, n - h);
    if (k <= 0) {
      if (h) {
        memcpy(held_buf, b, h);
        held_len = h;
      }
      return k;
    } /* no new data (timeout/EAGAIN): say so, keep the held_buf bytes */
    u32 out = scan_in(b, h + (u32)k);
    if (out)
      return (long)out; /* nothing but a partial header so far: its rest is on the way, wait for it */
  }
}
