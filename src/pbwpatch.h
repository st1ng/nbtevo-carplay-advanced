// pbwpatch.h — "Full-screen CarPlay": the patch of the CarPlay layout, made on the head unit by the plugin.
// In the MINI iDrive 5 layout of HMI 006_017_006 (mini/id5/widget/generated/Spec_State_Merged.pbw), the width of the "Table" slot of state
// LT_State_CarPlay, where the CarPlay video goes, is the literal 844. It becomes "IsCarPlayFullscreen ? screenW - 112 : 844" (as in HMI
// 007_047_001), and the HMI gives the video 1180 px when CARPLAY_FULLSCREEN is coded. The file is protobuf: the operand is replaced and the
// length of every message that contains it is recomputed. No libc: memcpy only. Tested on the Mac against a reference implementation.
// BMW and the other brands use the HMI's common layout (see "BMW" below).
#pragma once

namespace pbw {

typedef unsigned char u8;
typedef unsigned u32;

// The operand "literal 844" with the context that identifies it: field 3 (operand) of vec3(844, defaultD, eParam_WidgetHeight).
// clang-format off
static const u8 kOld[] = { 0x1a, 0x0b, 0x12, 0x09, 0x0a, 0x07, 0x2a, 0x05, 0x1d, 0x00, 0x00, 0x53, 0x44,   // operand 844.0f
                           0x1a, 0x0a, 0x0a, 0x04, 0x08, 0x86, 0xb4, 0x01 };                             // next: ref defaultD (23046)
const u32 kOpOff = 2, kOpLen = 11;          // the field's value inside kOld
// sub(IsCarPlayFullscreen ? screenW - 112 : 844), name indices of 006_017_006
static const u8 kNew[] = {
  0x12,0x46,0x2a,0x44,0x08,0x12,0x10,0x0b,0x10,0x02,0x10,0x02,0x1a,0x09,0x0a,0x03,0x08,0xc3,0x11,0x12,0x02,0x20,0x00,0x1a,0x22,0x12,
  0x20,0x2a,0x1e,0x08,0x07,0x10,0x02,0x10,0x02,0x1a,0x09,0x0a,0x03,0x08,0x97,0x24,0x12,0x02,0x20,0x04,0x1a,0x0b,0x12,0x09,0x0a,0x07,
  0x2a,0x05,0x1d,0x00,0x00,0xe0,0x42,0x1a,0x0b,0x12,0x09,0x0a,0x07,0x2a,0x05,0x1d,0x00,0x00,0x53,0x44 };
// clang-format on

inline bool varint(const u8 *b, u32 n, u32 &i, u32 &v) {
  v = 0;
  for (u32 s = 0; i < n && s < 35; s += 7) {
    u8 c = b[i++];
    v |= (u32)(c & 0x7f) << s;
    if (!(c & 0x80))
      return true;
  }
  return false;
}
inline u32 putVarint(u8 *o, u32 v) {
  u32 k = 0;
  while (v >= 0x80) {
    o[k++] = (u8)(v | 0x80);
    v >>= 7;
  }
  o[k++] = (u8)v;
  return k;
}

// A value to replace: the one that takes exactly [t, t + ol) of the whole file becomes nw[0..nl).
struct Rep {
  u32 t, ol;
  const u8 *nw;
  u32 nl;
};
// Copies the message b[0..n), which starts at offset base of the file, to o, replacing the values r[0..rn) (they must not overlap) and
// fixing the lengths of the messages that contain them. Returns the size written, or 0 if the message does not decode.
inline u32 rewrite(const u8 *b, u32 n, u32 base, const Rep *r, u32 rn, u8 *o) {
  u32 i = 0, w = 0;
  while (i < n) {
    u32 f0 = i, tag, v;
    if (!varint(b, n, i, tag))
      return 0;
    switch (tag & 7) {
      case 0:
        if (!varint(b, n, i, v))
          return 0;
        break;
      case 1:
        i += 8;
        break;
      case 5:
        i += 4;
        break;
      case 2: {
        u32 len;
        if (!varint(b, n, i, len) || len > n - i)
          return 0;
        u32 s = i;
        i += len;
        const Rep *hit = 0;
        bool inside = false;
        for (u32 k = 0; k < rn; k++)
          if (r[k].t >= base + s && r[k].t + r[k].ol <= base + i) {  // a value to replace is inside this one
            inside = true;
            if (r[k].t == base + s && r[k].ol == len)
              hit = r + k;
          }
        if (inside) {
          u32 hdr = 0;
          u8 tmp[5];
          hdr = putVarint(tmp, tag);
          memcpy(o + w, tmp, hdr);
          w += hdr;
          if (hit) {
            w += putVarint(o + w, hit->nl);
            memcpy(o + w, hit->nw, hit->nl);
            w += hit->nl;
          } else {
            u8 *sub = o + w + 5;  // writes the content 5 bytes ahead and moves it after the length
            u32 sl = rewrite(b + s, len, base + s, r, rn, sub);
            if (!sl)
              return 0;
            u32 k = putVarint(tmp, sl);
            memcpy(o + w, tmp, k);
            for (u32 q = 0; q < sl; q++)
              o[w + k + q] = sub[q];
            w += k + sl;
          }
          continue;
        }
        break;
      }
      default:
        return 0;
    }
    if (i > n)
      return 0;
    memcpy(o + w, b + f0, i - f0);
    w += i - f0;
  }
  return w;
}
// One value: b[t, t + ol) -> nw[0..nl).
inline u32 rewriteAt(const u8 *b, u32 n, u32 t, u32 ol, const u8 *nw, u32 nl, u8 *o) {
  Rep r = { t, ol, nw, nl };
  return rewrite(b, n, 0, &r, 1, o);
}

// Finds the operand (exactly once); out needs n + 64 bytes. Returns the new size, 0 if it is not the expected layout (or already changed).
inline u32 patch(const u8 *b, u32 n, u8 *out) {
  u32 hit = 0, cnt = 0;
  for (u32 i = 0; i + sizeof kOld <= n; i++) {
    u32 k = 0;
    while (k < sizeof kOld && b[i + k] == kOld[k])
      k++;
    if (k == sizeof kOld) {
      hit = i + kOpOff;
      cnt++;
    }
  }
  if (cnt != 1)
    return 0;
  u32 r = rewriteAt(b, n, hit, kOpLen, kNew, sizeof kNew, out);
  return (r == n - kOpLen + sizeof kNew) ? r : 0;
}

// MINI iDrive 5 layout of HMI ID5_1703-490 Build 139: the names are in the file itself (strings, not indices), and CCarPlayActivity has
// no IsCarPlayFullscreen. The "Table" slot of LT_State_CarPlay is vec3(844, defaultD, eParam_WidgetHeight); with the option on the width
// becomes the literal 1328 (screenW 1440 - 2 * 56, the 1180 px of 007_047_001 at x 49), always: the file is only linked with the option on.
// Same size (a float in place), no length changes. Tested against the car's file.
// clang-format off
static const u8 kOld1703[] = {
  0x08,0x15,0x10,0x02,0x10,0x02,0x10,0x02,                                    // vec3(float, float, float)
  0x1a,0x0b,0x12,0x09,0x0a,0x07,0x2a,0x05,0x1d, 0x00,0x00,0x53,0x44,           // literal 844.0f
  0x1a,0x10,0x0a,0x0a,0x12,0x08,'d','e','f','a','u','l','t','D',0x12,0x02,0x20,0x04,
  0x1a,0x1b,0x0a,0x15,0x12,0x13,'e','P','a','r','a','m','_','W','i','d','g','e','t','H','e','i','g','h','t',0x12,0x02,0x20,0x01 };
const u32 kWidthOff1703 = 17;
static const u8 kWide1703[4] = { 0x00, 0x00, 0xa6, 0x44 };                    // 1328.0f

// The side panel ("Apple CarPlay / iPhone", widget ContentPreview of LT_State_CarPlay) sits at x = smallScreenW, inside the wide slot.
// 007 hides it in code when CARPLAY_FULLSCREEN is coded; this HMI has no such flag, so the layout moves it off the screen: the operand
// smallScreenW of its position vec3(smallScreenW, 0, contentPreviewMB) becomes the literal 2000 (the design screen is 1440 wide).
static const u8 kPanel1703[] = {
  0x08,0x15,0x10,0x02,0x10,0x02,0x10,0x02,                                    // vec3(float, float, float)
  0x1a,0x14,0x0a,0x0e,0x12,0x0c,'s','m','a','l','l','S','c','r','e','e','n','W',0x12,0x02,0x20,0x04,   // ref smallScreenW (constant)
  0x1a,0x0b,0x12,0x09,0x0a,0x07,0x2a,0x05,0x1d,0x00,0x00,0x00,0x00 };          // literal 0
const u32 kPanelOff1703 = 10, kPanelLen1703 = 20;                             // the operand's value
static const u8 kPanelNew1703[] = { 0x12,0x09,0x0a,0x07,0x2a,0x05,0x1d, 0x00,0x00,0xfa,0x44 };   // literal 2000.0f
// clang-format on

inline u32 findOnce(const u8 *b, u32 n, const u8 *p, u32 pn) {
  u32 hit = 0, cnt = 0;
  for (u32 i = 0; i + pn <= n; i++) {
    u32 k = 0;
    while (k < pn && b[i + k] == p[k])
      k++;
    if (k == pn) {
      hit = i;
      cnt++;
    }
  }
  return cnt == 1 ? hit + 1 : 0;  // position + 1, 0 = not exactly once
}
// out needs n + 64 bytes. Width (one byte) and the side panel's position; 0 if either place is not found exactly once.
inline u32 patchFixed(const u8 *b, u32 n, u8 *out) {
  u32 w = findOnce(b, n, kOld1703, sizeof kOld1703), p = findOnce(b, n, kPanel1703, sizeof kPanel1703);
  if (!w || !p)
    return 0;
  u32 r = rewriteAt(b, n, p - 1 + kPanelOff1703, kPanelLen1703, kPanelNew1703, sizeof kPanelNew1703, out);
  if (r != n - kPanelLen1703 + sizeof kPanelNew1703)
    return 0;
  u32 wo = w - 1 + kWidthOff1703;
  if (wo > p - 1)
    wo -= kPanelLen1703 - sizeof kPanelNew1703;  // the width comes after the panel?
  if (out[wo] != 0x00 || out[wo + 2] != 0x53 || out[wo + 3] != 0x44)
    return 0;
  memcpy(out + wo, kWide1703, 4);
  return r;
}

// ---- BMW ------------------------------------------------------------------------------------------------------------------------
// The brand packages of BMW (bmw/), BMW i (bmwi/) and Rolls-Royce (rr/) have no LT_State_CarPlay of their own for iDrive 5 (bmw/id5
// has no Spec_State_Merged.pbw at all; rr/id5 has one without it), so the HMI uses the state in its common layout,
// /opt/hmi/ID5/data/ro/common/widget/generated/Spec_State_Merged.pbw (HMI package). Its design screen is 1440 x 540 (common
// constregistry: screenW 1440, smallScreenW 900; the 10.25" 1440 x 540 screen 1:1, the 8.8" 1280 x 480 at 8/9). The CarPlay "Table"
// slot is at x 0, and the side panel (ContentPreview, 540 wide) at x smallScreenW, shown while the tablet is wider than
// smallScreenW. (A third constant group, the small screen, has screenW = smallScreenW = 972: no side panel there, and the changes
// below change nothing on it.)

// The common LT_State_CarPlay state, by its header (field 1: the name): exactly one top-level state starts with hdr. Its message is
// b[s..e). The file is a list of field-1 messages.
inline bool stateRange(const u8 *b, u32 n, const u8 *hdr, u32 hn, u32 &s, u32 &e) {
  u32 i = 0, cnt = 0;
  while (i < n) {
    u32 tag, len;
    if (!varint(b, n, i, tag) || tag != 0x0a || !varint(b, n, i, len) || len > n - i)
      return false;
    u32 k = 0;
    while (k < hn && k < len && b[i + k] == hdr[k])
      k++;
    if (k == hn) {
      s = i;
      e = i + len;
      cnt++;
    }
    i += len;
  }
  return cnt == 1;
}

// HMI 006_017_006: the common slot is already "vec3(IsCarPlayFullscreen ? screenW : smallScreenW, defaultD, eParam_WidgetHeight)"
// (as in 007_047_001), and the side panel is hidden with IsCarPlayFullscreen. The flag the plugin forces on (kFsCode in cpm.cpp) is
// all it takes: 1440 design units, the whole screen. Nothing is written; this only checks that the layout is that one (name indices
// of 006_017_006).
// clang-format off
static const u8 kState006[] = { 0x0a,0x04,0x08,0xa2,0xd9,0x01 };                 // LT_State_CarPlay (27810)
static const u8 kSlot006[] = {
  0x08,0x15,0x10,0x02,0x10,0x02,0x10,0x02,                                      // vec3(float, float, float)
  0x1a,0x2e,0x12,0x2c,0x2a,0x2a,0x08,0x12,0x10,0x0b,0x10,0x02,0x10,0x02,           //   select(bool, float, float)
  0x1a,0x09,0x0a,0x03,0x08,0xc3,0x11,0x12,0x02,0x20,0x00,                         //     IsCarPlayFullscreen (2243, own)
  0x1a,0x09,0x0a,0x03,0x08,0x97,0x24,0x12,0x02,0x20,0x04,                         //     screenW (4631, constant)
  0x1a,0x0a,0x0a,0x04,0x08,0x86,0xc4,0x01,0x12,0x02,0x20,0x04,                    //     smallScreenW (25094, constant)
  0x1a,0x0a,0x0a,0x04,0x08,0x86,0xb4,0x01,0x12,0x02,0x20,0x04,                    //   defaultD (23046, constant)
  0x1a,0x09,0x0a,0x03,0x08,0x81,0x22,0x12,0x02,0x20,0x01 };                      //   eParam_WidgetHeight (4353, own)
// clang-format on
inline bool hasFullscreen006(const u8 *b, u32 n) {
  u32 s, e;
  return stateRange(b, n, kState006, sizeof kState006, s, e) && findOnce(b + s, e - s, kSlot006, sizeof kSlot006);
}

// HMI ID5_1703-490 Build 139: names in strings, no IsCarPlayFullscreen. The slot is vec3(smallScreenW, defaultD,
// eParam_WidgetHeight) at x 0; its width becomes the constant screenW (1440: the whole screen; the 972 small screen keeps its
// width). The side panel is moved off the screen as on the MINI (kPanel1703: x smallScreenW -> 2000). Both places must be found
// exactly once inside LT_State_CarPlay.
// clang-format off
static const u8 kState1703[] = { 0x0a,0x12,0x12,0x10,'L','T','_','S','t','a','t','e','_','C','a','r','P','l','a','y' };
static const u8 kSlotCommon1703[] = {
  0x08,0x15,0x10,0x02,0x10,0x02,0x10,0x02,                                      // vec3(float, float, float)
  0x1a,0x14,0x0a,0x0e,0x12,0x0c,'s','m','a','l','l','S','c','r','e','e','n','W',0x12,0x02,0x20,0x04,   // ref smallScreenW (constant)
  0x1a,0x10,0x0a,0x0a,0x12,0x08,'d','e','f','a','u','l','t','D',0x12,0x02,0x20,0x04,
  0x1a,0x1b,0x0a,0x15,0x12,0x13,'e','P','a','r','a','m','_','W','i','d','g','e','t','H','e','i','g','h','t',0x12,0x02,0x20,0x01 };
const u32 kSlotOff1703 = 10, kSlotLen1703 = 20;                               // the width operand's value
static const u8 kScreenW1703[] = { 0x0a,0x09,0x12,0x07,'s','c','r','e','e','n','W',0x12,0x02,0x20,0x04 };   // ref screenW (constant)
// clang-format on
// out needs n + 64 bytes. Returns the new size (n - 14), 0 if it is not the expected layout (or already changed).
inline u32 patchCommon1703(const u8 *b, u32 n, u8 *out) {
  u32 s, e;
  if (!stateRange(b, n, kState1703, sizeof kState1703, s, e))
    return 0;
  u32 w = findOnce(b + s, e - s, kSlotCommon1703, sizeof kSlotCommon1703),
      p = findOnce(b + s, e - s, kPanel1703, sizeof kPanel1703);
  if (!w || !p)
    return 0;
  Rep r[2] = { { s + w - 1 + kSlotOff1703, kSlotLen1703, kScreenW1703, sizeof kScreenW1703 },
               { s + p - 1 + kPanelOff1703, kPanelLen1703, kPanelNew1703, sizeof kPanelNew1703 } };
  u32 m = rewrite(b, n, 0, r, 2, out);
  return m == n - kSlotLen1703 + sizeof kScreenW1703 - kPanelLen1703 + sizeof kPanelNew1703 ? m : 0;
}

}  // namespace pbw
