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
/* Switch debug_log in the plugin's settings folder ($RGHOOK_CFG in tests; /var may not be linked yet when mm-ipod starts): the
   per-message lines (every route guidance message, the lanes, the control message ids) only with it. Read once. */
int rg_debug_log(void) {
  static volatile int on = -1;
  if (on < 0) {
    init();
    const char *cfg = getenv("RGHOOK_CFG");
    int v = 0;
    for (int i = 0; i < 3 && !v; i++) {
      const char *d = i == 0 ? cfg : i == 1 ? "/var/opt/hmi/data/rw/carplaymenu" : "/fs/sda1/opt/hmi/data/ID5/carplaymenu";
      if (!d || !*d)
        continue;
      char p[192];
      snprintf(p, sizeof p, "%s/debug_log", d);
      int fd = r_open(p, 0);
      if (fd >= 0) {
        r_close(fd);
        v = 1;
      }
    }
    on = v;
  }
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
  u32 route_state, maneuver_state, dist, dist_units, rem, rem_units, eta, cur_idx, has_cur;
  char road[128], dest[128], dist_s[32], rem_s[32], src[64];
  struct {
    u32 used, type, dist, side, jtype, nang;
    s32 exit_angle;
    s16 ang[8];
    char road[128], desc[128];
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
static void publish(void) {
  const char *p = getenv("RGHOOK_PPS");
  char tmp[160], b[2048];
  if (!p)
    p = "/var/iap/pps/routeguidance";
  int k =
      snprintf(b, sizeof b,
               "@routeguidance\nroute_state:n:%u\nmaneuver_state:n:%u\ndist_to_maneuver:n:%u\ndist_to_maneuver_units:n:%u\n"
               "dist_to_maneuver_string::%s\ndistance_remaining:n:%u\ndistance_remaining_string::%s\ncurrent_road::%s\ndestination:"
               ":%s\nsource::%s\n",
               st.route_state, st.maneuver_state, st.dist, st.dist_units, st.dist_s, st.rem, st.rem_s, st.road, st.dest, st.src);
  if (st.has_cur) {
    u32 x = st.cur_idx % 32;
    k += snprintf(b + k, sizeof b - k,
                  "maneuver_index:n:%u\nmaneuver_type:n:%u\nmaneuver_exit_angle:n:%d\nmaneuver_driving_side:n:%u\nmaneuver_road::%"
                  "s\nmaneuver_description::%s\n",
                  st.cur_idx, st.man[x].type, st.man[x].exit_angle, st.man[x].side, st.man[x].road, st.man[x].desc);
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
  snprintf(tmp, sizeof tmp, "%s.tmp", p);
  int fd = r_open(tmp, 0x301, 0644);
  if (fd < 0)
    return;
  r_write(fd, b, (u32)k);
  r_close(fd);
  r_rename(tmp, p);
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
static void on_rg_locked(u32 mid, const u8 *p, u32 n);
static void kick(void) {
#ifdef RGHOOK_KOMBI
  rgk_kick();
#endif
#ifdef RGHOOK_MOST
  rgh_kick();
#endif
}
static void on_rg(u32 mid, const u8 *p, u32 n) {
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
        case 13:
          if (dn >= 2) {
            st.cur_idx = be16(d);
            st.has_cur = 1;
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
  }
}
static u32 scan_in(u8 *b,
                   u32 n) { /* returns how many bytes to hand to the driver (the rest is an incomplete packet start kept in held_buf) */
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
/* The route guidance state belongs to one iPhone session: a new mm-ipod (Connectivity restarts it for every session) starts without
   the previous file, so the HUD (src/hud.h) never keeps an old route; within a session the iPhone ends a route itself
   (RouteGuidanceUpdate state 2 Arrived / 0 NoRouteSet). close() is not hooked: mm-ipod calls it while the libraries are still loading,
   before libc can be looked up (it crashed). */
static void rg_clear(void) {
  const char *p = getenv("RGHOOK_PPS");
  st_lock();
  memset(&st, 0, sizeof st);
  st_unlock();
  r_unlink(p ? p : "/var/iap/pps/routeguidance");
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
    u32 x = st.cur_idx % 32;
    v->active = rs == 1 || rs == 5 || rs == 6;
    v->index = st.cur_idx;
    v->type = st.man[x].type;
    v->side = st.man[x].side;
    v->angle = st.man[x].exit_angle;
    v->dist = st.dist;
    v->units = st.dist_units;
    copy_z(v->dist_s, sizeof v->dist_s, st.dist_s);
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
#if defined(RGHOOK_KOMBI) || defined(RGHOOK_MOST)
/* The watcher (ksh, P_NOWAITO so it outlives mm-ipod; mm-ipod lives for one CarPlay session): polls our pid every 3 s, then has
   libcpprobe.so clear what a marker still names us for. RGHOOK_NO_WATCH: none (tests). */
extern int spawnle(int, const char *, const char *, ...);
extern int getpid(void);
void rg_watch_once(void) {
  static volatile int done;
  if (__atomic_exchange_n(&done, 1, __ATOMIC_ACQ_REL))
    return;
  if (getenv("RGHOOK_NO_WATCH"))
    return;
  static const char lib[] = "/var/opt/hmi/data/rw/Plugins/CarPlayMenu/libcpprobe.so";
  const char *mk = getenv("RGHOOK_MARKER");
  char sc[512];
  int pid = getpid();
  snprintf(sc, sizeof sc,
           "while kill -0 %d 2>/dev/null; do sleep 3; done; [ -f %s ] && CPM_CLEAR=%d CPM_KOMBI_MARKER=%s LD_PRELOAD=%s /bin/cat"
           " < /dev/null > /dev/null 2>&1",
           pid, lib, pid, mk && *mk ? mk : RG_KOMBI_MARKER, lib);
  static char *const envp[] = {(char *)"PATH=/bin:/usr/bin:/proc/boot", 0};
  char b[96];
  if (spawnle(3 /* P_NOWAITO */, "/bin/ksh", "ksh", "-c", sc, (char *)0, envp) == -1) {
    log_text("watcher not started: an arrow left when mm-ipod ends stays until the next session\n");
    return;
  }
  snprintf(b, sizeof b, "watcher started for pid %d (clears an arrow left when mm-ipod ends)\n", pid);
  log_text(b);
}
#endif
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
