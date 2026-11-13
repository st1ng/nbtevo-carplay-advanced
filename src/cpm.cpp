// libCarPlayMenu.so — native backend of the "CarPlay Advanced Settings" menu plugin (iDrive 5/6, HMI 006_017_006 and ID5_1703-490 Build 139).
// A native HMI plugin app, like libActiveSeating.so / libSecurityMenu.so.
//   pluginAppInfo = { u16 1, u16 0, name, vendor, u32 rhmiVersion 1, create, destroy }
//   create(IDispatcher&, const char* name, const Bundle& params, const Bundle& config) -> IPluginApp*
//   builds a bmw::PluginApp with the framework's constructor and gives it a copy of the vtable with onInit replaced,
//   to register the menu handlers (CarPlayMenu.xml). The entry button and the submenu come from the DescFile.
//   ABI mapped by the NBT-AAD project (github.com/Sq00ky/Supra-iDrive6-Android-Auto).
//
// Menu (CarPlayMenu.xml; raAction / raBoolModel; texts in texts.py):
//   CarPlay Advanced Settings
//     5 / 12   Full-screen CarPlay          MINI layout with a 1180 px CarPlay slot (pbwpatch.h), ln -sP, and the reads of the
//                                           CARPLAY_FULLSCREEN flag in CCarPlayActivity forced on (kFsCode): no coding needed
//                                           (BMW: the HMI's common layout, the whole screen; brandMini)
//     8 / 19   CarPlay directions in HUD    CarPlay maneuver (librghook) drawn on the cluster's HUDTerminal over MOST (hud.h)
//                                           (on an Ethernet KOMBI, G-series / J29: sent by librghook-kombi.so itself, src/rgkombi.cpp)
//     HUD effects
//       84 / 85  Fill toward the turn       hud_fill (hud.h): the maneuver fills with the effect colour over the last 300 m
//       88 / 89  Blink near the turn        hud_blink: it blinks route colour <-> effect colour in the last fifth
//       Effect colour                       96-98 / 99-101: Orange / Grey / White = hud_colour 5 / 3 / 4 (F54 palette at night)
//     iDrive control
//       6 / 17   Enable knob tilts          carplay_tilt patch (src/tilt_006.json) in the HMI's memory
//       Touchpad
//         7 / 18   Enable touchpad          CarPlay's own touchpad (Connectivity's carplay.ini) + the hook on CCarPlayActivity::
//                                           handleTouchInput that the three below need; one finger goes to CarPlay's touchpad
//         46 / 47  Tap touchpad as click    a short one-finger tap = knob press (CarPlay's touchpad has no select)
//         Touchpad sensitivity              52-54 / 55-57: Low / Medium / High = touchpad_mm 100 / 175 / 250 (how far a slide moves)
//         30 / 32  Two fingers tap to back  a short two-finger tap = back, two-finger slides = tilts
//       61 / 62  Enable rotation as zoom    knob rotation = a pinch on a hidden two-finger CarPlay touchscreen (libcppinch.so, pinch.c)
//     9        Restart to apply             restart.sh: makes the links again and restarts the HMI and the CarPlay drivers / Connectivity
// Test switches without a menu row (files in kCfgDir): media_gestures (two fingers: volume over MOST, hud.h, and track, iAP2PlaybackControl;
// only with "Enable touchpad" and without "Two fingers"), touchpad_tilts (one finger = tilts every kStep, as before 2026-11-12).
// The options are stored in kCfgDir (they survive an HMI restart). Addresses: per HmiMain3D build (kBuilds); every word is checked before
// it is written, so another HMI version only logs the error and changes nothing.
extern "C" {
void *malloc(__SIZE_TYPE__);
void free(void *);
void *memcpy(void *, const void *, __SIZE_TYPE__);
void *memset(void *, int, __SIZE_TYPE__);
int open(const char *, int, ...);
long write(int, const void *, unsigned long);
int close(int);
int unlink(const char *);
int mkdir(const char *, unsigned);
void *mmap(void *, unsigned long, int, int, int, long);
int munmap(void *, unsigned long);
int msync(void *, unsigned long, int);
int getpid(void);
int kill(int, int);
int snprintf(char *, unsigned long, const char *, ...);
long read(int, void *, unsigned long);
int pathmgr_symlink(const char *path, const char *symlink);
int pathmgr_unlink(const char *path);
int chmod(const char *, unsigned);
int spawnl(int, const char *, const char *, ...);
char *getenv(const char *);
}
#define O_WRONLY 1
#define O_CREAT  0x100
#define O_APPEND 8
#define O_TRUNC  0x200

inline void *operator new(__SIZE_TYPE__, void *p) {
  return p;
}  // placement new
// clang calls these EABI helpers for memsets of aligned blocks; QNX libc.so.3 does not export them (the HMI refuses the plugin:
// "Unresolved symbols"). Local to the plugin.
extern "C" __attribute__((visibility("hidden"))) void __aeabi_memclr8(void *p, __SIZE_TYPE__ n) {
  memset(p, 0, n);
}
extern "C" __attribute__((visibility("hidden"))) void __aeabi_memclr4(void *p, __SIZE_TYPE__ n) {
  memset(p, 0, n);
}
extern "C" __attribute__((visibility("hidden"))) void __aeabi_memclr(void *p, __SIZE_TYPE__ n) {
  memset(p, 0, n);
}
extern "C" __attribute__((visibility("hidden"))) void __aeabi_memcpy8(void *d, const void *s, __SIZE_TYPE__ n) {
  memcpy(d, s, n);
}
extern "C" __attribute__((visibility("hidden"))) void __aeabi_memcpy4(void *d, const void *s, __SIZE_TYPE__ n) {
  memcpy(d, s, n);
}
extern "C" __attribute__((visibility("hidden"))) void __aeabi_memcpy(void *d, const void *s, __SIZE_TYPE__ n) {
  memcpy(d, s, n);
}

namespace bmw {
class IDispatcher;
class Bundle;
class Parcel;
template <typename T>
class BasicString {
public:
  BasicString(const T *s);
  ~BasicString();
  T *m_data;
};
enum RHMIInterfaceVersion {
  RHMI_INTERFACE_VERSION_1 = 1
};
// Delegate<bool, const Parcel&, void, void, void> -> the addHandler handler (object + stub)
template <typename R, typename A1, typename A2, typename A3, typename A4>
struct Delegate {
  void *object;
  R (*stub)(void *, A1);
};
typedef Delegate<bool, const Parcel &, void, void, void> ActionDelegate;
class PluginApp {
public:
  PluginApp(IDispatcher &disp, BasicString<char> name, unsigned major, unsigned minor, RHMIInterfaceVersion rhmi,
            const Bundle &params, const Bundle &config);              // _ZN3bmw9PluginAppC1E...
  void addHandler(unsigned actionId, const ActionDelegate &handler);  // _ZN3bmw9PluginApp10addHandlerEjRK...
  void updateModel(unsigned modelId, bool value);                     // _ZN3bmw9PluginApp11updateModelEjb
  bool onInit();                                                      // _ZN3bmw9PluginApp6onInitEv (base)
  unsigned char m_storage[0x200];
};
}  // namespace bmw

namespace {

typedef unsigned u32;
typedef unsigned short u16;
typedef unsigned char u8;

const char *const kLog = "/dev/shmem/carplaymenu.log";
// "Enable fullscreen": the MINI iDrive 5 layout with the wide CarPlay slot (pbwpatch.h), written to kCfgDir and linked over the original
// with a path-manager link (ln -sP). The HMI reads the layouts when it starts: applies at the next HMI start.
// The HMI reads /opt/hmi/data/ro/... (-ro=, starter.cfg) and /opt/hmi/data is a launcher link to the ID5 folder: boot.sh links both
// paths, and so does the plugin. Both links are removed before the original is read (with only one, it read its own copy, did not
// recognise it and deleted it: fullscreen worked every other start).
const char *const kSsmPaths[2] = { "/opt/hmi/data/ro/mini/id5/widget/generated/Spec_State_Merged.pbw",
                                   "/opt/hmi/ID5/data/ro/mini/id5/widget/generated/Spec_State_Merged.pbw" };
const char *const kSsmFs = "/var/opt/hmi/data/rw/carplaymenu/Spec_State_Merged.pbw";
// BMW (and BMW i, Rolls-Royce): their brand packages have no CarPlay state for iDrive 5, the HMI's common layout has it (pbwpatch.h,
// "BMW"). Its patched copy has a name of its own, so boot.sh links each file over its own two paths.
const char *const kSsmCommonPaths[2] = { "/opt/hmi/data/ro/common/widget/generated/Spec_State_Merged.pbw",
                                         "/opt/hmi/ID5/data/ro/common/widget/generated/Spec_State_Merged.pbw" };
const char *const kSsmCommonFs = "/var/opt/hmi/data/rw/carplaymenu/Spec_State_Merged-common.pbw";
const char *const kCfgDir = "/var/opt/hmi/data/rw/carplaymenu";  // one file per option, present = on
const char *const kRestart = "/dev/shmem/carplaymenu-restart";   // restart request (when restart.sh cannot run)

void logLine(const char *s) {
  int n = 0;
  while (s[n] && n < 240)
    n++;
  int fd = open(kLog, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd >= 0) {
    long w = write(fd, s, n);
    (void)w;
    w = write(fd, "\n", 1);
    (void)w;
    close(fd);
  }
}

bool flagGet(const char *path) {
  int fd = open(path, 0);
  if (fd < 0)
    return false;
  close(fd);
  return true;
}
// Switch debug_log: the per-event lines (touchpad gestures, knob directions, HUD calls) only with it. Read once, on first use; also
// on the disk, as boot.sh's wrappers do, in case /var is not linked yet.
bool debugLog() {
  static int on = -1;
  if (on < 0)
    on = flagGet("/var/opt/hmi/data/rw/carplaymenu/debug_log") || flagGet("/fs/sda1/opt/hmi/data/ID5/carplaymenu/debug_log");
  return on;
}
void dlogLine(const char *s) {  // logLine with debug_log only
  if (debugLog())
    logLine(s);
}
void flagSet(const char *path, bool on) {
  if (on) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
      long w = write(fd, "1", 1);
      (void)w;
      close(fd);
    }
  } else {
    unlink(path);
  }
}

