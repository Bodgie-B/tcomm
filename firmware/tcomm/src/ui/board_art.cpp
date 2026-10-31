// The Arduino build uses -Os; the artwork is drawn pixel by pixel on every redraw.
#pragma GCC optimize("O2")
#include "board_art.h"
#include <stdlib.h>
#include "board_art_data.h"

namespace boardart {

using gfx::Canvas;
using gfx::Color;
using gfx::Pt;

Color colour(const Shape &s, const Paint &p) {
  switch (s.role) {
    case kZoneW:
    case kZoneS: {
      Color base = s.role == kZoneW ? p.zone_w : p.zone_s;
      float v = s.v[p.cls];
      if (s.ops & (1 << p.cls)) return gfx::mix(base, gfx::hex(0xFFFFFF), v);
      return gfx::scale(base, v);
    }
    case kFixed: return gfx::hex(s.rgb[0]);
    case kPerMode: return gfx::hex(s.rgb[p.mode < 0 || p.mode > 2 ? 0 : p.mode]);
    case kStripK: return gfx::scale(p.strip, s.v[0]);
    case kFg: return p.fg;
    case kBg: return p.bg;
  }
  return p.fg;
}

void draw_shape(Canvas &c, const Shape &s, Color col, float dx, float dy) {
  switch (s.kind) {
    case kDisc: c.fill_circle(s.cx + dx, s.cy + dy, s.r, col); return;
    case kRing: c.ring(s.cx + dx, s.cy + dy, s.r + s.w * 0.5f, s.r - s.w * 0.5f, col); return;
    case kFill:
    case kStroke: break;
  }
  int total = 0;
  for (int k = 0; k < s.nsub; k++) total += kSubs[s.sub0 + k].npt;
  // From the heap: the art task and the interface task both draw, so no shared buffer.
  Pt *pts = total ? (Pt *)malloc(sizeof(Pt) * total) : nullptr;
  if (!pts) return;
  int lens[32], n = 0, rings = 0;
  for (int k = 0; k < s.nsub && rings < 32; k++) {
    const Sub &sb = kSubs[s.sub0 + k];
    for (int j = 0; j < sb.npt; j++) {
      const int16_t *q = kPts + 2 * (sb.pt0 + j);
      pts[n + j] = Pt{q[0] / 16.0f + dx, q[1] / 16.0f + dy};
    }
    if (s.kind == kStroke) {  // round caps and round joins: a capsule per segment
      for (int j = 1; j < sb.npt; j++) c.line(pts[n + j - 1].x, pts[n + j - 1].y, pts[n + j].x, pts[n + j].y, s.w, col);
      if (sb.npt == 1) c.fill_circle(pts[n].x, pts[n].y, s.w * 0.5f, col);
    } else {
      lens[rings++] = sb.npt;
      n += sb.npt;
    }
  }
  if (s.kind == kFill) c.fill_path(pts, lens, rings, col);
  free(pts);
}

void draw(Canvas &c, const Group &g, const Paint &p, float dx, float dy, const gfx::RRect *outer,
          const gfx::RRect *status_zone) {
  for (int i = 0; i < g.n; i++) {
    const Shape &s = g.shapes[i];
    c.set_clip(s.clip ? status_zone : outer);
    draw_shape(c, s, colour(s, p), dx, dy);
  }
  c.set_clip(nullptr);
}

}  // namespace boardart
