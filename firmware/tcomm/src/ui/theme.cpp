#include "theme.h"

namespace theme {

using tc::Mode;

// Spec section 6, by mode: bg, fg, wx, ink. Evening is day x 0.86 (bg) and 0.94 (fg); night bg is
// day x 0.5, its weather zone x 0.3, and all its text #E4D8C0. The spec's values, not computed, so
// they match the boards to the digit.
static const StatusColors kStatus[3][6] = {
    {
        {hex(0x1F6B3A), hex(0xFBF1DC), hex(0xCFE4C4), hex(0x173A22)},  // OPEN
        {hex(0xF2A71B), hex(0x3A1F2B), hex(0xF8DFA0), hex(0x4A3305)},  // BUSY (plum text for contrast)
        {hex(0xB8442E), hex(0xFBF1DC), hex(0xEEC5B6), hex(0x4D1F13)},  // PRIVATE
        {hex(0x5A4A8C), hex(0xFBF1DC), hex(0xD6CFEA), hex(0x2A2650)},  // SLEEPING
        {hex(0x66705C), hex(0xFBF1DC), hex(0xDAD9C8), hex(0x2F3328)},  // AWAY
        {hex(0x49535B), hex(0xFBF1DC), hex(0xD3D8DE), hex(0x262C33)},  // NO SIGNAL
    },
    {
        {hex(0x1B5C32), hex(0xECE3CF), hex(0xB2C4A9), hex(0x173A22)},
        {hex(0xD09017), hex(0x371D28), hex(0xD5C08A), hex(0x4A3305)},
        {hex(0x9E3A28), hex(0xECE3CF), hex(0xCDA99D), hex(0x4D1F13)},
        {hex(0x4D4078), hex(0xECE3CF), hex(0xB8B2C9), hex(0x2A2650)},
        {hex(0x58604F), hex(0xECE3CF), hex(0xBBBBAC), hex(0x2F3328)},
        {hex(0x3F474E), hex(0xECE3CF), hex(0xB5BABF), hex(0x262C33)},
    },
    {
        {hex(0x10361D), hex(0xE4D8C0), hex(0x092011), hex(0xE4D8C0)},
        {hex(0x79540E), hex(0xE4D8C0), hex(0x493208), hex(0xE4D8C0)},
        {hex(0x5C2217), hex(0xE4D8C0), hex(0x37140E), hex(0xE4D8C0)},
        {hex(0x2D2546), hex(0xE4D8C0), hex(0x1B162A), hex(0xE4D8C0)},
        {hex(0x33382E), hex(0xE4D8C0), hex(0x1F221C), hex(0xE4D8C0)},
        {hex(0x242A2E), hex(0xE4D8C0), hex(0x16191B), hex(0xE4D8C0)},
    },
};

// outbg is the Outside tile's sky. Lighter by day and evening than the boards' EAD9B8 and D0BF9E,
// so it looks less hazy through the tile's 90 percent; lighter again later that day, still short of
// white (Mike, 2026-10-31).
static const Palette kPalettes[3] = {
    // paper      gold         outbg        dbg          dring        dogc         gearbd
    {hex(0xF7EAD2), hex(0xF6C24E), hex(0xF8EFDA), hex(0x3A1F2B), hex(0x6B4655), hex(0x3F7C85), hex(0x1F6B3A),
     // cream     plum         outtx        outside_sub  humidity     dfg          gearink      dog text     heat
     hex(0xFBF1DC), hex(0x3A1F2B), hex(0x3A1F2B), hex(0x6B4655), hex(0x3A1F2B), hex(0xFBF1DC), hex(0x3A1F2B), hex(0xFFFFFF),
     hex(0xC8441C), hex(0xE0483C)},
    {hex(0xE0D2B6), hex(0xE8B847), hex(0xE1D3B6), hex(0x33202A), hex(0x5E3E4C), hex(0x386E76), hex(0x1B5C33),
     hex(0xF2E7CE), hex(0x3A1F2B), hex(0x3A1F2B), hex(0x5E3E4C), hex(0x3A1F2B), hex(0xF2E7CE), hex(0x3A1F2B), hex(0xFFFFFF),
     hex(0xC0401A), hex(0xD8443A)},
    {hex(0x1B2620), hex(0xC9A460), hex(0x18262A), hex(0x2A1A22), hex(0x4A3440), hex(0x2D5A62), hex(0x2F5A3A),
     hex(0xE4D8C0), hex(0xE4D8C0), hex(0xE4D8C0), hex(0x9AA89A), hex(0xE8C9A0), hex(0xE4D8C0), hex(0xE4D8C0), hex(0xC7C1A9),
     hex(0xE07A42), hex(0xC9584A)},
};

// Date strip colours by phase: night, dawn, morning, midday, golden, dusk.
static const SkyColors kSky[6] = {{hex(0x16222C)}, {hex(0x6A3252)}, {hex(0x1B5183)},
                                  {hex(0x174876)}, {hex(0x8A3A22)}, {hex(0x4E2F5C)}};

StatusColors status_colors(int i, Mode m) {
  if (i < 0 || i > 5) i = kNoSignal;
  int mi = (int)m;
  if (mi < 0 || mi > 2) mi = 0;
  return kStatus[mi][i];
}

const Palette &palette(Mode m) { return kPalettes[(int)m]; }
const SkyColors &sky(tc::Phase p) { return kSky[(int)p]; }

Color sheet_bg(Mode m) { return palette(m).paper; }

// Calendar roll-down spec, section 3: day, evening, night.
static const CalColors kCal[3] = {
    // paper ink on-ink muted field accent accent-text accent-fill badge-plus cream danger
    {hex(0xF7EAD2), hex(0x3A1F2B), hex(0xFBF1DC), hex(0x6B4655), hex(0xFFF9EC), hex(0x1F6B3A), hex(0x154D29),
     hex(0x1F6B3A), hex(0xFFFFFF), hex(0xFBF1DC), hex(0xB8442E),
     // up-fill up-ink down-fill down-ink down-edge key key-special keyboard-bg; opacities
     hex(0xFFF9EC), hex(0x3A1F2B), hex(0x3A1F2B), hex(0xFBF1DC), hex(0x3A1F2B), hex(0xFFF9EC), hex(0xD9C7A3),
     hex(0x3A1F2B), 240, 51, 14},
    {hex(0xE0D2B6), hex(0x3A1F2B), hex(0xF2E7CE), hex(0x5E382B), hex(0xF6EEDB), hex(0x1B5C33), hex(0x154D29),
     hex(0x1B5C33), hex(0xFFFFFF), hex(0xF2E7CE), hex(0x8F3322),
     hex(0xF6EEDB), hex(0x3A1F2B), hex(0x33202A), hex(0xF2E7CE), hex(0x33202A), hex(0xF6EEDB), hex(0xC9B68F),
     hex(0x33202A), 240, 56, 15},
    {hex(0x1B2620), hex(0xE4D8C0), hex(0x1B2620), hex(0xAAB7AA), hex(0x243229), hex(0x5FB07C), hex(0x7CC795),
     hex(0x2F6B45), hex(0x0E1612), hex(0xE4D8C0), hex(0xE58A76),
     hex(0xD7CBB0), hex(0x14201A), hex(0x0E1612), hex(0xE4D8C0), hex(0xE4D8C0), hex(0x24342B), hex(0x17211C),
     hex(0x080C0A), 242, 51, 18},
};

const CalColors &cal_colors(Mode m) { return kCal[(unsigned)m > 2 ? 0 : (unsigned)m]; }

Color settings_tile(int i, Mode m) {
  // F11 gives each tile its own flat color; reuse the status and accent colors in turn.
  static const uint32_t kTiles[] = {0x2F6C7C, 0x5A4A8C, 0x1F6B3A, 0xB8442E, 0x66705C, 0x3F7C85, 0x8A3A22, 0x4E2F5C};
  Color c = hex(kTiles[i % 8]);
  if (m == Mode::Evening) c = gfx::scale(c, 0.86f);
  if (m == Mode::Night) c = gfx::scale(c, 0.5f);
  return c;
}

}  // namespace theme