// ---- options -----------------------------------------------------------------------------------------------------------------------
struct Option {
  const char *file;
  unsigned action, model;
  const char *label;
  bool on;
};
// The files and ids of earlier versions stay (an update keeps what was on; loadOptions converts the old menu).
enum {
  OPT_FS,
  OPT_KNOB,
  OPT_PAD,
  OPT_TAP,
  OPT_TWO,
  OPT_ZOOM,
  OPT_HUD,
  OPT_FILL,
  OPT_BLINK,
  OPT_N
};
Option g_options[OPT_N] = {
  { "fullscreen", 5, 12, "Full-screen CarPlay", false },
  { "remap_knob", 6, 17, "Enable knob tilts", false },
  { "remap_touchpad", 7, 18, "Enable touchpad", false },
  { "tap_click", 46, 47, "Tap touchpad as click", false },
  { "two_finger", 30, 32, "Two fingers tap to back", false },
  { "knob_zoom", 61, 62, "Enable rotation as zoom", false },
  { "hud_guidance", 8, 19, "CarPlay directions in the HUD", false },
  { "hud_fill", 84, 85, "Fill toward the turn", false },
  { "hud_blink", 88, 89, "Blink near the turn", false },
};
const unsigned kActRestart = 9;
bool g_media, g_padTilts;  // test switches media_gestures, touchpad_tilts

// "Touchpad sensitivity": radio buttons for the touchpad's size in Connectivity's carplay.ini (touchpad_mm, iniwide.h kConnPad); the
// iPhone moves further per slide on a bigger pad (car 2026-11-12: 50 and 100 mm slow, 250 mm good).
struct Level {
  unsigned action, model;
  u32 mm;
  const char *label;
};
const Level kSens[3] = { { 52, 55, 100, "Low" }, { 53, 56, 175, "Medium" }, { 54, 57, 250, "High" } };  // High = the car's 250
const char *const kPadMm = "/var/opt/hmi/data/rw/carplaymenu/touchpad_mm";
u32 flagNum(const char *path, u32 def, u32 lo, u32 hi);
u32 padMm() {
  return flagNum(kPadMm, 250, 10, 1000);
}
int sensLevel(u32 mm) {
  return mm < 138 ? 0 : mm < 213 ? 1 : 2;
}  // a hand-written value shows as the nearest level

// "HUD effects > Effect colour": radio buttons for hud_colour, the fill / blink colour index (hud.h); F54 palette at night
// (hud_probe.sh colours, 2026-11-13): 5 orange, 3 grey, 4 white (the route's own colour)
struct Colour {
  unsigned action, model;
  u32 index;
  const char *label;
  int pad;
};
const Colour kColours[3] = { { 96, 99, 5, "Orange", 0 }, { 97, 100, 3, "Grey", 0 }, { 98, 101, 4, "White", 0 } };
const char *const kHudColour = "/var/opt/hmi/data/rw/carplaymenu/hud_colour";
int colourLevel() {
  u32 c = flagNum(kHudColour, 5, 0, 31);
  for (int k = 0; k < 3; k++)
    if (kColours[k].index == c)
      return k;
  return -1;
}

void optPath(int i, char *buf, unsigned cap) {
  snprintf(buf, cap, "%s/%s", kCfgDir, g_options[i].file);
}
void loadOptions() {
  char p[128];
  for (int i = 0; i < OPT_N; i++) {
    optPath(i, p, sizeof p);
    g_options[i].on = flagGet(p);
  }
  snprintf(p, sizeof p, "%s/media_gestures", kCfgDir);
  g_media = flagGet(p);
  snprintf(p, sizeof p, "%s/touchpad_tilts", kCfgDir);
  g_padTilts = flagGet(p);
  // The menu of 2026-11-12 ("iDrive control"): every touchpad gesture needs "Enable touchpad", and a tap selects only with "Tap
  // touchpad as click". Switches from the menu before that keep doing what they did: once, on the first start with this plugin.
  snprintf(p, sizeof p, "%s/menu_v2", kCfgDir);
  if (!flagGet(p)) {
    mkdir(kCfgDir, 0777);
    char q[128];
    if ((g_options[OPT_TWO].on || g_media) && !g_options[OPT_PAD].on) {
      g_options[OPT_PAD].on = true;
      optPath(OPT_PAD, q, sizeof q);
      flagSet(q, true);
    }
    if (g_options[OPT_PAD].on) {
      g_options[OPT_TAP].on = true;
      optPath(OPT_TAP, q, sizeof q);
      flagSet(q, true);
    }
    flagSet(p, true);
    logLine("CarPlayMenu: switches of the old menu carried over (Enable touchpad / Tap touchpad as click)");
  }
  unlink(kRestart);
}

// ---- code patches in HmiMain3D ----------------------------------------------------------------------------------------------------
// The executable's text cannot be made writable: the page is replaced by a private anonymous copy.
// The words are written into the temporary copy before the swap, so the page is without code only during the mmap+memcpy.
struct Word {
  u32 addr, orig, neu;
};
// kTilt006 / kTilt1703: generated by build.sh from src/tilt_006.json and src/tilt_1703.json
#include "out/tilt.inc"
// "Full-screen CarPlay" without the CARPLAY_FULLSCREEN coding. With it, variant 0x9C71 is set and CCarPlayActivity keeps it at +0x108
// (in the constructors, 0x43cd5c / 0x43ce68); with the flag it hides the side panel and sets IsCarPlayFullscreen in state 74001, which
// the MINI layout of fullscreenOn() uses for the 1180 px slot (the BMW common layout: the whole screen). The constructors run before
// the plugin loads, so the reads of the flag are what is forced "on": the six ldrb [rN, #0x108] in CCarPlayActivity and the variant
// query at 0x43c0c0 (blx -> mov r0, #1).
// clang-format off
const Word kFsCode[] = {
  { 0x43bbe0, 0xe5d00108, 0xe3a00001 },  // ldrb r0, [r0, #0x108] -> mov r0, #1
  { 0x43bbfc, 0xe5d00108, 0xe3a00001 },
  { 0x43c9ec, 0xe5d43108, 0xe3a03001 },  // ldrb r3, [r4, #0x108] -> mov r3, #1
  { 0x43d1f0, 0xe5d43108, 0xe3a03001 },
  { 0x43e4d0, 0xe5d43108, 0xe3a03001 },
  { 0x43e974, 0xe5d43108, 0xe3a03001 },
  { 0x43c0c0, 0xe12fff33, 0xe3a00001 },  // isVariantSet(0x9C71): blx r3 -> mov r0, #1
};
// clang-format on

// ---- HMI builds ---------------------------------------------------------------------------------------------------------------------
// Addresses and offsets that depend on HmiMain3D. The build is chosen from version.txt; every word is still checked before it is written.
//   006_017_006  ID5_1803-510-1749MA Build 71
//   1703         ID5_1703-490-1650ZD Build 139 (NBTevo_M17345I, read from a car), mapped by matching functions with 006_017_006.
//                CCarPlayActivity::handleTouchInput 0x8f55cc and handleControllerInput 0x8f5e94 have the same code; CTouchData has one
//                more field before the finger list (list at +8/+0xc, origin at +0x1c, time at +0x18, each field 4 bytes later).
//                The CARPLAY_FULLSCREEN flag does not exist in this build's CCarPlayActivity (the getters return constants): "Full-screen
//                CarPlay" only changes the size, the layout's slot and side panel (names in strings; MINI pbw::patchFixed, BMW
//                pbw::patchCommon1703) and carplay.ini; no HMI word is written.
struct Build {
  const char *id, *tag;
  u32 touchFn, ctlSetFn, ctlProxy, playFn, playObj;  // see the touchpad hook below
  u32 tdFirst, tdEnd, tdTime, tdOrigin;  // CTouchData: finger list [first, end), time (ms), origin (1 screen, 2/3 touchpad)
  const Word *tilt;
  u32 tiltN;
  const Word *fs;
  u32 fsN;     // CARPLAY_FULLSCREEN flag reads forced on (0 = no flag)
  u32 pbw;     // MINI layout: 1 = pbw::patch (006, IsCarPlayFullscreen ?), 2 = pbw::patchFixed
  u32 pbwBmw;  // common layout (BMW): 1 = already "IsCarPlayFullscreen ? screenW" (pbw::hasFullscreen006, nothing written),
               // 2 = pbw::patchCommon1703
  u32 ini;     // 1 = carplay.ini full width too (iniwide.h: 1184 on the MINI, 1440 / 1280 on a BMW); 0 = layout only
};
// clang-format off
const Build kBuilds[] = {
  { "ID5_1803-510-1749MA Build 71", "006_017_006",
    /* touchFn, ctlSetFn, ctlProxy, playFn, playObj */ 0x43c02c, 0x1fd0d8c, 0x4e60, 0x1fd0cec, 0x52b0,
    /* tdFirst, tdEnd, tdTime, tdOrigin */             4, 8, 0x14, 0x18,
    /* tilt */ kTilt006, sizeof kTilt006 / sizeof kTilt006[0],
    /* fs */   kFsCode, sizeof kFsCode / sizeof kFsCode[0],
    /* pbw, pbwBmw, ini */ 1, 1, 1 },
  { "ID5_1703-490-1650ZD Build 139", "1703-490 Build 139",
    /* touchFn, ctlSetFn, ctlProxy, playFn, playObj */ 0x8f55cc, 0x20d9e50, 0x4e60, 0x20d9db0, 0x52b0,
    /* tdFirst, tdEnd, tdTime, tdOrigin */             8, 0xc, 0x18, 0x1c,
    /* tilt */ kTilt1703, sizeof kTilt1703 / sizeof kTilt1703[0],
    /* fs */   0, 0,                      // no CARPLAY_FULLSCREEN flag in this build
    /* pbw, pbwBmw, ini */ 2, 2, 1 },     // ini 1: the iPhone renders 1184 wide in the car (2026-11-12); 1180 kept it at "connecting"
};
// clang-format on
const Build *g_build;  // 0 = unknown build: menu only, nothing is written into the HMI

const char *const kVersion = "/opt/hmi/ID5/data/ro/version.txt";
bool contains(const char *s, const char *w) {
  for (; *s; s++) {
    int k = 0;
    while (w[k] && s[k] == w[k])
      k++;
    if (!w[k])
      return true;
  }
  return false;
}
void detectBuild() {
  char v[160];
  int fd = open(kVersion, 0);
  long n = fd >= 0 ? read(fd, v, sizeof v - 1) : -1;
  if (fd >= 0)
    close(fd);
  if (n < 0)
    n = 0;
  v[n] = 0;
  for (long i = 0; i < n; i++)
    if (v[i] == '\n' || v[i] == '\r') {
      v[i] = 0;
      break;
    }
  g_build = 0;
  for (u32 i = 0; i < sizeof kBuilds / sizeof kBuilds[0]; i++)
    if (contains(v, kBuilds[i].id))
      g_build = &kBuilds[i];
  char b[240];
  if (g_build)
    snprintf(b, sizeof b, "CarPlayMenu: HMI build %s (%s)", g_build->tag, v);
  else
    snprintf(b, sizeof b, "CarPlayMenu: HMI build '%s' unknown: menu only, nothing is patched", v);
  logLine(b);
}

