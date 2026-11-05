// Status chip. See chip.h.
//
// Drawing:
//   - The body is two capsules (strokeCapsule): the outline color at the full
//     size, the fill color inside it, 1.5 px smaller all round.
//   - The word is the panel's Jost glyphs (chip_font.h, cut from the firmware's
//     LVGL font by tools/make_chip_font.py), their 4-bit alpha blended over the
//     fill with blend565. Laid out as lv_font_fmt_txt does: whole-pixel
//     advances with the font's kerning, so it reads like the panel's tiles.
//   - A dog badge is a circle in the outline color and one inside it in a
//     neutral dark, with the silhouette on top in the cream.
//   - A pulse ring is a thin capsule-shaped band at distance r from the body,
//     one polygon with a hole (winding cancels inside it), drawn with a global
//     opacity so it fades. It goes under the body, so a ring at r = 0 is hidden.
//
// Night mode crossfades every color from the panel's day row to its night row.
// The look is what draws: chipUpdate quantizes the animation into ChipLook and
// chipDraw reads only that.
#include "chip.h"
#include "chip_font.h"
#include "../anim/ease.h"

#include <string.h>

namespace ss {

namespace {

// The panel's own status tile colors (firmware/tcomm/src/ui/theme.cpp kStatus,
// the GUI spec to the digit): the tile's bg for the fill, its fg for the word.
// Two rows, day (mode 0) and night (mode 2); night mode crossfades from one to
// the other. Order as Inputs::other_status.
constexpr Pixel kFillDay[kChipStatusCount] = {
    rgb565_hex(0x1F6B3A),   // Open
    rgb565_hex(0xF2A71B),   // Busy
    rgb565_hex(0xB8442E),   // Private
    rgb565_hex(0x5A4A8C),   // Sleeping
    rgb565_hex(0x66705C),   // Away
    rgb565_hex(0x49535B),   // No signal
};
constexpr Pixel kFillNight[kChipStatusCount] = {
    rgb565_hex(0x10361D), rgb565_hex(0x79540E), rgb565_hex(0x5C2217),
    rgb565_hex(0x2D2546), rgb565_hex(0x33382E), rgb565_hex(0x242A2E),
};
constexpr Pixel kInkDay = rgb565_hex(0xFBF1DC);     // cream: the day word, the dogs, the glow's target
constexpr Pixel kInkNight = rgb565_hex(0xE4D8C0);   // the night word (the panel: no white at night)
constexpr Pixel kLabelDay[kChipStatusCount] = {     // Busy's is plum: cream on amber has too little contrast
    kInkDay, rgb565_hex(0x3A1F2B), kInkDay, kInkDay, kInkDay, kInkDay,
};

// The panel's status words, case and spelling (controller.cpp status_word, and
// the No signal tile's "NO SIGNAL"). A word with a letter missing from
// chip_font.h draws without it; tests/test_chip.cpp checks none is.
const char* const kWord[kChipStatusCount] = {"OPEN", "BUSY", "PRIVATE", "SLEEPING", "AWAY", "NO SIGNAL"};

// The dog badge's fill: neutral dark by day, darker at night, so the cream dog reads on both.
constexpr Pixel kBadgeDay = rgb565_hex(0x2A2E2C);
constexpr Pixel kBadgeNight = rgb565_hex(0x171A19);

// Timing and shape. PROPOSED, like the constants in chip.h.
constexpr float kFadeSeconds = 0.6f;       // status color crossfade
constexpr float kNightPerSecond = 0.5f;    // day row to night row and back in 2 s
constexpr float kPopSeconds = 0.3f;        // a badge grows to full size
constexpr float kPopFrom = 0.3f;           // ... from this fraction
constexpr float kOutlineWidth = 1.5f;      // the thin outline, px
constexpr float kRingHalfWidth = 1.0f;     // the pulse ring is 2 px thick
constexpr float kRingAlpha = 0.8f;         // opacity of a ring as it leaves the chip
constexpr float kGlowBoost = 0.22f;        // fill moves this far toward the cream at a pulse peak
constexpr float kGlowAttack = 0.08f;       // fraction of a pulse period the glow takes to peak
constexpr int kRingSegments = 12;          // per end cap of a ring outline (UNVERIFIED how it looks)
constexpr float kPi = 3.14159265f;

constexpr float kChipCy = (float)kChipY0 + 0.5f * (float)kChipH;
constexpr float kBadgeR = 0.5f * (float)kChipBadgeD;

inline int statusIndex(int s) { return (s >= 0 && s < kChipStatusCount) ? s : kChipNoSignal; }

inline uint8_t q8(float v) { return (uint8_t)(clamp01(v) * 255.f + 0.5f); }

inline Rect unite(const Rect& a, const Rect& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return Rect{a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0,
              a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1};
}

inline int dogSlots(uint8_t dogs) { return (dogs & 1) + ((dogs >> 1) & 1); }

// The day-row color toward the night-row one: `night` 0 = day .. 255 = night.
inline Pixel byNight(Pixel day, Pixel night, uint8_t t) {
  if (t == 0) return day;
  if (t == 255) return night;
  return mixRGB(day, night, (float)t * (1.0f / 255.0f));
}

// ---- the word ---------------------------------------------------------------

const ChipGlyph* findGlyph(char ch) {
  for (int i = 0; i < kChipGlyphCount; ++i) {
    if (kChipGlyphs[i].ch == ch) return &kChipGlyphs[i];
  }
  return nullptr;
}

int kernBetween(char l, char r) {   // 1/16 px
  for (int i = 0; i < kChipKernCount; ++i) {
    if (kChipKerns[i].left == l && kChipKerns[i].right == r) return kChipKerns[i].value;
  }
  return 0;
}

// Whole-pixel advance of glyph i of `word` (lv_font_fmt_txt: the advance plus the
// kerning with the next letter, rounded).
int advanceAt(const char* word, int i) {
  const ChipGlyph* g = findGlyph(word[i]);
  if (g == nullptr) return 0;
  int a = g->adv;
  if (word[i + 1] != '\0') a += kernBetween(word[i], word[i + 1]);
  return (a + 8) >> 4;
}

// Ink extent of a word relative to its first pen position.
void wordInk(const char* word, int* ink_l, int* ink_r) {
  int pen = 0, l = 0, r = 0;
  bool any = false;
  for (int i = 0; word[i] != '\0'; ++i) {
    const ChipGlyph* g = findGlyph(word[i]);
    if (g != nullptr && g->w > 0) {
      const int gl = pen + g->ofs_x, gr = gl + g->w;
      if (!any || gl < l) l = gl;
      if (!any || gr > r) r = gr;
      any = true;
    }
    pen += advanceAt(word, i);
  }
  *ink_l = l;
  *ink_r = r;
}

// Blend one glyph's 4-bit alpha into the surface, inside `clip` only.
void blitGlyph(Surface& s, const ChipGlyph& g, int left, int top, Pixel color, const Rect& clip) {
  const Rect c = clip.clipped(s.bounds());
  for (int gy = 0; gy < g.h; ++gy) {
    const int y = top + gy;
    if (y < c.y0 || y >= c.y1) continue;
    Pixel* row = s.row(y);
    for (int gx = 0; gx < g.w; ++gx) {
      const int x = left + gx;
      if (x < c.x0 || x >= c.x1) continue;
      const int i = gy * g.w + gx;   // nibble index into this glyph's run
      const uint8_t b = kChipBitmaps[g.bitmap + (i >> 1)];
      const uint8_t a4 = (i & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)(b >> 4);
      if (a4 != 0) row[x] = blend565(row[x], color, (uint8_t)(a4 * 17));
    }
  }
}

void drawWord(Surface& s, const char* word, Pixel color, const Rect& clip) {
  int ink_l, ink_r;
  wordInk(word, &ink_l, &ink_r);
  int pen = kChipX0 + (kChipW - (ink_r - ink_l)) / 2 - ink_l;
  const int baseline = kChipY0 + (kChipH - kChipFontCapHeight) / 2 + kChipFontCapHeight;
  for (int i = 0; word[i] != '\0'; ++i) {
    const ChipGlyph* g = findGlyph(word[i]);
    if (g != nullptr && g->w > 0) blitGlyph(s, *g, pen + g->ofs_x, baseline - g->ofs_y - g->h, color, clip);
    pen += advanceAt(word, i);
  }
}

// ---- the dog ----------------------------------------------------------------

// Local coordinates are pixels at full badge size, origin at the badge center,
// the dog facing left (toward the scene).
struct Xf {
  float cx, cy, k;
  Vec2 at(float x, float y) const { return Vec2{cx + k * x, cy + k * y}; }
};

// Side-on silhouette from primitives: body ellipse, neck, head, snout, ears,
// tail and four legs. The ears are the one difference between the dogs: Lucy's
// stand up, Copper's hang (and so vanish into the head's outline). UNVERIFIED:
// whether that matches the real dogs, and how it reads on the panel.
void drawDog(Surface& s, const Xf& t, Pixel col, bool copper, const Rect& clip) {
  fillEllipse(s, t.at(1.5f, 0.6f), 7.8f * t.k, 4.3f * t.k, col, clip);                         // body
  strokeCapsule(s, t.at(-4.5f, -0.5f), t.at(-7.5f, -4.2f), 4.2f * t.k, col, clip);            // neck
  fillCircle(s, t.at(-8.3f, -5.0f), 3.3f * t.k, col, clip);                                    // head
  strokeCapsule(s, t.at(-9.5f, -4.2f), t.at(-12.6f, -3.6f), 2.6f * t.k, col, clip);           // snout
  if (!copper) {
    const Vec2 ear[3] = {t.at(-9.6f, -6.6f), t.at(-6.2f, -7.6f), t.at(-7.4f, -11.6f)};      // upright ear
    fillPolygon(s, ear, 3, col, clip);
  }
  strokeCapsule(s, t.at(8.0f, -0.6f), t.at(11.4f, copper ? 2.4f : -4.8f), 2.0f * t.k, col, clip);   // tail
  static const float kLegX[4] = {-4.6f, -2.0f, 5.0f, 7.6f};
  for (int i = 0; i < 4; ++i) {                                                                // legs
    strokeCapsule(s, t.at(kLegX[i], 3.0f), t.at(kLegX[i] + 0.3f, 8.4f), 2.4f * t.k, col, clip);
  }
}

void drawBadge(Surface& s, float cx, uint8_t pop, bool copper, Pixel outline, Pixel ink, Pixel fill,
               const Rect& clip) {
  const float k = (float)pop * (1.0f / 255.0f);
  if (!(k > 0.f)) return;
  const Vec2 c{cx, kChipCy};
  fillCircle(s, c, kBadgeR * k, outline, clip);
  fillCircle(s, c, (kBadgeR - kOutlineWidth) * k, fill, clip);
  drawDog(s, Xf{cx + 1.0f * k, kChipCy + 0.6f * k, k}, ink, copper, clip);
}

// ---- the pulse ring ---------------------------------------------------------

// Closed outline of a capsule (centers (ax, cy) and (bx, cy), radius r): the
// right cap top to bottom, then the left cap bottom to top, `n` segments each;
// clockwise on screen. 2 * (n + 1) points.
int capsulePath(Vec2* out, float ax, float bx, float cy, float r, int n) {
  int m = 0;
  for (int i = 0; i <= n; ++i) {
    const float turns = -0.25f + 0.5f * (float)i / (float)n;
    out[m++] = Vec2{bx + r * cosT(turns), cy + r * sinT(turns)};
  }
  for (int i = 0; i <= n; ++i) {
    const float turns = 0.25f + 0.5f * (float)i / (float)n;
    out[m++] = Vec2{ax + r * cosT(turns), cy + r * sinT(turns)};
  }
  return m;
}

// A band: the outer capsule clockwise, then the inner one counter-clockwise.
// Each path is closed by repeating its first point; the two joining edges are
// the same segment both ways, so they cancel and the winding inside is zero.
// 4 * (n + 1) + 2 points: 54 at n = 12, under SS_MAX_POLY_POINTS.
void drawRing(Surface& s, float r_out, Pixel color, uint8_t alpha, const Rect& clip) {
  const float ax = (float)kChipX0 + 0.5f * (float)kChipH, bx = (float)(kChipX0 + kChipW) - 0.5f * (float)kChipH;
  const float rc = 0.5f * (float)kChipH + r_out;   // radius of the ring's center line
  Vec2 pts[SS_MAX_POLY_POINTS];
  Vec2 path[2 * (kRingSegments + 1)];
  int n = capsulePath(pts, ax, bx, kChipCy, rc + kRingHalfWidth, kRingSegments);
  pts[n++] = pts[0];
  const int m = capsulePath(path, ax, bx, kChipCy, rc - kRingHalfWidth, kRingSegments);
  pts[n++] = path[0];
  for (int i = m - 1; i >= 0; --i) pts[n++] = path[i];
  fillPolygonAlpha(s, pts, n, color, alpha, clip);
}

}  // namespace

// ---- public -------------------------------------------------------------------

void chipInit(ChipState& c) {
  memset(&c, 0, sizeof(c));
  c.look.status = c.look.from = (uint8_t)kChipNoSignal;
  c.look.mix = 255;
  // night and night_f start at 0: the day row
  c.mix_t = 1.f;
  c.pulse_t = -1.f;
}

const char* chipLabel(int status) { return kWord[statusIndex(status)]; }

int chipLabelWidth(int status) {
  int l, r;
  wordInk(chipLabel(status), &l, &r);
  return r - l;
}

Rect chipBounds(const ChipState& c) {
  const ChipLook& L = c.look;
  Rect r{kChipX0, kChipY0, kChipX0 + kChipW, kChipY0 + kChipH};
  if (L.ring_a != 0) {
    const int g = (L.ring_r + 3) / 4 + 2;   // the ring's distance, its half width and its edge pixel
    r = Rect{r.x0 - g, r.y0 - g, r.x1 + g, r.y1 + g};
  }
  const int n = dogSlots(L.dogs);
  if (n > 0) {
    const int x0 = kChipX0 - kChipBadgeGap - n * kChipBadgeD - (n - 1) * kChipBadgeStep;
    if (x0 < r.x0) r.x0 = x0;
  }
  return r;
}

void chipDoorbell(ChipState& c) { c.pulse_t = 0.f; }

Rect chipUpdate(ChipState& c, int status, int dog_out, bool night, float dt) {
  const Rect before = chipBounds(c);
  const ChipLook old = c.look;
  if (!(dt > 0.f)) dt = 0.f;
  if (dt > 0.25f) dt = 0.25f;
  const bool first = !c.primed;
  c.primed = true;
  ChipLook& L = c.look;

  // Status: crossfade the color; the word switches at the halfway point.
  const uint8_t st = (uint8_t)statusIndex(status);
  if (first) {
    L.status = L.from = st;
    c.mix_t = 1.f;
  } else if (st != L.status) {
    L.from = L.status;   // a change in the middle of a fade restarts from the old target (a small jump)
    L.status = st;
    c.mix_t = 0.f;
  }
  c.mix_t = clamp01(c.mix_t + dt * (1.0f / kFadeSeconds));
  L.mix = q8(easeInOut(c.mix_t));

  // Night: crossfade from the day row of colors to the night row; the first call sets it.
  const float target = night ? 1.f : 0.f;
  const float step = dt * kNightPerSecond;
  if (first) {
    c.night_f = target;
  } else if (c.night_f < target) {
    c.night_f = c.night_f + step > target ? target : c.night_f + step;
  } else if (c.night_f > target) {
    c.night_f = c.night_f - step < target ? target : c.night_f - step;
  }
  L.night = q8(c.night_f);

  // Dogs: a badge pops in when its dog goes out and vanishes when it comes in.
  const uint8_t dogs = (uint8_t)(dog_out > 0 ? (dog_out & 3) : 0);
  for (int i = 0; i < 2; ++i) {
    if ((dogs >> i) & 1) {
      if (first) c.pop_f[i] = 1.f;
      else if (!((L.dogs >> i) & 1)) c.pop_f[i] = 0.f;
      c.pop_f[i] = clamp01(c.pop_f[i] + dt * (1.0f / kPopSeconds));
      L.pop[i] = q8(kPopFrom + (1.f - kPopFrom) * easeOutCubic(c.pop_f[i]));
    } else {
      c.pop_f[i] = 0.f;
      L.pop[i] = 0;
    }
  }
  L.dogs = dogs;

  // Pulse: kChipPulseCount rings in kChipPulseSeconds. Each period a ring grows
  // from the chip's edge and fades, and the chip brightens to a peak at its start.
  L.glow = 0;
  L.ring_r = 0;
  L.ring_a = 0;
  if (c.pulse_t >= 0.f) {
    if (c.pulse_t >= kChipPulseSeconds) {
      c.pulse_t = -1.f;
    } else {
      const float period = kChipPulseSeconds / (float)kChipPulseCount;
      float u = c.pulse_t / period;
      u -= (float)(int)u;
      float g;   // quick rise to the peak, then a cubic fall
      if (u < kGlowAttack) {
        g = u / kGlowAttack;
      } else {
        const float f = (1.f - u) / (1.f - kGlowAttack);
        g = f * f * f;
      }
      L.glow = q8(g);
      L.ring_a = q8(kRingAlpha * (1.f - u) * (1.f - u));
      if (L.ring_a != 0) L.ring_r = (uint8_t)(4.f * (float)kChipRingGrow * easeOutCubic(u) + 0.5f);
      c.pulse_t += dt;
    }
  }

  if (memcmp(&old, &L, sizeof(ChipLook)) == 0) return Rect{0, 0, 0, 0};
  return unite(before, chipBounds(c));
}

void chipDraw(const ChipState& c, Surface& strip, const Rect& clip, const Palette& pal) {
  const ChipLook& L = c.look;
  const Rect cl = clip.clipped(strip.bounds()).clipped(chipBounds(c));
  if (cl.empty()) return;
  const uint8_t nt = L.night;
  const int shown = L.mix >= 128 ? L.status : L.from;   // the word, and the color of its letters

  // The fill: the fading status's colors for this moment between the day and night rows.
  Pixel fill = byNight(kFillDay[L.status], kFillNight[L.status], nt);
  if (L.mix < 255) {
    fill = mixRGB(byNight(kFillDay[L.from], kFillNight[L.from], nt), fill, (float)L.mix * (1.0f / 255.0f));
  }
  const Pixel ink = byNight(kInkDay, kInkNight, nt);
  const Pixel ring_color = fill;   // the status bg, before the pulse's brightening
  if (L.glow != 0) fill = mixRGB(fill, ink, kGlowBoost * (float)L.glow * (1.0f / 255.0f));
  const Pixel outline = pal[CHIP_OUTLINE];
  // The word's color flips at the halfway point of the night fade instead of blending: plum Busy letters
  // and the cream ones would cross the fill's own color on the way and vanish.
  const Pixel label = nt < 128 ? kLabelDay[shown] : kInkNight;

  // Under everything: the pulse ring.
  if (L.ring_a != 0) {
    drawRing(strip, 0.25f * (float)L.ring_r, ring_color, L.ring_a, cl);
  }

  // The body.
  const float ax = (float)kChipX0 + 0.5f * (float)kChipH, bx = (float)(kChipX0 + kChipW) - 0.5f * (float)kChipH;
  strokeCapsule(strip, Vec2{ax, kChipCy}, Vec2{bx, kChipCy}, (float)kChipH, outline, cl);
  strokeCapsule(strip, Vec2{ax, kChipCy}, Vec2{bx, kChipCy}, (float)kChipH - 2.f * kOutlineWidth, fill, cl);
  drawWord(strip, kWord[shown], label, cl);

  // The badges, Lucy nearest the chip. The dog is the cream, never the (plum) Busy letters.
  const Pixel badge_fill = byNight(kBadgeDay, kBadgeNight, nt);
  int slot = 0;
  for (int i = 0; i < 2; ++i) {
    if (!((L.dogs >> i) & 1)) continue;
    const float cx = (float)(kChipX0 - kChipBadgeGap) - kBadgeR - (float)(slot * (kChipBadgeD + kChipBadgeStep));
    drawBadge(strip, cx, L.pop[i], i == 1, outline, ink, badge_fill, cl);
    ++slot;
  }
}

}  // namespace ss
