// iniwide.h — the plugin's copies of Connectivity's carplay.ini: the template (/opt/conn/data/carplay/carplay.ini, the same file in
// 006_017_006 and ID5_1703-490; patch) and Connectivity's working copy that CarPlay reads (patchConn, below). No libc. Tested on the Mac
// against the car's files.
//   wide   "Full-screen CarPlay", the size the iPhone renders: the MINI sections (750_480; 711_480 with display resolution 1) 1184 wide,
//          PhysicalWidth=194 (the stock 6.1 px/mm), and the picture drawn 1180 wide (dstw=1180 in the VideoURL), the layout's slot. The iPhone
//          declines 1180 x 480 (car, 2026-11-12: it joins the car's Wi-Fi and never offers CarPlay, also after a re-pair and with 194 mm)
//          but takes 1024, 1152 and 1184 (multiples of 16); 1184 -> 1180 is invisible. A smaller size does not scale up: 1024 x 416
//          (a "Lower resolution" try, car 2026-11-13) showed CarPlay cut off; 1200 x 480 and 1472 x 600 fill the slot whole.
//          BMW (the HMI's common layout, slot 900 of 1440 design units at x 0): the sections 900_540 (10.25" 1440 x 540) and 800_480
//          (8.8" 1280 x 480) become the whole screen, 1440 x 540 / 1280 x 480, with HMI 007_047_001's own values (its carplay.ini sections
//          1440_540 and 1280_480: PhysicalWidth 242 / 208, dstw = Width; both widths are multiples of 16).
//   nudge  knob directions: NudgeSupport=YES in [KnobInput]. libconn_carplay's knob HID (CCarPlayVirtualHID::knobUpdate,
//          CCarPlayServiceCore::getHIDDescriptor_Knob) carries X/Y only when [KnobInput] NudgeSupport is set (0x36- instead of 0x23-byte
//          descriptor, 4- instead of 2-byte report); the stock file has PrimaryInput and BackKeySupport only, so the iPhone never had
//          directions (found on the car's libconn_carplay.so, identical to 006's).
#pragma once