#define PROT_RW              0x300
#define PROT_RWX             0x700
#define MAP_PRIV_ANON        0x80002
#define MAP_PRIV_ANON_FIXED  0x80012
#define MS_INVALIDATE_ICACHE 0x10

bool writePage(u32 pg, const Word *w, u32 n) {
  u8 *tmp = (u8 *)mmap(0, 0x1000, PROT_RW, MAP_PRIV_ANON, -1, 0);
  if ((long)tmp == -1) {
    logLine("CarPlayMenu: patch: mmap tmp failed");
    return false;
  }
  memcpy(tmp, (void *)pg, 0x1000);
  for (u32 i = 0; i < n; i++)
    if ((w[i].addr & ~0xfffu) == pg)
      *(u32 *)(tmp + (w[i].addr & 0xfff)) = w[i].neu;
  void *r = mmap((void *)pg, 0x1000, PROT_RWX, MAP_PRIV_ANON_FIXED, -1, 0);
  if ((long)r == -1) {
    munmap(tmp, 0x1000);
    logLine("CarPlayMenu: patch: mmap fixed failed");
    return false;
  }
  memcpy((void *)pg, tmp, 0x1000);
  munmap(tmp, 0x1000);
  msync((void *)pg, 0x1000, MS_INVALIDATE_ICACHE);
  return true;
}
// Checks all originals (a word that already has the new value counts as done); nothing is written if any differs.
bool patchWords(const char *name, const Word *w, u32 n) {
  char b[160];
  for (u32 i = 0; i < n; i++) {
    u32 cur = *(volatile u32 *)w[i].addr;
    if (cur != w[i].orig && cur != w[i].neu) {
      snprintf(b, sizeof b, "CarPlayMenu: %s: word at %x is %x, expected %x: other HMI build, nothing written", name, w[i].addr,
               cur, w[i].orig);
      logLine(b);
      return false;
    }
  }
  u32 done[16];
  u32 nd = 0;
  for (u32 i = 0; i < n; i++) {
    u32 pg = w[i].addr & ~0xfffu, k = 0;
    while (k < nd && done[k] != pg)
      k++;
    if (k < nd)
      continue;
    if (nd == 16 || !writePage(pg, w, n))
      return false;
    done[nd++] = pg;
  }
  snprintf(b, sizeof b, "CarPlayMenu: %s: %u words written", name, n);
  logLine(b);
  return true;
}

// ---- controller touchpad ("Enable touchpad" and its options) --------------------------------------------------------------------
// CCarPlayActivity::handleTouchInput (0x43c02c) gets the CTouchData of the controller's touchpad (origin 2/3) and sends it to CarPlay as
// CarPlayTouchPadInput. With "Enable touchpad" (OPT_PAD) the hook decides per gesture (from the first finger down to the last one up)
// what goes to CarPlay:
//   * one finger goes on to CarPlay's touchpad (declared in Connectivity's carplay.ini, iniwide.h); with the test switch touchpad_tilts it
//     scrolls instead: every kStep of slide from the anchor is a tilt in the dominant direction, already while moving, and the anchor
//     moves along. The tilt goes out through the same send handleControllerInput uses (CAuxIn_CarPlayControllerInput,
//     0x1fd0d8c: proxy, rotation, press, back, up, down, left, right, voice).
//   * "Tap touchpad as click" (OPT_TAP): a short one-finger tap = press (CarPlay's touchpad has no select).
//   * "Two fingers" (OPT_TWO): two-finger slides (their centre) = tilts, a short two-finger tap = back.
//   * test switch media_gestures (without "Two fingers"): two fingers up/down change the volume every kVolStep while sliding
//     (AudioMaster VolumeActual over MOST, hud.h); right/left, once per gesture, next/previous track (the
//     CAuxIn_iAP2PlaybackControl request to the iPhone that the HMI uses for the steering wheel's track buttons).
// Two fingers only become a slide if the centre moves more than the distance between them changes; otherwise it is a pinch and goes to
// CarPlay as a two-finger touchpad (original code + libcppinch.so in Connectivity; in the car the controller does not report the second
// finger's position, so this does not happen). Screen touches (origin 1) and the touchpad without "Enable touchpad" go to the original code.
// Per build (Build): touchFn = CCarPlayActivity::handleTouchInput(this, CTouchData**); ctlSetFn = CPxyAuxiliaryInput::
// CAuxIn_CarPlayControllerInput "Set", at *(activity + 0xb0) + ctlProxy (as in handleControllerInput); playFn =
// CAuxIn_iAP2PlaybackControl "StartResult"(u8 code), at *(activity + 0xb0) + playObj (CDevExternalDevicesSkipDevice: +0x52b0).
const int kStep = 64;     // slide per tilt, of 0..511
const int kVolStep = 40;  // vertical two-finger slide per volume step
const int kTrack = 90;    // horizontal two-finger slide to change track
const int kDecide = 24;   // movement before deciding between slide and pinch
const u32 kTapMs = 350;   // tap: shorter than this and without moving kDecide

typedef void (*TouchFn)(void *, void **);
typedef void (*CtlSetFn)(void *, u32 rot, u32 press, u32 back, u32 up, u32 down, u32 left, u32 right, u32 voice);
typedef int (*PlayFn)(void *, u32 code);
TouchFn g_touchOrig;
bool g_touchHooked;

struct Finger {
  bool on;
  int x0, y0, x, y;
};
Finger g_fingers[2];
enum Mode {
  M_NONE,
  M_ONE,
  M_UNDECIDED,
  M_PINCH,
  M_ARROWS,
  M_VOLUME,
  M_TRACK,
  M_PASS
};
struct Gesture {
  bool active;
  u32 t0;
  int maxF, first;
  bool moved;
  int travel;
  int ax, ay;
  Mode two;
  bool fwd;
  int dist0;
};  // first: id of the 1st finger
Gesture g_gesture;

void ctlSend(void *act, u32 rot, int dir, u32 on, u32 press = 0, u32 back = 0) {
  void *proxy = (void *)(*(u32 *)((u8 *)act + 0xb0) + g_build->ctlProxy);
  u32 d[4] = { 0, 0, 0, 0 };
  if (dir >= 0)
    d[dir] = on;
  ((CtlSetFn)g_build->ctlSetFn)(proxy, rot, press, back, d[0], d[1], d[2], d[3], 0);
}
void ctlDirection(void *act, int dir) {  // 0 up, 1 down, 2 left, 3 right
  static const char *const nm[4] = { "up", "down", "left", "right" };
  char b[80];
  snprintf(b, sizeof b, "CarPlayMenu: touchpad -> %s", nm[dir]);
  dlogLine(b);
  ctlSend(act, 0, dir, 1);
  ctlSend(act, 0, dir, 0);
}
void ctlButton(void *act, bool back) {
  dlogLine(back ? "CarPlayMenu: touchpad two-finger tap -> back" : "CarPlayMenu: touchpad tap -> select");
  if (back) {
    ctlSend(act, 0, -1, 0, 0, 1);
    ctlSend(act, 0, -1, 0, 0, 0);
  } else {
    ctlSend(act, 0, -1, 0, 1, 0);
    ctlSend(act, 0, -1, 0, 0, 0);
  }
}
void playControl(void *act, bool next) {  // track button: press (0x80 | code) and release (code); 2 next, 3 previous
  void *obj = (void *)(*(u32 *)((u8 *)act + 0xb0) + g_build->playObj);
  u32 c = next ? 2 : 3;
  dlogLine(next ? "CarPlayMenu: media gesture -> next track" : "CarPlayMenu: media gesture -> previous track");
  ((PlayFn)g_build->playFn)(obj, 0x80 | c);
  ((PlayFn)g_build->playFn)(obj, c);
}
void mediaVolume(int steps);  // hud.h: AudioMaster VolumeActual Increment / Decrement
int absInt(int v) {
  return v < 0 ? -v : v;
}
// direction of a slide (dx, dy): 0 up, 1 down, 2 left, 3 right
int dirOf(int dx, int dy) {
  return absInt(dx) > absInt(dy) ? (dx > 0 ? 3 : 2) : (dy > 0 ? 1 : 0);
}

void padWrite(const Finger *f);
// Calls the original code with each finger's state changed: from -> to (0 = all) — "move" becomes "down" in the first send of a gesture
// that starts in the middle, and all become "up" (3) when the gesture stops going to CarPlay.
void origWith(void *self, void **ref, u8 from, u8 to) {
  u8 *td = (u8 *)*ref, *e = *(u8 **)(td + g_build->tdEnd);
  u8 saved[2] = { 0, 0 };
  int k = 0;
  for (u8 *q = *(u8 **)(td + g_build->tdFirst); q && q + 10 <= e && k < 2; q += 10, k++) {
    saved[k] = q[7];
    if (!from || q[7] == from)
      q[7] = to;
  }
  g_touchOrig(self, ref);
  k = 0;
  for (u8 *q = *(u8 **)(td + g_build->tdFirst); q && q + 10 <= e && k < 2; q += 10, k++)
    q[7] = saved[k];
}

// The first event after the hook goes to the log with the start of the CTouchData, to check the build's offsets in a car.
void logTouchHeader(const u8 *td) {
  static bool done;
  if (done || !td)
    return;
  done = true;
  const u32 *w = (const u32 *)td;
  char b[200];
  snprintf(b, sizeof b, "CarPlayMenu: touch data (%s): %x %x %x %x %x %x %x %x", g_build->tag, w[0], w[1], w[2], w[3], w[4], w[5],
           w[6], w[7]);
  logLine(b);
}

// Test switch touch_log: the first 120 touchpad events after the HMI starts go to the log raw (origin, time, entry bytes), to read
// how the car's controller reports one and two fingers.
void logTouchRaw(const u8 *td) {
  static int left = -1;
  if (left < 0) {
    char p[128];
    snprintf(p, sizeof p, "%s/touch_log", kCfgDir);
    left = flagGet(p) ? 120 : 0;
  }
  if (left <= 0 || !td)
    return;
  left--;
  const u8 *f = *(u8 *const *)(td + g_build->tdFirst), *e = *(u8 *const *)(td + g_build->tdEnd);
  long n = (f && e > f) ? (long)(e - f) : 0;
  if (n > 48)
    n = 48;
  char b[200];
  int o = snprintf(b, sizeof b, "CarPlayMenu: touch raw org %u t %u n %ld:", *(const u32 *)(td + g_build->tdOrigin),
                   *(const u32 *)(td + g_build->tdTime), (long)((f && e > f) ? e - f : 0));
  for (long i = 0, k = 0; i < n && o < (int)sizeof b - 4; i++, k = k == 9 ? 0 : k + 1)  // a space before each 10-byte entry
    o += snprintf(b + o, sizeof b - o, "%s%02x", k ? "" : " ", f[i]);
  logLine(b);
}

