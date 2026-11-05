// The touch reveal. See reveal.h.
#include "reveal.h"

#include <math.h>
#include <string.h>

namespace ss {

namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The ring over columns [x0, x1) of row y: full colour from the circle's edge
// to kRevealRing out, anti-aliased on its outer edge only. Its inner side meets
// pixels the next step copies, so a soft inner edge would leave a tint behind.
void ringSpan(Pixel* row, int x0, int x1, float dy, const RevealState& rv, float r, Pixel ring) {
  const float outer = r + kRevealRing + 0.5f;
  for (int x = x0; x < x1; ++x) {
    const float dx = (float)x + 0.5f - rv.cx;
    const float d = sqrtf(dx * dx + dy * dy);
    if (d < r) continue;
    const float a = outer - d;
    if (a <= 0.0f) continue;
    row[x] = a >= 1.0f ? ring : blend565(row[x], ring, (uint8_t)(a * 255.0f + 0.5f));
  }
}

}  // namespace

void revealBegin(RevealState& rv, int x, int y) {
  rv.active = true;
  rv.done = false;
  rv.cx = (float)clampi(x, 0, SS_WIDTH - 1) + 0.5f;
  rv.cy = (float)clampi(y, 0, SS_HEIGHT - 1) + 0.5f;
  rv.t = 0.0f;
  // The farthest corner, plus the ring and a pixel, so the ring leaves the frame.
  const float fx = rv.cx > 0.5f * SS_WIDTH ? rv.cx : (float)SS_WIDTH - rv.cx;
  const float fy = rv.cy > 0.5f * SS_HEIGHT ? rv.cy : (float)SS_HEIGHT - rv.cy;
  rv.r_max = sqrtf(fx * fx + fy * fy) + kRevealRing + 1.0f;
  for (int i = 0; i < SS_HEIGHT; ++i) {
    rv.lo[i] = 0;
    rv.hi[i] = 0;
  }
}

bool revealStep(RevealState& rv, Surface& fb, const Pixel* main, Pixel ring, float dt) {
  if (!rv.active || main == nullptr || fb.pixels == nullptr) return false;
  if (rv.done) return true;
  rv.t += dt;
  if (rv.t >= kRevealSeconds) {
    for (int y = 0; y < SS_HEIGHT; ++y)
      memcpy(fb.row(y), main + (size_t)y * SS_WIDTH, (size_t)SS_WIDTH * sizeof(Pixel));
    rv.done = true;
    return true;
  }
  const float k = 1.0f - rv.t / kRevealSeconds;
  const float r = rv.r_max * (1.0f - k * k * k);   // ease-out cubic
  const float outer = r + kRevealRing + 0.5f;
  const int y0 = clampi((int)floorf(rv.cy - outer), 0, SS_HEIGHT);
  const int y1 = clampi((int)ceilf(rv.cy + outer) + 1, 0, SS_HEIGHT);
  for (int y = y0; y < y1; ++y) {
    const float dy = (float)y + 0.5f - rv.cy;
    Pixel* row = fb.row(y);
    const Pixel* src = main + (size_t)y * SS_WIDTH;
    int lo = rv.lo[y], hi = rv.hi[y];
    // Pixel centres inside r show the main screen; copy only the new ones.
    const float h2 = r * r - dy * dy;
    if (h2 > 0.0f) {
      const float h = sqrtf(h2);
      const int nlo = clampi((int)ceilf(rv.cx - h - 0.5f), 0, SS_WIDTH);
      const int nhi = clampi((int)floorf(rv.cx + h - 0.5f) + 1, 0, SS_WIDTH);
      if (nlo < nhi) {
        if (lo >= hi) {
          memcpy(row + nlo, src + nlo, (size_t)(nhi - nlo) * sizeof(Pixel));
          lo = nlo;
          hi = nhi;
        } else {
          if (nlo < lo) memcpy(row + nlo, src + nlo, (size_t)(lo - nlo) * sizeof(Pixel));
          if (nhi > hi) memcpy(row + hi, src + hi, (size_t)(nhi - hi) * sizeof(Pixel));
          if (nlo < lo) lo = nlo;
          if (nhi > hi) hi = nhi;
        }
        rv.lo[y] = (int16_t)lo;
        rv.hi[y] = (int16_t)hi;
      }
    }
    // The ring, on either side of the copied columns.
    const float g2 = outer * outer - dy * dy;
    if (g2 <= 0.0f) continue;
    const float g = sqrtf(g2);
    const int x0 = clampi((int)floorf(rv.cx - g - 0.5f), 0, SS_WIDTH);
    const int x1 = clampi((int)ceilf(rv.cx + g - 0.5f) + 1, 0, SS_WIDTH);
    if (lo >= hi) {
      ringSpan(row, x0, x1, dy, rv, r, ring);
    } else {
      ringSpan(row, x0, lo < x1 ? lo : x1, dy, rv, r, ring);
      ringSpan(row, hi > x0 ? hi : x0, x1, dy, rv, r, ring);
    }
  }
  return false;
}

}  // namespace ss