namespace ini {

typedef unsigned char u8;
typedef unsigned u32;

inline bool startsWith(const u8 *l, u32 n, const char *w) {
  u32 k = 0;
  while (w[k] && k < n && l[k] == (u8)w[k])
    k++;
  return !w[k];
}
inline void put(u8 *o, u32 &m, const char *s) {
  for (u32 k = 0; s[k]; k++)
    o[m++] = (u8)s[k];
}

// The full-screen values for a [Screen] section by the width it has: Width, PhysicalWidth, dstw.
struct Wide {
  const char *w, *pw, *dstw;
};
const Wide kWideMini = { "Width=1184", "PhysicalWidth=194", "dstw=1180" };  // 750_480 / 711_480
const Wide kWide1440 = { "Width=1440", "PhysicalWidth=242", "dstw=1440" };  // 900_540, BMW 10.25"
const Wide kWide1280 = { "Width=1280", "PhysicalWidth=208", "dstw=1280" };  // 800_480, BMW 8.8"
inline const Wide &wideFor(const u8 *l, u32 n) {                            // l = "Width=..."
  return startsWith(l, n, "Width=900") && n == 9 ? kWide1440 : startsWith(l, n, "Width=800") && n == 9 ? kWide1280 : kWideMini;
}
// The width of the [Screen] section whose lines start at b[i]: its first "Width=" line before the next section.
inline const Wide &sectionWide(const u8 *b, u32 n, u32 i) {
  while (i < n) {
    u32 e = i;
    while (e < n && b[e] != '\n')
      e++;
    u32 len = e > i && b[e - 1] == '\r' ? e - i - 1 : e - i;
    if (startsWith(b + i, len, "["))
      break;
    if (startsWith(b + i, len, "Width="))
      return wideFor(b + i, len);
    i = e + 1;
  }
  return kWideMini;
}

// b[0..n) -> o (needs n + 64 bytes). Returns the length written, or 0 if an asked change has no place in this file (wide: exactly the
// 6 lines of the two sections; nudge: one [KnobInput] section). bmw: the BMW sections (900_540, 800_480) instead of the MINI ones.
inline u32 patch(const u8 *b, u32 n, u8 *o, bool wide, bool nudge, bool bmw = false) {
  bool sec = false, knob = false, nudged = false;
  const Wide *v = &kWideMini;
  u32 m = 0, hits = 0, knobs = 0;
  for (u32 i = 0; i < n;) {
    u32 e = i;
    while (e < n && b[e] != '\n')
      e++;
    const u8 *l = b + i;
    u32 len = e - i;
    const char *rep = 0;
    bool skip = false;
    if (startsWith(l, len, "Option: ")) {
      sec = bmw ? startsWith(l, len, "Option: 900_540") || startsWith(l, len, "Option: 800_480")
                : startsWith(l, len, "Option: 750_480") || startsWith(l, len, "Option: 711_480");
      v = !bmw ? &kWideMini : startsWith(l, len, "Option: 900_540") ? &kWide1440 : &kWide1280;
    } else if (wide && sec && startsWith(l, len, "Width="))
      rep = v->w;
    else if (wide && sec && startsWith(l, len, "PhysicalWidth="))
      rep = v->pw;
    if (startsWith(l, len, "["))
      knob = startsWith(l, len, "[KnobInput]");
    if (nudge && knob && startsWith(l, len, "NudgeSupport="))
      skip = true;  // replaced by our line below
    if (rep) {
      put(o, m, rep);
      hits++;
    } else if (skip) {
    } else if (wide && sec && startsWith(l, len, "VideoURL=")) {  // ...&dstw=750&... -> dstw=1180
      for (u32 k = 0; k < len;) {
        if (startsWith(l + k, len - k, "dstw=")) {
          put(o, m, v->dstw);
          k += 5;
          while (k < len && l[k] >= '0' && l[k] <= '9')
            k++;
          hits++;
        } else
          o[m++] = l[k++];
      }
    } else {
      for (u32 k = 0; k < len; k++)
        o[m++] = l[k];
    }
    if (skip) {
      i = e + 1;
      continue;
    }
    if (e < n)
      o[m++] = '\n';
    if (nudge && startsWith(l, len, "[KnobInput]")) {
      put(o, m, "NudgeSupport=YES\n");
      knobs++;
      nudged = true;
    }
    i = e + 1;
  }
  if (wide && hits != 6)
    return 0;
  if (nudge && (!nudged || knobs != 1))
    return 0;
  return m;
}
// "Full-screen CarPlay" only (kept for the Mac test and older callers)
inline u32 widen(const u8 *b, u32 n, u8 *o) {
  return patch(b, n, o, true, false);
}

// Connectivity's working copy, /var/opt/conn/carplay/carplay.ini: what CarPlay actually reads (CCarplayServiceMgr::start parses it
// once, when Connectivity starts). Connectivity writes it from the template above only when coded values change ("Dipo: no values
// have changed in carplay.ini"), so in the car the template's changes never got there (2026-11-12, HMI ID5_1703-490). It has the
// chosen options only: one [Screen] section, [KnobInput], no touchpad on the MINI. Changes:
//   wide   [Screen] Width=1184, PhysicalWidth=194, dstw=1180 (as above; with 1180 the iPhone stays at "connecting"); a BMW section by
//          its width: 900 -> 1440, 800 -> 1280 (Wide above). The plugin writes this when the HMI starts, before the iPhone's first
//          connection: a [Screen] written by hand before a reboot never reaches the iPhone. For size tries, scr (the test switch
//          screen_size) replaces Width, Height, PhysicalWidth, PhysicalHeight.
//   nudge  [KnobInput] NudgeSupport=YES
//   pad    a [TouchPadInput] section (an existing one is replaced), given by the caller (kConnPad): the template's touch_pad option
//          with the HMI's own range (0..511, what Connectivity's setTouchPadEvt gets; 4096 made every slide an eighth of the pad) and
//          PrimaryInput=YES (readConfig: primaryInputDevice 2 = touchpad, after the knob's 3; with the knob primary the iPhone ignored
//          the touchpad). WidthMM/HeightMM set how far the iPhone moves per slide (car: 50 and 100 mm slow, 250 mm faster).
//   ts     "Enable rotation as zoom": a [TouchScreen] section (kConnTouch) when the file has none, without PrimaryInput (readConfig:
//          primaryInputDevice 1 only with PrimaryInput=YES; the touchpad or the knob stay the primary input). Connectivity then creates
//          CarPlay's touchscreen HID (getHIDDescriptor_Touch: X 0..[Screen] Width, Y 0..Height) for libcppinch.so's pinches. A file
//          that has one (a car with a touchscreen) keeps it.
const char kConnPad[] = "[TouchPadInput]\nName=TouchPadInput\nPrimaryInput=YES\nWidth=511\nHeight=511\nWidthMM=%u\nHeightMM=%u\n";
const char kConnTouch[] = "[TouchScreen]\nName=TouchScreen\n";
// b[0..n) -> o (needs n + 512 bytes; pad at most 200 bytes, 0 = no touchpad). Returns the length written, or 0 if an asked change has
// no place in this file.
// scr (with wide): whole lines that replace [Screen] Width=, Height=, PhysicalWidth=, PhysicalHeight= (0 = as above)
// wout (with wide): the full-screen Width line used (kWideMini / kWide1440 / kWide1280).
inline u32 patchConn(const u8 *b, u32 n, u8 *o, bool wide, bool nudge, const char *pad, bool ts = false, const char *const *scr = 0,
                     const char **wout = 0) {
  bool screen = false, knob = false, tpad = false, hasTs = false;
  const Wide *v = &kWideMini;
  u32 m = 0, hits = 0, knobs = 0;
  for (u32 i = 0; i < n;) {
    u32 e = i;
    while (e < n && b[e] != '\n')
      e++;
    const u8 *l = b + i;
    u32 len = e - i;
    const char *rep = 0;
    bool skip = false;
    if (startsWith(l, len, "[")) {
      screen = startsWith(l, len, "[Screen]");
      if (screen) {
        v = &sectionWide(b, n, e + 1);
        if (wide && wout)
          *wout = v->w;
      }
      knob = startsWith(l, len, "[KnobInput]");
      tpad = startsWith(l, len, "[TouchPadInput]");
      if (startsWith(l, len, "[TouchScreen]"))
        hasTs = true;
    }
    if (pad && tpad)
      skip = true;  // the old touchpad section: ours goes at the end
    else if (nudge && knob && startsWith(l, len, "NudgeSupport="))
      skip = true;
    else if (wide && screen && startsWith(l, len, "Width="))
      rep = scr && scr[0] ? scr[0] : v->w;
    else if (wide && screen && scr && scr[1] && startsWith(l, len, "Height=")) {
      put(o, m, scr[1]);
      o[m++] = '\n';
      i = e + 1;
      continue;
    } else if (wide && screen && startsWith(l, len, "PhysicalWidth="))
      rep = scr && scr[2] ? scr[2] : v->pw;
    else if (wide && screen && scr && scr[3] && startsWith(l, len, "PhysicalHeight=")) {
      put(o, m, scr[3]);
      o[m++] = '\n';
      i = e + 1;
      continue;
    }
    if (skip) {
      i = e + 1;
      continue;
    }
    if (rep) {
      put(o, m, rep);
      hits++;
    } else if (wide && screen && startsWith(l, len, "VideoURL=")) {
      for (u32 k = 0; k < len;) {
        if (startsWith(l + k, len - k, "dstw=")) {
          put(o, m, v->dstw);
          k += 5;
          while (k < len && l[k] >= '0' && l[k] <= '9')
            k++;
          hits++;
        } else
          o[m++] = l[k++];
      }
    } else
      for (u32 k = 0; k < len; k++)
        o[m++] = l[k];
    if (e < n)
      o[m++] = '\n';
    if (nudge && startsWith(l, len, "[KnobInput]")) {
      put(o, m, "NudgeSupport=YES\n");
      knobs++;
    }
    i = e + 1;
  }
  if (wide && hits != 3)
    return 0;
  if (nudge && knobs != 1)
    return 0;
  if (ts && !hasTs) {
    if (m && o[m - 1] != '\n')
      o[m++] = '\n';
    put(o, m, kConnTouch);
  }
  if (pad) {
    if (m && o[m - 1] != '\n')
      o[m++] = '\n';
    put(o, m, pad);
  }
  return m;
}

}  // namespace ini
