/* rgview.h — the iPhone's current route guidance as librghook.so (src/rghook.c, C) hands it to its KOMBI sender
   (src/rgkombi.cpp, kombi.h): the same fields hud.h reads from the routeguidance file, taken under rghook's lock. */
#ifndef RGVIEW_H
#define RGVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

struct rg_lane { unsigned status, na; int hl, ang[4]; };   /* status 0 not good, 1 good, 2 preferred; hl 1000 = none */
struct rg_view {
  int active, hold;              /* a maneuver on an active route (route state 1, 5, 6); Loading / Locating (3, 4): keep the last */
  unsigned index, type, side;    /* maneuver index, iAP2 maneuver type, driving side (1 = left-hand traffic) */
  int angle;                     /* exit angle, positive = right */
  unsigned dist, units;          /* metres to the maneuver, iAP2 units of the iPhone's text (0 km, 1 mi, 2 m, 3 yd, 4 ft) */
  char dist_s[32], road[128];    /* the iPhone's distance text ("450 m"), the maneuver's road (else the current road) */
  unsigned char roadfb;          /* 1 = road is the current road (the maneuver names none): it changes as the car drives */
  char exitno[16];               /* the maneuver's exit number (ExitInfo, else "Take exit 23B" -> "23B"), "" = none */
  unsigned nl;                   /* lanes the iPhone shows now, left to right */
  struct rg_lane lanes[8];
};

int rg_view_get(struct rg_view *v);                        /* rghook.c: 1 = guidance state filled in, 0 = none yet */
int rg_file_read(const char *path, char *buf, unsigned cap); /* rghook.c: bytes read (NUL-terminated), -1 = no file */
int rg_file_write(const char *path, const char *text);     /* rghook.c: 0 ok */
void rg_file_unlink(const char *path);
long rg_fd_read(int fd, void *buf, unsigned n);            /* rghook.c: libc's read() (this library's own read() is the hook) */
void rg_log_line(const char *s);                           /* rghook.c: one line to rghook.log */
int rg_debug_log(void);                                    /* rghook.c: 1 = switch debug_log set (per-event lines), read once */
void rgk_kick(void);                                       /* rgkombi.cpp: guidance arrived; starts the KOMBI worker once */
void rgh_kick(void);                                       /* rghud.cpp: guidance arrived; starts the MOST HUD drawing once */
/* rghook.c: once per mm-ipod, a watcher removes its session's route file when its pid ends. Drawing variants also run libcpprobe.so
   with CPM_CLEAR=<pid> to clear arrows whose markers still name that pid. */
int rg_watch_once(void);                                   /* 1 only after successful spawn; 0 = do not publish/draw yet */
#define RG_KOMBI_MARKER "/dev/shmem/carplaymenu-kombi.shown"
#define RG_MOST_MARKER "/dev/shmem/carplaymenu-most.shown"

#ifdef __cplusplus
}
#endif

#endif