void touchHook(void *self, void **ref) {
  u8 *td = ref ? (u8 *)*ref : 0;
  logTouchHeader(td);
  logTouchRaw(td);
  u32 origin = td ? *(u32 *)(td + g_build->tdOrigin) : 0;
  bool pad = g_options[OPT_PAD].on, tapClick = g_options[OPT_TAP].on, two = g_options[OPT_TWO].on, media = g_media && !two;
  if (!pad || (origin != 2 && origin != 3)) {
    g_touchOrig(self, ref);
    return;
  }
  u32 now = *(u32 *)(td + g_build->tdTime);  // time of the touch (ms), from the CTouchData
  Gesture &gesture = g_gesture;
  u8 *f = *(u8 **)(td + g_build->tdFirst), *e = *(u8 **)(td + g_build->tdEnd);
  int fingersBefore = g_fingers[0].on + g_fingers[1].on;  // fingers before this event: the last movement may come with the release
  // finger: u16 x, u16 y, u16 z, u8 type, u8 state (1 down, 2 move, 3 up), u8 id. In the car (HMI 1703, touch_log 2026-11-12) the
  // second finger comes without a position: x = y = z = 0xffff and the same id 0 as the first; it counts as finger 2 resting on finger 1
  // (the two-finger slide moves with the first one's point; a pinch cannot be measured).
  for (int k = 0; f && f + 10 <= e; f += 10, k++) {
    u32 id = f[8];
    int x = *(u16 *)f, y = *(u16 *)(f + 2);
    u8 st = f[7];
    bool blind = x == 0xffff && y == 0xffff;
    if (blind) {
      if (k == 0)
        continue;
      id = 1;
      x = g_fingers[0].x;
      y = g_fingers[0].y;
    }
    if (id > 1)
      continue;
    Finger &g = g_fingers[id];
    if (st == 1 || (st == 2 && !g.on)) {
      g.on = true;
      g.x0 = g.x = x;
      g.y0 = g.y = y;
    } else if (st == 2) {
      g.x = x;
      g.y = y;
    } else {
      g.on = false;
      g.x = x;
      g.y = y;
    }
  }
  int fingersDown = g_fingers[0].on + g_fingers[1].on;
  if (fingersDown && !gesture.active) {  // start of the gesture
    gesture.active = true;
    gesture.t0 = now;
    gesture.maxF = 0;
    gesture.moved = false;
    gesture.travel = 0;
    gesture.two = M_NONE;
    gesture.fwd = false;
    gesture.first = g_fingers[0].on ? 0 : 1;
    gesture.ax = g_fingers[gesture.first].x;
    gesture.ay = g_fingers[gesture.first].y;
  }
  bool oneMine = g_padTilts && !two;  // one finger: our tilts (touchpad_tilts); otherwise it goes to CarPlay as touchpad
  if (fingersDown > gesture.maxF) {
    gesture.maxF = fingersDown;
    if (fingersDown == 2) {  // the second finger came: anchor at the centre, starting distance for the pinch
      gesture.ax = (g_fingers[0].x + g_fingers[1].x) / 2;
      gesture.ay = (g_fingers[0].y + g_fingers[1].y) / 2;
      gesture.dist0 = absInt(g_fingers[0].x - g_fingers[1].x) + absInt(g_fingers[0].y - g_fingers[1].y);
      gesture.two = M_UNDECIDED;
      if (gesture.fwd) {
        origWith(self, ref, 0, 3);
        gesture.fwd = false;
      }  // the finger that went to CarPlay leaves; two fingers wait for the decision
    }
  }
  bool lift = fingersDown < fingersBefore;  // the final position of the finger that lifted still counts (an idle HMI merges events)
  if (gesture.maxF == 1 && (fingersDown == 1 || (lift && fingersBefore == 1))) {  // ---- one finger ----
    const Finger &a = g_fingers[gesture.first];
    gesture.travel =
        absInt(a.x - a.x0) + absInt(a.y - a.y0) > gesture.travel ? absInt(a.x - a.x0) + absInt(a.y - a.y0) : gesture.travel;
    if (oneMine) {
      int dx = a.x - gesture.ax, dy = a.y - gesture.ay;
      if (absInt(dx) >= kStep || absInt(dy) >= kStep) {
        ctlDirection(self, dirOf(dx, dy));
        gesture.ax = a.x;
        gesture.ay = a.y;
        gesture.moved = true;
      }
    } else {
      g_touchOrig(self, ref);
      gesture.fwd = fingersDown > 0;
    }
  } else if (gesture.maxF == 2 && (fingersDown == 2 || (lift && fingersBefore == 2))) {  // ---- two fingers ----
    int cx = (g_fingers[0].x + g_fingers[1].x) / 2, cy = (g_fingers[0].y + g_fingers[1].y) / 2, dx = cx - gesture.ax,
        dy = cy - gesture.ay;
    int dd = absInt(g_fingers[0].x - g_fingers[1].x) + absInt(g_fingers[0].y - g_fingers[1].y) - gesture.dist0;
    if (gesture.two == M_UNDECIDED && (absInt(dx) >= kDecide || absInt(dy) >= kDecide || absInt(dd) >= kDecide)) {
      bool slide = absInt(dx) + absInt(dy) > absInt(dd);
      if (!slide)
        gesture.two = M_PINCH;
      else if (two)
        gesture.two = M_ARROWS;
      else if (media)
        gesture.two = absInt(dx) > absInt(dy) ? M_TRACK : M_VOLUME;
      else
        gesture.two = M_PASS;
      gesture.moved = true;
      if (gesture.two == M_PINCH)
        dlogLine("CarPlayMenu: touchpad two fingers -> pinch to CarPlay");
      if (gesture.two == M_PASS)
        dlogLine("CarPlayMenu: touchpad two fingers -> passed to CarPlay");
    }
    if (gesture.two == M_PINCH || gesture.two == M_PASS) {
      if (gesture.fwd)
        g_touchOrig(self, ref);
      else if (!lift) {
        origWith(self, ref, 2, 1);
        gesture.fwd = true;
      }
    } else if (gesture.two == M_ARROWS) {
      if (absInt(dx) >= kStep || absInt(dy) >= kStep) {
        ctlDirection(self, dirOf(dx, dy));
        gesture.ax = cx;
        gesture.ay = cy;
      }
    } else if (gesture.two == M_VOLUME) {
      if (absInt(dy) >= kVolStep) {
        int n = dy / kVolStep;
        mediaVolume(-n);
        gesture.ay += n * kVolStep;
      }  // up (smaller y) = louder
    } else if (gesture.two == M_TRACK) {
      if (absInt(dx) >= kTrack) {
        playControl(self, dx > 0);
        gesture.two = M_NONE;
      }  // once per gesture
    }
  } else if (gesture.fwd)
    g_touchOrig(self, ref);              // a finger leaving a gesture that went to CarPlay
  if (!fingersDown && gesture.active) {  // ---- end of the gesture ----
    gesture.active = false;
    bool tap = !gesture.moved && now - gesture.t0 < kTapMs && gesture.travel < kDecide &&
               (gesture.two == M_UNDECIDED || gesture.two == M_NONE);
    if (tap && gesture.maxF == 2 && two)
      ctlButton(self, true);
    else if (tap && gesture.maxF == 1 && (tapClick || oneMine))
      ctlButton(self, false);  // CarPlay's touchpad has no select (car)
    if (gesture.fwd)
      gesture.fwd = false;
  }
  padWrite(g_fingers);  // both fingers for libcppinch.so (pinch in Connectivity)
}

// ---- knob directions ------------------------------------------------------------------------------------------------------------
// Up/down/left/right never reach the iPhone through the HMI's controller message: Connectivity turns it into a KnobEvent without
// directions (button type, rotation, pressed), and the knob HID has X/Y only with [KnobInput] NudgeSupport (iniwide.h). Found in the car
// (HMI ID5_1703-490) and the same in 006_017_006. So the send function (CAuxIn_CarPlayControllerInput "Set", g_build->ctlSetFn) is hooked:
// every direction there — the controller tilts once the carplay_tilt patch routes them here, and the touchpad gestures (ctlSend) —
// goes to /dev/shmem/carplaymenu-knob, where libcppinch.so in Connectivity turns it into CCarplayServiceMgr::setKnobEvt(..., x, y, 0)
// (src/pinch.c); the HMI's own call goes on with the directions cleared (they came out as "select" otherwise).
const char *const kKnob = "/dev/shmem/carplaymenu-knob";
void knobWrite(int x, int y, u32 released = 0) {  // struct knob in pinch.c; "ms" carries the flag: 1 = the direction x, y let go
  struct {
    u32 magic, seq, ms;
    int x, y;
  } r;
  static u32 seq;
  r.magic = 0x4b4e4231u;
  r.seq = ++seq;
  r.ms = released;
  r.x = x;
  r.y = y;
  int fd = open(kKnob, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    return;
  long w = write(fd, &r, sizeof r);
  (void)w;
  close(fd);
}
CtlSetFn g_ctlOrig;
bool g_ctlHooked;
// A call without a direction after one is its release: written with the flag, so libcppinch can repeat a held tilt until then (its
// test switch knob_repeat); the direction goes with it, in case the press was overwritten before libcppinch read it (the touchpad
// sends both at once). The test switch ctl_log logs every call (to see what the controller sends while a tilt is held).
bool g_ctlHeld;
int g_ctlX, g_ctlY;
void controllerHook(void *proxy, u32 rot, u32 press, u32 back, u32 up, u32 down, u32 left, u32 right, u32 voice) {
  static int logLeft = -1;
  if (logLeft < 0) {
    char p[128];
    snprintf(p, sizeof p, "%s/ctl_log", kCfgDir);
    logLeft = flagGet(p) ? 200 : 0;
  }
  if (logLeft > 0) {
    logLeft--;
    char b[120];
    snprintf(b, sizeof b, "CarPlayMenu: ctl #%d rot=%d press=%u back=%u up=%u down=%u left=%u right=%u voice=%u", 200 - logLeft,
             (int)rot, press, back, up, down, left, right, voice);
    logLine(b);
  }
  if (up || down || left || right) {
    int x = right ? 1 : left ? -1 : 0, y = down ? 1 : up ? -1 : 0;
    knobWrite(x, y);
    g_ctlHeld = true;
    g_ctlX = x;
    g_ctlY = y;
    char b[64];
    snprintf(b, sizeof b, "CarPlayMenu: knob direction x=%d y=%d", x, y);
    dlogLine(b);
  } else if (g_ctlHeld) {
    knobWrite(g_ctlX, g_ctlY, 1);
    g_ctlHeld = false;
  }
  if (rot || press || back || voice || !(up || down || left || right))  // a direction alone is not passed on
    g_ctlOrig(proxy, rot, press, back, 0, 0, 0, 0, voice);
}
// Entry hook: the first two instructions (push {r4-r8,fp,lr}; add fp,sp,#0x18 in both builds) go to a trampoline.
bool installCtlHook() {
  if (g_ctlHooked)
    return true;
  if (!g_build) {
    logLine("CarPlayMenu: knob: unknown HMI build, not hooked");
    return false;
  }
  u32 fn = g_build->ctlSetFn, *p = (u32 *)fn;
  if (p[0] != 0xe92d49f0 || p[1] != 0xe28db018) {
    logLine("CarPlayMenu: knob: controller send not found (other HMI build), not hooked");
    return false;
  }
  u32 *t = (u32 *)mmap(0, 0x1000, PROT_RWX, MAP_PRIV_ANON, -1, 0);
  if ((long)t == -1) {
    logLine("CarPlayMenu: knob: mmap failed");
    return false;
  }
  t[0] = p[0];
  t[1] = p[1];
  t[2] = 0xe51ff004;
  t[3] = fn + 8;
  msync(t, 0x1000, MS_INVALIDATE_ICACHE);
  g_ctlOrig = (CtlSetFn)(void *)t;
  Word w[2] = { { fn, 0xe92d49f0, 0xe51ff004 }, { fn + 4, 0xe28db018, (u32)(void *)&controllerHook } };
  g_ctlHooked = patchWords("knob directions hook", w, 2);
  return g_ctlHooked;
}

bool installTouchHook() {
  if (g_touchHooked)
    return true;
  if (!g_build) {
    logLine("CarPlayMenu: touchpad: unknown HMI build, not hooked");
    return false;
  }
  const u32 kTouchFn = g_build->touchFn;
  u32 *p = (u32 *)kTouchFn;
  if (p[0] != 0xe92d4ff0 || p[1] != 0xe28db020 || *(u32 *)g_build->ctlSetFn != 0xe92d49f0 ||
      *(u32 *)g_build->playFn != 0xe92d4870) {
    logLine("CarPlayMenu: touchpad: handleTouchInput / controller send not found (other HMI build), not hooked");
    return false;
  }
  u32 *t = (u32 *)mmap(0, 0x1000, PROT_RWX, MAP_PRIV_ANON, -1, 0);  // trampoline: the 2 original instructions and the jump back
  if ((long)t == -1) {
    logLine("CarPlayMenu: touchpad: mmap failed");
    return false;
  }
  t[0] = p[0];
  t[1] = p[1];
  t[2] = 0xe51ff004;
  t[3] = kTouchFn + 8;  // ldr pc,[pc,#-4]
  msync(t, 0x1000, MS_INVALIDATE_ICACHE);
  g_touchOrig = (TouchFn)(void *)t;
  Word w[2] = { { kTouchFn, 0xe92d4ff0, 0xe51ff004 }, { kTouchFn + 4, 0xe28db020, (u32)(void *)&touchHook } };
  g_touchHooked = patchWords("touchpad hook", w, 2);
  return g_touchHooked;
}

}  // namespace
#include "pbwpatch.h"
#include "iniwide.h"
#include "hud.h"
namespace {

bool touchAny() {
  return g_options[OPT_PAD].on;
}  // the touchpad options act only with "Enable touchpad"
// "Media gestures", volume: the same request as a volume control on MOST, AudioMaster (0x20/00) VolumeActual (0x200) Increment /
// Decrement with the number of steps (u8, like the Increment sends of the HMI's CPxyAudioMaster proxy), through hud.h's MOST client.
void mediaVolume(int steps) {
  if (!steps)
    return;
  char b[80];
  snprintf(b, sizeof b, "CarPlayMenu: media gesture -> volume %s%d", steps > 0 ? "+" : "", steps);
  dlogLine(b);
  if (!hud::volume(steps))
    logLine("CarPlayMenu: media gesture: AudioMaster not reachable on MOST");
}

// The size the iPhone draws comes from Connectivity's carplay.ini (section 750_480 on the MINI, 711_480 with display resolution 1; the same
// file in 006_017_006 and 1703): without changing it, the 750 px video would be stretched over the 1180 slot. A copy with both sections at
// 1184 (Width, PhysicalWidth 194; dstw 1180: the iPhone declines 1180, iniwide.h) is linked over it (ln -sP); Connectivity reads it when
// it starts (restart.sh restarts the CONN_MOCCA package; boot.sh makes the link again). On a BMW the sections are 900_540 (10.25") and
// 800_480 (8.8"), made 1440 x 540 and 1280 x 480: the whole screen, as the common layout's full-width slot.
const char *const kIni = "/opt/conn/data/carplay/carplay.ini";
const char *const kIniFs = "/var/opt/hmi/data/rw/carplaymenu/carplay.ini";
bool fsAvailable();
bool brandMini();
bool knobDirs() {
  return g_options[OPT_KNOB].on || touchAny();
}  // the knob directions are used (tilts or touchpad gestures)
bool needConn() {
  return knobDirs() || g_options[OPT_ZOOM].on;
}  // Connectivity with libcppinch.so (directions, rotation as zoom)
// Connectivity's working copy (ini::patchConn): what CarPlay reads, once, when Connectivity starts; boot.sh links the plugin's copy
// before that. The template copy below stays for the case Connectivity writes the working copy anew (coding change).
const char *const kConn = "/var/opt/conn/carplay/carplay.ini";
const char *const kConnFs = "/var/opt/hmi/data/rw/carplaymenu/carplay-conn.ini";
// a number in a test switch file (e.g. touchpad_mm "80"), def if absent or out of lo..hi
u32 flagNum(const char *path, u32 def, u32 lo, u32 hi) {
  int fd = open(path, 0);
  if (fd < 0)
    return def;
  char b[16];
  long n = read(fd, b, sizeof b - 1);
  close(fd);
  u32 v = 0;
  bool any = false;
  for (long i = 0; i < n && b[i] >= '0' && b[i] <= '9' && v < 100000; i++) {
    v = v * 10 + (u32)(b[i] - '0');
    any = true;
  }
  return any && v >= lo && v <= hi ? v : def;
}
bool connIniApply(bool wide, bool nudge, bool pad, bool ts) {
  pathmgr_unlink(kConn);  // reads Connectivity's own file
  if (!wide && !nudge && !pad && !ts) {
    unlink(kConnFs);
    logLine("CarPlayMenu: Connectivity's carplay.ini: original");
    return true;
  }
  int fd = open(kConn, 0);
  if (fd < 0) {
    unlink(kConnFs);
    logLine("CarPlayMenu: Connectivity's carplay.ini not there (yet)");
    return false;
  }
  const u32 cap = 64 * 1024;
  u8 *b = (u8 *)malloc(cap), *o = (u8 *)malloc(cap + 512);
  u32 n = 0, m = 0;
  long r;
  while (b && o && n < cap && (r = read(fd, b + n, cap - n)) > 0)
    n += (u32)r;
  close(fd);
  // the touchpad's size for the iPhone, which scales a slide by it ("Touchpad sensitivity"; 50 and 100 mm felt slow in the car, 250 better)
  u32 mm = padMm();
  char ps[200];
  snprintf(ps, sizeof ps, ini::kConnPad, mm, mm);
  // test switch screen_size "<width> <height> <mm wide> <mm high>" (with Full-screen): what the iPhone is told, for size tries
  char sw[24], sh[24], spw[32], sph[32];
  const char *scr[4] = { 0, 0, 0, 0 };
  char sl[64] = "";
  const char *ww = "Width=1184";  // the full-screen width patchConn used
  {
    u32 v[4] = { 0, 0, 0, 0 };
    int k = 0;
    int fd = open("/var/opt/hmi/data/rw/carplaymenu/screen_size", 0);
    if (fd >= 0) {
      char t[64];
      long r = read(fd, t, sizeof t - 1);
      close(fd);
      for (long j = 0; j < r && k < 4; j++)
        if (t[j] >= '0' && t[j] <= '9') {
          v[k] = v[k] * 10 + (u32)(t[j] - '0');
          if (j + 1 == r || t[j + 1] < '0' || t[j + 1] > '9')
            k++;
        }
    }
    if (k >= 1 && v[0] >= 320 && v[0] <= 4096) {
      snprintf(sw, sizeof sw, "Width=%u", v[0]);
      scr[0] = sw;
    }
    if (k >= 2 && v[1] >= 240 && v[1] <= 2160) {
      snprintf(sh, sizeof sh, "Height=%u", v[1]);
      scr[1] = sh;
    }
    if (k >= 3 && v[2] >= 50 && v[2] <= 1000) {
      snprintf(spw, sizeof spw, "PhysicalWidth=%u", v[2]);
      scr[2] = spw;
    }
    if (k >= 4 && v[3] >= 20 && v[3] <= 1000) {
      snprintf(sph, sizeof sph, "PhysicalHeight=%u", v[3]);
      scr[3] = sph;
    }
    if (wide && k)
      snprintf(sl, sizeof sl, " screen_size %u x %u, %u x %u mm", v[0], v[1], v[2], v[3]);
  }
  if (b && o && n < cap)
    m = ini::patchConn(b, n, o, wide, nudge, pad ? ps : 0, ts, scr, &ww);
  if (m) {
    int w = open(kConnFs, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (w < 0 || write(w, o, m) != (long)m)
      m = 0;
    if (w >= 0)
      close(w);
  }
  free(b);
  free(o);
  if (!m) {
    unlink(kConnFs);
    logLine("CarPlayMenu: Connectivity's carplay.ini: not the expected sections, left as it is");
    return false;
  }
  if (pathmgr_symlink(kConnFs, kConn) != 0) {
    logLine("CarPlayMenu: Connectivity's carplay.ini: ln -sP failed");
    return false;
  }
  char lb[220];
  snprintf(lb, sizeof lb, "CarPlayMenu: Connectivity's carplay.ini linked:%s%s%s%s%s%s%s (read when Connectivity starts)",
           wide ? " " : "", wide ? ww + 6 : "", wide ? " wide" : "", sl, nudge ? " NudgeSupport" : "", pad ? " touchpad" : "",
           ts ? " touchscreen" : "");
  logLine(lb);
  if (pad) {
    snprintf(lb, sizeof lb, "CarPlayMenu: touchpad for CarPlay: 0..511, %u x %u mm, primary input", mm, mm);
    logLine(lb);
  }
  return true;
}
// The copies of carplay.ini for what is switched on: 1184 wide (fullscreen, on builds with ini 1 or the test switch fullscreen_ini),
// NudgeSupport (knob directions) and, in the working copy, the touchpad ("Enable touchpad") and the hidden touchscreen that "Enable
// rotation as zoom" pinches on. Nothing: the originals again.
bool iniApply() {
  bool wide = g_options[OPT_FS].on && fsAvailable() && (g_build->ini || flagGet("/var/opt/hmi/data/rw/carplaymenu/fullscreen_ini"));
  bool nudge = knobDirs();
  connIniApply(wide, nudge, touchAny(), g_options[OPT_ZOOM].on);
  pathmgr_unlink(kIni);  // reads the original
  if (!wide && !nudge) {
    unlink(kIniFs);
    logLine("CarPlayMenu: carplay.ini: original");
    return true;
  }
  int fd = open(kIni, 0);
  if (fd < 0) {
    logLine("CarPlayMenu: carplay.ini not found");
    return false;
  }
  const u32 cap = 64 * 1024;
  u8 *b = (u8 *)malloc(cap), *o = (u8 *)malloc(cap + 64);
  u32 n = 0, m = 0;
  long r;
  while (b && o && n < cap && (r = read(fd, b + n, cap - n)) > 0)
    n += (u32)r;
  close(fd);
  if (b && o && n < cap)
    m = ini::patch(b, n, o, wide, nudge, !brandMini());
  if (m) {
    int w = open(kIniFs, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (w < 0 || write(w, o, m) != (long)m)
      m = 0;
    if (w >= 0)
      close(w);
  }
  free(b);
  free(o);
  if (!m) {
    unlink(kIniFs);
    logLine("CarPlayMenu: carplay.ini: not the expected sections, left as it is");
    return false;
  }
  if (pathmgr_symlink(kIniFs, kIni) != 0) {
    logLine("CarPlayMenu: carplay.ini: ln -sP failed");
    return false;
  }
  char lb[120];
  snprintf(lb, sizeof lb, "CarPlayMenu: carplay.ini linked:%s%s (Connectivity reads it when it starts)",
           !wide         ? ""
           : brandMini() ? " 1184 wide"
                         : " full width (900_540 1440, 800_480 1280)",
           nudge ? " NudgeSupport" : "");
  logLine(lb);
  return true;
}

// Which layout has the CarPlay state: MINI's own (mini/id5, kSsmPaths) or the HMI's common one (BMW, BMW i, Rolls-Royce;
// kSsmCommonPaths). The launcher exports the coded brand to the processes it starts (sloginfo "StarterPlugin: ENV: HMI_BRAND=02" on
// the MINI; the others by the order of the coding's va_HMI_BRAND values: 00 BMW, 01 BMW i, 03 Rolls-Royce, 04 BMW M, 05 J29; only 02
// seen in a car). The folder alone does not say it: which brand packages a unit carries follows its part number (the smaller-screen
// parts carry the MINI one whatever the car). Without the variable, a unit with the MINI layout counts as a MINI (the behaviour
// before BMW support).
u8 g_brand;  // 0 = not looked up yet, 1 = MINI, 2 = other
bool brandMini() {
  if (!g_brand) {
    const char *v = getenv("HMI_BRAND");
    char b[120];
    if (v && v[0] == '0' && v[1] >= '0' && v[1] <= '5' && !v[2]) {
      g_brand = v[1] == '2' ? 1 : 2;
      snprintf(b, sizeof b, "CarPlayMenu: brand HMI_BRAND=%s: %s layout", v, g_brand == 1 ? "MINI" : "common (BMW)");
    } else {
      int fd = open(kSsmPaths[0], 0);
      if (fd < 0)
        fd = open(kSsmPaths[1], 0);
      if (fd >= 0)
        close(fd);
      g_brand = fd >= 0 ? 1 : 2;
      snprintf(b, sizeof b, "CarPlayMenu: brand: HMI_BRAND %s, %s layout", v ? "has an unknown value" : "not set",
               g_brand == 1 ? "MINI layout present: MINI" : "no MINI layout: common (BMW)");
    }
    logLine(b);
  }
  return g_brand == 1;
}
// The layout patch of this build for this car's brand (0 = none)
u32 fsLayout() {
  return !g_build ? 0 : brandMini() ? g_build->pbw : g_build->pbwBmw;
}
bool fsAvailable() {
  return fsLayout() != 0;
}
// MINI: writes kSsmFs from the original MINI layout and links both MINI paths (kSsmPaths) to it. BMW: the same with the common layout
// (kSsmCommonFs, kSsmCommonPaths), or on 006_017_006 only checks that the common layout already has the full-width slot. The original
// is read without the links, removed first; the other brand's links and copy go.
bool fullscreenOn() {
  if (!fsAvailable()) {
    logLine("CarPlayMenu: fullscreen: not available on this HMI build, nothing changed");
    return false;
  }
  bool mini = brandMini();
  const char *const *paths = mini ? kSsmPaths : kSsmCommonPaths;
  const char *fs = mini ? kSsmFs : kSsmCommonFs;
  u32 kind = fsLayout();
  for (int i = 0; i < 2; i++) {
    pathmgr_unlink(kSsmPaths[i]);
    pathmgr_unlink(kSsmCommonPaths[i]);
  }
  unlink(mini ? kSsmCommonFs : kSsmFs);
  {  // made again every time from the original: a plugin update changes it (1703: + the panel)
    int fd = -1;
    for (int i = 0; i < 2 && fd < 0; i++)
      fd = open(paths[i], 0);
    if (fd < 0) {
      logLine(mini ? "CarPlayMenu: fullscreen: MINI layout not found" : "CarPlayMenu: fullscreen: common layout not found");
      return false;
    }
    const u32 cap = mini ? 256 * 1024 : 1024 * 1024;  // the common layout: 223 KB (006), 429 KB (1703)
    u8 *b = (u8 *)malloc(cap), *o = (u8 *)malloc(cap + 256);
    u32 n = 0;
    long r;
    while (b && o && n < cap && (r = read(fd, b + n, cap - n)) > 0)
      n += (u32)r;
    close(fd);
    if (!mini && kind == 1) {  // 006: the HMI's own full-width slot, nothing to write
      bool ok = b && o && n < cap && pbw::hasFullscreen006(b, n);
      free(b);
      free(o);
      unlink(fs);
      logLine(ok ? "CarPlayMenu: fullscreen: common layout has the full-width slot (IsCarPlayFullscreen), no layout change"
                 : "CarPlayMenu: fullscreen: not the common iDrive 5 layout of this HMI build, nothing changed");
      return ok;
    }
    u32 m = (b && o && n < cap) ? (!mini       ? pbw::patchCommon1703(b, n, o)
                                   : kind == 2 ? pbw::patchFixed(b, n, o)
                                               : pbw::patch(b, n, o))
                                : 0;
    if (m) {
      int w = open(fs, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (w < 0 || write(w, o, m) != (long)m)
        m = 0;
      if (w >= 0)
        close(w);
    }
    free(b);
    free(o);
    if (!m) {
      unlink(fs);
      logLine(mini ? "CarPlayMenu: fullscreen: not the MINI iDrive 5 layout of this HMI build, nothing changed"
                   : "CarPlayMenu: fullscreen: not the common iDrive 5 layout of this HMI build, nothing changed");
      return false;
    }
    logLine("CarPlayMenu: fullscreen: wide CarPlay layout written");
  }
  int linked = 0;
  for (int i = 0; i < 2; i++)
    linked += pathmgr_symlink(fs, paths[i]) == 0;
  if (!linked) {
    logLine("CarPlayMenu: fullscreen: ln -sP failed");
    return false;
  }
  logLine("CarPlayMenu: fullscreen: layout linked (HMI reads it when it starts)");
  return true;
}
// "Add route guidance to HUD", part 1: the iPhone only sends guidance to an accessory that declares it in its iAP2 identification. That is
// done by librghook.so (src/rghook.c) inside mm-ipod, the iAP2 process Connectivity starts: it puts the route guidance messages and a
// RouteGuidanceDisplayComponent into IdentificationInformation, asks for StartRouteGuidanceUpdates and writes what arrives to
// /var/iap/pps/routeguidance. To get into mm-ipod whoever starts it, /opt/conn/bin/carplay/mm-ipod is shadowed (ln -sP) by a script
// that puts librghook.so in front of LD_PRELOAD (it passes the other write() calls on) and runs a copy of the original binary. Applies at mm-ipod's next start.
const char *const kMmipod = "/opt/conn/bin/carplay/mm-ipod";
const char *const kMmipodBin = "/var/opt/hmi/data/rw/carplaymenu/bin/mm-ipod";  // same name: slay/pidin find it
const char *const kMmipodWrap = "/var/opt/hmi/data/rw/carplaymenu/mm-ipod";
const char *const kRghook = "/var/opt/hmi/data/rw/Plugins/CarPlayMenu/librghook.so";
bool copyFile(const char *from, const char *to, unsigned mode) {
  int in = open(from, 0);
  if (in < 0)
    return false;
  int out = open(to, O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (out < 0) {
    close(in);
    return false;
  }
  static u8 b[16384];
  long r;
  bool ok = true;
  while ((r = read(in, b, sizeof b)) > 0)
    if (write(out, b, (unsigned long)r) != r) {
      ok = false;
      break;
    }
  close(in);
  close(out);
  chmod(to, mode);
  if (!ok || r < 0) {
    unlink(to);
    return false;
  }
  return true;
}
// A system binary shadowed (ln -sP) by a script that puts a library of ours in front of LD_PRELOAD and runs a copy of the original
// (same name, for slay/pidin/starter). Applies at the process's next start; boot.sh makes the link again at every boot.
struct Shadow {
  const char *sys, *bin, *wrap, *preload, *pre, *label;
};
const Shadow kRouteGuidanceShadow = {
  kMmipod, kMmipodBin, kMmipodWrap, kRghook, "mkdir -p /var/iap/pps\n", "route guidance: mm-ipod"
};
// "Remap iDrive touch panel", two-finger pinch: libcppinch.so (src/pinch.c) in Connectivity replaces CarPlay's touchpad HID
// with a two-finger one; the second finger comes from the file touchHook writes (kPad).
const Shadow kConnectivityShadow = { "/opt/conn/bin/Connectivity",
                                     "/var/opt/hmi/data/rw/carplaymenu/bin/Connectivity",
                                     "/var/opt/hmi/data/rw/carplaymenu/Connectivity",
                                     "/var/opt/hmi/data/rw/Plugins/CarPlayMenu/libcppinch.so",
                                     "",
                                     "touchpad pinch: Connectivity" };
bool shadowOn(const Shadow &s) {
  char b[200];
  if (!flagGet(s.preload)) {
    snprintf(b, sizeof b, "CarPlayMenu: %s: %s missing in the plugin folder", s.label, s.preload);
    logLine(b);
    return false;
  }
  pathmgr_unlink(s.sys);  // reads the original, not its own script
  mkdir("/var/opt/hmi/data/rw/carplaymenu/bin", 0777);
  if (!flagGet(s.bin) && !copyFile(s.sys, s.bin, 0755)) {
    snprintf(b, sizeof b, "CarPlayMenu: %s: binary not copied", s.label);
    logLine(b);
    return false;
  }
  // In the car /var is a link the launcher makes during boot: the wrapper uses the same files on the disk if it does not exist yet.
  const char *rw = "/var/opt/hmi/data/rw/";
  u32 rl = 0;
  while (rw[rl])
    rl++;
  const char *pre = s.preload, *bin = s.bin;
  bool rel = true;
  for (u32 k = 0; k < rl; k++)
    if (pre[k] != rw[k] || bin[k] != rw[k])
      rel = false;
  char sc[768];
  int n;
  if (rel)
    n = snprintf(sc, sizeof sc,
                 "#!/bin/ksh\n# CarPlayMenu: %s with %s\nR=/var/opt/hmi/data/rw; [ -d $R ] || R=/fs/sda1/opt/hmi/data/ID5\n"
                 "%sexport LD_PRELOAD=$R/%s${LD_PRELOAD:+:$LD_PRELOAD}\nexec $R/%s \"$@\"\n",
                 s.label, s.preload, s.pre, pre + rl, bin + rl);
  else
    n = snprintf(sc, sizeof sc,
                 "#!/bin/ksh\n# CarPlayMenu: %s with %s\n%sexport LD_PRELOAD=%s${LD_PRELOAD:+:$LD_PRELOAD}\nexec %s \"$@\"\n",
                 s.label, s.preload, s.pre, s.preload, s.bin);
  int fd = open(s.wrap, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (fd < 0 || write(fd, sc, (unsigned long)n) != n) {
    if (fd >= 0)
      close(fd);
    snprintf(b, sizeof b, "CarPlayMenu: %s: wrapper not written", s.label);
    logLine(b);
    return false;
  }
  close(fd);
  chmod(s.wrap, 0755);
  if (pathmgr_symlink(s.wrap, s.sys) != 0) {
    snprintf(b, sizeof b, "CarPlayMenu: %s: ln -sP failed", s.label);
    logLine(b);
    return false;
  }
  snprintf(b, sizeof b, "CarPlayMenu: %s now loads %s (from its next start)", s.label, s.preload);
  logLine(b);
  return true;
}
void shadowOff(const Shadow &s) {
  pathmgr_unlink(s.sys);
  unlink(s.wrap);
  char b[160];
  snprintf(b, sizeof b, "CarPlayMenu: %s: original binary again (from its next start)", s.label);
  logLine(b);
}
bool routeGuidanceOn() {
  return shadowOn(kRouteGuidanceShadow);
}
// Connectivity with libcppinch.so: knob directions (tilts, touchpad gestures), rotation as zoom and the two-finger touchpad (pinch).
// The test switch no_pinch in kCfgDir only keeps the one-finger touchpad HID (the library reads it); the rest still needs the library.
bool connOn() {
  return shadowOn(kConnectivityShadow);
}
void routeGuidanceOff() {
  shadowOff(kRouteGuidanceShadow);
}

// Both touchpad fingers for libcppinch.so in Connectivity (struct pad in pinch.c), at every touchpad event.
const char *const kPad = "/dev/shmem/carplaymenu-pad";
void padWrite(const Finger *f) {
  struct {
    u32 magic, seq, ms;
    u16 x1, y1, x2, y2;
    u8 down1, down2, pad[2];
  } r;
  static u32 seq;
  r.magic = 0x50414432u;
  r.seq = ++seq;
  r.ms = hud::nowMs();
  r.x1 = (u16)f[0].x;
  r.y1 = (u16)f[0].y;
  r.x2 = (u16)f[1].x;
  r.y2 = (u16)f[1].y;
  r.down1 = f[0].on;
  r.down2 = f[1].on;
  r.pad[0] = r.pad[1] = 0;
  int fd = open(kPad, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
    return;
  long w = write(fd, &r, sizeof r);
  (void)w;
  close(fd);
}

void fullscreenOff() {
  for (int i = 0; i < 2; i++) {
    pathmgr_unlink(kSsmPaths[i]);
    pathmgr_unlink(kSsmCommonPaths[i]);
  }
  unlink(kSsmFs);
  unlink(kSsmCommonFs);
  logLine("CarPlayMenu: fullscreen: original layout");
}

// Bootloop guard (boot.sh): boot.sh writes boot_pending at every boot; a boot counts as good once the HMI has run kStableS with the
// plugin, and then the file goes. Three failed boots in a row put boot.sh in safe mode (safe_mode): nothing below is applied, the menu
// only saves the switches. "Restart to apply" leaves safe mode, and its reboot does not count as a failed boot.
const char *const kBootPending = "/var/opt/hmi/data/rw/carplaymenu/boot_pending";
const char *const kSafeMode = "/var/opt/hmi/data/rw/carplaymenu/safe_mode";
const u32 kStableS = 60;
bool g_safe;
void *stableThread(void *) {
  for (u32 i = 0; i < kStableS * 2; i++)
    usleep(500000);
  unlink(kBootPending);
  logLine(g_safe ? "CarPlayMenu: boot guard: HMI up for a minute (safe mode stays until Restart to apply)"
                 : "CarPlayMenu: boot guard: HMI up for a minute, this boot counts as good");
  return 0;
}
void startStableTimer() {
  int t;
  if (pthread_create(&t, 0, stableThread, 0) == 0)
    return;
  unlink(kBootPending);  // no timer: the old rule (the plugin started)
  logLine("CarPlayMenu: boot guard: timer thread failed, boot counted as good now");
}

// Applied when the plugin loads (HMI start), from what was stored: hence "Restart to apply".
#ifdef CPM_HUD_ONLY
// The HUD-only menu (libCarPlayMenu-hud.so, installed as libCarPlayMenu.so by the turn-by-turn-only install, --hud): the same plugin
// with only "CarPlay directions in HUD", "HUD effects" and "Effect colour" in its menu (plugin/CarPlayMenu-hud.xml). It patches no HMI
// code and installs no hook, so any build will do. The directions are drawn from mm-ipod (librghook-most / -kombi, which read the same
// switch files when a CarPlay session starts), so every row applies at the next CarPlay connection. On iDrive 6 it shows only with the
// car coded for apps (A4A); without that the HMI never starts it and nothing else changes (boot.sh skips its bootloop guard: hud_only).
const bool kHudOnly = true;
#else
const bool kHudOnly = false;
#endif
inline bool inHudMenu(int i) {
  return i == OPT_HUD || i == OPT_FILL || i == OPT_BLINK;
}

void applyAtStart() {
  char b[200];
  if (kHudOnly) {
    snprintf(b, sizeof b, "CarPlayMenu: start (HUD-only menu): hud=%d fill=%d blink=%d colour=%u", g_options[OPT_HUD].on,
             g_options[OPT_FILL].on, g_options[OPT_BLINK].on, flagNum(kHudColour, 5, 0, 31));
    logLine(b);
    return;  // nothing to apply: mm-ipod reads the switches when CarPlay connects
  }
  if (g_safe) {
    logLine("CarPlayMenu: SAFE MODE (boot.sh: 3 failed boots in a row): no switch applied; Restart to apply leaves safe mode");
    return;
  }
  snprintf(b, sizeof b, "CarPlayMenu: start: fullscreen=%d knob=%d touchpad=%d (%u mm) tap=%d two-finger=%d zoom=%d hud=%d%s%s",
           g_options[OPT_FS].on, g_options[OPT_KNOB].on, g_options[OPT_PAD].on, padMm(), g_options[OPT_TAP].on,
           g_options[OPT_TWO].on, g_options[OPT_ZOOM].on, g_options[OPT_HUD].on, g_media ? " media_gestures" : "",
           g_padTilts ? " touchpad_tilts" : "");
  logLine(b);
  if (debugLog())
    logLine("CarPlayMenu: debug_log: per-event lines on");
  hud::g_log = logLine;
  hud::g_debug = debugLog;
  if (g_options[OPT_HUD].on) {
    routeGuidanceOn();
    hud::enable(true);
  }  // the iPhone sends guidance (librghook) + drawing on the HUDTerminal (hud.h)
  if (g_options[OPT_FS].on && fsAvailable()) {
    fullscreenOn();  // the link goes away at a reboot: made again for the next HMI start
    if (g_build->fsN)
      patchWords("carplay_fullscreen (no CARPLAY_FULLSCREEN coding needed)", g_build->fs, g_build->fsN);
  } else if (g_options[OPT_FS].on)
    logLine("CarPlayMenu: fullscreen: not available on this HMI build");
  if (g_options[OPT_KNOB].on) {
    if (g_build)
      patchWords("carplay_tilt", g_build->tilt, g_build->tiltN);
    else
      logLine("CarPlayMenu: carplay_tilt: unknown HMI build");
  }
  if (touchAny())
    installTouchHook();
  if (knobDirs())
    installCtlHook();
  if (needConn())
    connOn();  // directions and zoom: Connectivity with libcppinch.so (its next start)
  iniApply();  // width, NudgeSupport, touchpad, touchscreen (Connectivity's next start)
  if (g_media && touchAny() && !g_options[OPT_TWO].on)
    hud::start();  // MOST client for the volume
}

// ---- menu handlers ------------------------------------------------------------------------------------------------------------------
bmw::PluginApp *g_app;

void setOpt(int i, bool on) {
  Option &o = g_options[i];
  o.on = on;
  char p[128];
  optPath(i, p, sizeof p);
  mkdir(kCfgDir, 0777);
  flagSet(p, on);
  g_app->updateModel(o.model, on);
}
// What applies at once: the HUD drawing and "Tap touchpad as click" / "Two fingers" with the touchpad hook already installed (it reads the
// options at every touch). After "Restart to apply": full screen (layout read at the HMI start), controller tilts (patch at the plugin's
// start), the touchpad, the sensitivity and the zoom (carplay.ini and libcppinch.so, read when Connectivity starts) and the iPhone's
// guidance (mm-ipod with librghook.so).
bool toggle(int i) {
  bool on = !g_options[i].on;
  setOpt(i, on);
  if (g_safe) {  // saved only: applied after Restart to apply, which leaves safe mode
    char b[160];
    snprintf(b, sizeof b, "CarPlayMenu: %s -> %s (safe mode: applies after restart)", g_options[i].label, on ? "ON" : "OFF");
    logLine(b);
    return true;
  }
  if (kHudOnly) {  // the HUD-only menu: mm-ipod (route guidance and drawing) reads it all when CarPlay connects
    if (i == OPT_HUD) {
      if (on)
        routeGuidanceOn();
      else
        routeGuidanceOff();
    }
    char b[160];
    snprintf(b, sizeof b, "CarPlayMenu: %s -> %s (applies at the next CarPlay connection)", g_options[i].label, on ? "ON" : "OFF");
    logLine(b);
    return true;
  }
  bool now = i == OPT_HUD;
  if (i == OPT_FS) {
    if (on && !fsAvailable()) {
      setOpt(OPT_FS, false);
      fullscreenOn();
      return true;
    }  // logs why, the switch goes back to off
    if (on)
      fullscreenOn();
    else
      fullscreenOff();
    iniApply();
  }
  if (i == OPT_HUD) {
    if (on)
      routeGuidanceOn();
    else
      routeGuidanceOff();
    hud::enable(on);
  }
  if (i == OPT_FILL || i == OPT_BLINK) {  // read when the HUD starts
    char b[160];
    snprintf(b, sizeof b, "CarPlayMenu: %s -> %s (applies after restart)", g_options[i].label, on ? "ON" : "OFF");
    logLine(b);
    return true;
  }
  if (i == OPT_KNOB || i == OPT_PAD || i == OPT_TAP || i == OPT_TWO || i == OPT_ZOOM) {
    if ((i == OPT_TAP || i == OPT_TWO) && g_touchHooked)
      now = true;
    if (touchAny())
      installTouchHook();
    if (knobDirs())
      installCtlHook();
    if (needConn())
      connOn();
    else
      shadowOff(kConnectivityShadow);  // Connectivity and carplay.ini: after the restart
    iniApply();
  }
  char b[160];
  snprintf(b, sizeof b, "CarPlayMenu: %s -> %s%s", g_options[i].label, on ? "ON" : "OFF", now ? "" : " (applies after restart)");
  logLine(b);
  return true;
}
// "Touchpad sensitivity": one of the radio buttons; the others go off (the HMI does not do it for a plugin's models)
bool setSens(int k) {
  char v[16];
  int n = snprintf(v, sizeof v, "%u", kSens[k].mm);
  mkdir(kCfgDir, 0777);
  int fd = open(kPadMm, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd >= 0) {
    long w = write(fd, v, (unsigned long)n);
    (void)w;
    close(fd);
  }
  for (int j = 0; j < 3; j++)
    g_app->updateModel(kSens[j].model, j == k);
  if (touchAny() && !g_safe)
    iniApply();  // the touchpad's size in Connectivity's carplay.ini
  char b[120];
  snprintf(b, sizeof b, "CarPlayMenu: Touchpad sensitivity -> %s (%u mm, applies after restart)", kSens[k].label, kSens[k].mm);
  logLine(b);
  return true;
}
bool setColour(int k) {
  char v[16];
  int n = snprintf(v, sizeof v, "%u", kColours[k].index);
  mkdir(kCfgDir, 0777);
  int fd = open(kHudColour, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd >= 0) {
    long w = write(fd, v, (unsigned long)n);
    (void)w;
    close(fd);
  }
  for (int j = 0; j < 3; j++)
    g_app->updateModel(kColours[j].model, j == k);
  char b[120];
  snprintf(b, sizeof b, "CarPlayMenu: Effect colour -> %s (%u, applies after restart)", kColours[k].label, kColours[k].index);
  logLine(b);
  return true;
}
bool onColOrange(void *, const bmw::Parcel &) {
  return setColour(0);
}
bool onColGrey(void *, const bmw::Parcel &) {
  return setColour(1);
}
bool onColWhite(void *, const bmw::Parcel &) {
  return setColour(2);
}
bool onFullscreen(void *, const bmw::Parcel &) {
  return toggle(OPT_FS);
}
bool onKnob(void *, const bmw::Parcel &) {
  return toggle(OPT_KNOB);
}
bool onPad(void *, const bmw::Parcel &) {
  return toggle(OPT_PAD);
}
bool onTap(void *, const bmw::Parcel &) {
  return toggle(OPT_TAP);
}
bool onTwo(void *, const bmw::Parcel &) {
  return toggle(OPT_TWO);
}
bool onZoom(void *, const bmw::Parcel &) {
  return toggle(OPT_ZOOM);
}
bool onHud(void *, const bmw::Parcel &) {
  return toggle(OPT_HUD);
}
bool onFill(void *, const bmw::Parcel &) {
  return toggle(OPT_FILL);
}
bool onBlink(void *, const bmw::Parcel &) {
  return toggle(OPT_BLINK);
}
bool onSensLow(void *, const bmw::Parcel &) {
  return setSens(0);
}
bool onSensMid(void *, const bmw::Parcel &) {
  return setSens(1);
}
bool onSensHigh(void *, const bmw::Parcel &) {
  return setSens(2);
}
// "Restart to apply": restart.sh (in the plugin folder) in a process of its own, which outlives the HMI. It makes the links again (boot.sh)
// and restarts the HMI and the CarPlay drivers through the system starter (starterPackageCtrl), which does not start them again by
// itself. Without restart.sh the HMI is stopped directly.
const char *const kRestartSh = "/var/opt/hmi/data/rw/Plugins/CarPlayMenu/restart.sh";
bool onRestart(void *, const bmw::Parcel &) {
  logLine("CarPlayMenu: Restart to apply -> restart.sh");
  unlink(kBootPending);  // a reboot asked for, not a failed boot (bootloop guard)
  if (g_safe) {
    unlink(kSafeMode);
    logLine("CarPlayMenu: leaving safe mode: the switches apply after the restart");
  }
  if (flagGet(kRestartSh) && spawnl(3 /* P_NOWAITO */, "/bin/ksh", "ksh", kRestartSh, (char *)0) != -1)
    return true;
  logLine("CarPlayMenu: restart.sh not started: stopping the HMI directly");
  flagSet(kRestart, true);
  kill(getpid(), 15);
  return true;
}

// own vtable: a copy of bmw::PluginApp's vtable with onInit replaced.
enum {
  kSlotOnInit = 22,
  kVtblSlots = 41
};  // vtable _ZTVN3bmw9PluginAppE = 0xac = 2 header + 41 slots
void *g_vtbl[2 + kVtblSlots];

bool appOnInit(bmw::PluginApp *self) {
  typedef bool (*Stub)(void *, const bmw::Parcel &);
  static const Stub stubs[OPT_N] = { onFullscreen, onKnob, onPad, onTap, onTwo, onZoom, onHud, onFill, onBlink };
  static const Stub sens[3] = { onSensLow, onSensMid, onSensHigh };
  for (int i = 0; i < OPT_N; i++) {
    if (kHudOnly && !inHudMenu(i))
      continue;  // rows the HUD-only menu does not have
    bmw::ActionDelegate d;
    d.object = self;
    d.stub = stubs[i];
    self->addHandler(g_options[i].action, d);
  }
  for (int k = 0; k < 3 && !kHudOnly; k++) {
    bmw::ActionDelegate d;
    d.object = self;
    d.stub = sens[k];
    self->addHandler(kSens[k].action, d);
  }
  static const Stub cols[3] = { onColOrange, onColGrey, onColWhite };
  for (int k = 0; k < 3; k++) {
    bmw::ActionDelegate d;
    d.object = self;
    d.stub = cols[k];
    self->addHandler(kColours[k].action, d);
  }
  if (!kHudOnly) {
    bmw::ActionDelegate r;
    r.object = self;
    r.stub = onRestart;
    self->addHandler(kActRestart, r);
  }
  bool ok = self->onInit();  // non-virtual call = the base implementation
  for (int i = 0; i < OPT_N; i++)
    if (!kHudOnly || inHudMenu(i))
      self->updateModel(g_options[i].model, g_options[i].on);
  int lv = sensLevel(padMm());
  for (int k = 0; k < 3 && !kHudOnly; k++)
    self->updateModel(kSens[k].model, k == lv);
  int cl = colourLevel();
  for (int k = 0; k < 3; k++)
    self->updateModel(kColours[k].model, k == cl);
  logLine("CarPlayMenu: onInit, handlers registered");
  return ok;
}

}  // namespace

extern "C" void *CarPlayMenu_create(bmw::IDispatcher &disp, const char *name, const bmw::Bundle &params,
                                    const bmw::Bundle &config) {
  void *obj = malloc(0x200);
  if (!obj)
    return 0;
  memset(obj, 0, 0x200);
  new (obj) bmw::PluginApp(disp, bmw::BasicString<char>(name ? name : "CarPlayMenu"), 1, 0, bmw::RHMI_INTERFACE_VERSION_1, params,
                           config);
  void **base = *reinterpret_cast<void ***>(obj);  // bmw::PluginApp's vtable (entry point)
  memcpy(g_vtbl, base - 2, sizeof(g_vtbl));        // [offset-to-top, typeinfo, 41 slots]
  g_vtbl[2 + kSlotOnInit] = reinterpret_cast<void *>(&appOnInit);
  *reinterpret_cast<void ***>(obj) = g_vtbl + 2;
  g_app = reinterpret_cast<bmw::PluginApp *>(obj);
  logLine("CarPlayMenu: create ok");
  detectBuild();
  loadOptions();
  g_safe = flagGet(kSafeMode);
  applyAtStart();
  startStableTimer();  // boot.sh's bootloop guard: boot_pending goes after kStableS
  return obj;
}
extern "C" void CarPlayMenu_destroy(void *app) {
  if (!app)
    return;
  void **vt = *reinterpret_cast<void ***>(app);  // vtable[0] = complete destructor (~D1), without free
  reinterpret_cast<void (*)(void *)>(vt[0])(app);
  free(app);
}

struct pai {
  unsigned version;
  const char *name;
  const char *vendor;
  unsigned rhmi;
  void *create;
  void *destroy;
};
__attribute__((used, visibility("default"))) struct pai pluginAppInfo = {
  1, "CarPlay Advanced Settings", "CarPlay Advanced", 1, (void *)CarPlayMenu_create, (void *)CarPlayMenu_destroy
};
