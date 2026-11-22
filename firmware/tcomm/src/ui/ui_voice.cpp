// Voice notes on the panel (docs/voice-notes/DESIGN.md 2; Mike, 2026-11-20): what the person sees.
// The Controller does the work (Actions::voice_*), View::voice says what to show; this file holds no
// voice state beyond what is on screen. Three pieces:
//   - the recorder (Sheet::Voice): a LONG press on the own floor's badge opens a row across the own
//     tile for a message to the other floor (Mike, 2026-11-21): the timer and level left of where the
//     badge was, START there (then SEND over CANCEL), the text box with TRY AGAIN and the keyboard
//     button right; nothing records until START, and the 3 s count from START's press;
//   - the keyboard's voice strip: the blue mic key records into the focused field, the countdown
//     covering all four rows of keys while it records (Mike, 2026-11-21), then a strip over the top
//     row only while the Pi works; the text goes in through the owner's on_char;
//   - the arrival dialog: the oldest message in the inbox, over the SENDER's tile below the top 90 px
//     of its status zone, under the doorbell banner, never while a sheet is open, dismissed by one tap
//     after a 700 ms guard. Mike, 2026-11-21: a light card with a header strip in the sender's colour
//     and a gold rim that pulses for its first 3.2 s.
// The status TEXT left of the badge still opens the tile message editor (ui.cpp, unchanged).
//
// LVGL rule kept here: an event handler never deletes the object it is running for or its parent.
// Handlers change state and hide things; the rebuilds happen in voice_render (the render loop).
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "../core/text_edit.h"
#include "../core/voice_proto.h"
#include "ss_host.h"
#include "ui_internal.h"

namespace ui {
namespace detail {

using gfx::Color;
using gfx::hex;
using tc::VoiceErr;
using tc::VoicePurpose;
using tc::VoiceUi;
using tc::VoiceView;

namespace {

// ---- Timings (lv_tick_get ms) ----
// Check fix C2: the dim overlay's first draw blocks LVGL for 0.3-0.6 s, so a quick tap's release is
// only seen after it. While the finger is down LVGL sends PRESSING on every touch read (30 ms); a
// release seen more than this after the last one came after a stall, and is judged by that last
// moment the finger was seen down, so a tap whose release was hidden by the draw is still a tap.
constexpr uint32_t kSettleMs = 100;
constexpr uint32_t kGraceMs = 1000;     // a job just pressed may not be in the view yet
constexpr uint32_t kErrMs = 4000;       // an error on the strip, then the plain keyboard
constexpr uint32_t kPartMs = 3000;      // "Only part of it fit"
constexpr uint32_t kGuardMs = 700;      // the arrival dialog ignores taps this long after it shows
constexpr uint32_t kIdleCloseMs = 60000;  // the result screen, like the other sheets
constexpr uint32_t kDotMs = 300;        // the stepping dots
constexpr uint32_t kToastMs = 1500;     // "Delivered"
constexpr uint32_t kFadeMs = 200;       // the arrival dialog's fade, like the banner
constexpr uint32_t kPulseMs = 400;      // the arrival dialog's rim: on and off every 400 ms ...
constexpr int kPulseSteps = 8;          // ... for 3.2 s, then off

// ---- Colours (DESIGN 2.1) ----
Color stop_red() { return hex(g_mode == tc::Mode::Night ? 0x8E1F1F : 0xC62828); }
Color mic_blue(tc::Mode m) { return hex(m == tc::Mode::Night ? 0x2A56A8 : 0x1F5FBF); }
Color accent() { return hex(g_mode == tc::Mode::Night ? 0x1B5C33 : 0x1F6B3A); }  // the note editor's SEND
Color sheet_col() { return theme::sheet_bg(g_mode); }
Color muted() { return gfx::mix(sheet_col(), ink(), 0.62f); }

const VoiceView g_none = {};  // read-only: with the constants (PSRAM on the panel), not in internal RAM
const VoiceView &vv() { return g_f.view ? g_f.view->voice : g_none; }
uint32_t now() { return lv_tick_get(); }
uint32_t since(uint32_t t) { return lv_tick_elaps(t); }
uint32_t g_mono = 0;  // the frame's mono ms (View::voice.press_ms is on that clock)

tc::Floor other_floor() { return g_set.floor == tc::Floor::Upstairs ? tc::Floor::Downstairs : tc::Floor::Upstairs; }
const char *other_word() { return tc::floor_word(other_floor()); }

const char *err_text(VoiceErr e, char *buf, size_t n) {
  switch (e) {
    case VoiceErr::NoPi: return "The Pi didn't answer";
    case VoiceErr::GatewayOld: return "Gateway needs an update";
    case VoiceErr::SendFailed: return "Couldn't send the recording";
    case VoiceErr::Lost: return "Lost part of the recording, try again";
    case VoiceErr::Nothing: return "Didn't hear anything";
    case VoiceErr::NotCaught: return "Didn't catch that";
    case VoiceErr::NoMemory: return "Not enough memory to record";
    case VoiceErr::PeerNoAnswer: snprintf(buf, n, "%s didn't answer", other_word()); return buf;
    case VoiceErr::Bell: return "Recording stopped for the bell";
    case VoiceErr::Busy: return "The Pi is busy, try again";
    case VoiceErr::None: break;
  }
  return "Couldn't start the recording";
}

// Milliseconds left of the 3 s cap, counted from the press (owner default, kVoiceRecMs). Before the
// view has the job, the whole 3 s.
uint32_t rec_left(const VoiceView &v) {
  if (v.ui != VoiceUi::Recording) return tc::kVoiceRecMs;
  uint32_t el = g_mono - v.press_ms;
  return el >= tc::kVoiceRecMs ? 0 : tc::kVoiceRecMs - el;
}
int countdown_digit(uint32_t left) {
  int d = (int)((left + 999) / 1000);
  return d < 1 ? 1 : d;
}

// The screensaver exists on the panel only (ss_host.cpp; the simulator never starts it).
#if defined(ESP_PLATFORM)
bool saver_running() { return screensaver_running(); }
#else
bool saver_running() { return false; }
#endif

bool inside(lv_obj_t *o, lv_obj_t *ancestor) {
  for (; o; o = lv_obj_get_parent(o))
    if (o == ancestor) return true;
  return false;
}

int wrapped_h(const char *t, const lv_font_t *f, int w) {
  lv_point_t p;
  lv_txt_get_size(&p, t, f, 0, 0, (lv_coord_t)w, LV_TEXT_FLAG_NONE);
  return p.y;
}

// The press that started a job: the hold rule (DESIGN 2.1 and 2.2). Released more than 0.6 s after
// the press = hold-to-talk, the release ends it; sooner = tap mode. Check fix C2: see kSettleMs.
struct Press {
  bool live = false;
  uint32_t at = 0;
  uint32_t last_down = 0;  // the last PRESSING: the finger was surely down then
};
void press_start(Press &p) {
  p.live = true;
  p.at = p.last_down = now();
}
void press_still(Press &p) {
  if (p.live) p.last_down = now();
}
bool is_hold(const Press &p) {
  const uint32_t t = now();
  const uint32_t released = t - p.last_down <= kSettleMs ? t : p.last_down;  // after a stall: the last sure moment
  return released - p.at > tc::kVoiceHoldMs;
}

// A thin bar: the countdown's drain and the level.
struct Bar {
  lv_obj_t *fg = nullptr;
  int w = 0, shown = -1;
};
void bar_build(Bar &b, lv_obj_t *parent, int x, int y, int w, int h, Color bg, Color fg) {
  lv_obj_t *track = box(parent, x, y, w, h, bg, h / 2);
  b.fg = box(track, 0, 0, w, h, fg, h / 2);
  b.w = w;
  b.shown = -1;
}
void bar_set(Bar &b, int px) {
  if (!b.fg) return;
  if (px < 0) px = 0;
  if (px > b.w) px = b.w;
  if (px == b.shown) return;
  if (px > 0) lv_obj_set_width(b.fg, (lv_coord_t)px);
  show(b.fg, px > 0);
  b.shown = px;
}
// The level bar in 4 px steps, so a small change redraws nothing.
void level_set(Bar &b, uint8_t level) { bar_set(b, (level > 100 ? 100 : level) * b.w / 100 / 4 * 4); }

// Three dots stepping every 300 ms (30 x 8 px: cheap to redraw).
struct Dots {
  lv_obj_t *d[3] = {};
  int step = -1;
};
void dots_build(Dots &ds, lv_obj_t *parent, int x, int y) {
  for (int i = 0; i < 3; i++) ds.d[i] = box(parent, x + i * 11, y, 8, 8, muted(), 4);
  ds.step = -1;
}
void dots_tick(Dots &ds) {
  if (!ds.d[0]) return;
  int s = (int)(now() / kDotMs % 3);
  if (s == ds.step) return;
  for (int i = 0; i < 3; i++) lv_obj_set_style_bg_color(ds.d[i], C(i == s ? ink() : gfx::mix(sheet_col(), ink(), 0.3f)), 0);
  ds.step = s;
}

// ---- "Delivered" ----
lv_obj_t *g_toast = nullptr;
void toast_end(lv_timer_t *) {
  if (g_toast) lv_obj_del(g_toast);
  g_toast = nullptr;
}
void toast(const char *text) {
  const theme::Palette &pal = theme::palette(g_mode);
  if (g_toast) lv_obj_del(g_toast);
  int w = text_width(text, B20) + 44;
  g_toast = box(lv_layer_top(), 400 - w / 2, 80, w, 44, pal.doorbell, 22);
  label(g_toast, B20, pal.cream, 22, 11, text);
  if (g_banner) lv_obj_move_foreground(g_banner);
  lv_timer_t *t = lv_timer_create(toast_end, kToastMs, nullptr);
  lv_timer_set_repeat_count(t, 1);
}

// =============================================================================================
// The keyboard's voice strip (DESIGN 2.1)

enum class Ss : uint8_t { None, Rec, Work, Wait, Msg };
struct Strip {
  Keypad *kp = nullptr;      // the keyboard it belongs to
  lv_obj_t *parent = nullptr;  // kp->obj when it was made (a new keyboard object means a new strip)
  lv_obj_t *root = nullptr;  // over the keys (kFull* or kSmall*); it lives and dies with the keyboard
  Ss shown = Ss::None;
  bool job = false;          // a job from this keyboard is in the Controller, not yet done or cancelled
  uint16_t seq = 0;          // View::voice.text_seq at the press: a different one is this job's result
  uint32_t acted_at = 0;
  char msg[64] = "";
  uint32_t msg_at = 0, msg_ms = 0;
  Press press;
  lv_obj_t *digit = nullptr;
  Bar drain, level;
  Dots dots;
  int digit_shown = -1;
};
Strip &st = ui_keep<Strip>();  // PSRAM on the panel (field fix 2026-11-20)

// Mike, 2026-11-21: while recording the strip covers all four rows of keys (nothing else to press
// then but STOP and CANCEL); afterwards (working, waiting, a message) it covers the top row only and the
// rest of the keyboard shows again.
constexpr int kStripX = 8, kStripW = 784;
constexpr int kFullY = 8, kFullH = 4 * kKeyH + 3 * kKeyGap;  // 247: the four rows of keys
constexpr int kSmallY = 4, kSmallH = kKeyH + 8;              // 66: over the top row

// The mic key's look: blue with the white microphone, or red with a white stop square while recording.
void mic_look(Keypad &kp, bool recording) {
  if (kp.mic < 0 || kp.mic >= kp.nkeys) return;
  lv_obj_t *b = kp.keys[kp.mic].obj;
  if (!b) return;
  Color face = recording ? stop_red() : mic_blue(kp.mode);
  lv_color_t want = C(face);
  if (lv_obj_get_style_bg_color(b, 0).full != want.full) {
    lv_obj_set_style_bg_color(b, want, 0);
    lv_obj_set_style_bg_color(b, C(gfx::scale(face, 0.88f)), LV_STATE_PRESSED);
  }
  if (lv_obj_get_child_cnt(b) >= 2) {
    show(lv_obj_get_child(b, 0), !recording);
    show(lv_obj_get_child(b, 1), recording);
  }
}

// While a job runs every key but the mic key stops taking taps (no redraw: only the flag changes).
void keys_lock(Keypad &kp, bool lock) {
  for (int i = 0; i < kp.nkeys; i++) {
    if (i == kp.mic || !kp.keys[i].obj) continue;
    if (lock) lv_obj_clear_flag(kp.keys[i].obj, LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_add_flag(kp.keys[i].obj, LV_OBJ_FLAG_CLICKABLE);
  }
}

bool strip_live() { return st.root && st.kp && st.kp->obj == st.parent; }

// Hides the strip and gives the keys back. Safe inside an event: nothing is deleted.
void strip_hide() {
  if (st.root) show(st.root, false);
  if (strip_live()) {
    keys_lock(*st.kp, false);
    mic_look(*st.kp, false);
  }
  st.shown = Ss::None;
}

void strip_deleted(lv_event_t *e) {
  if (lv_event_get_target(e) != st.root) return;  // an old strip, already forgotten
  st.root = nullptr;
  st.parent = nullptr;
  st.kp = nullptr;
  st.shown = Ss::None;
  st.digit = nullptr;
  st.drain = Bar{};
  st.level = Bar{};
  st.dots = Dots{};
  if (st.job) {  // the keyboard went (its sheet or editor closed): the job goes with it
    st.job = false;
    if (g_act) g_act->voice_cancel();
  }
}

void strip_stop(lv_event_t *) {
  if (st.job && g_act) g_act->voice_stop();
}
void strip_cancel(lv_event_t *) {
  if (st.job && g_act) g_act->voice_cancel();
  st.job = false;
  st.press.live = false;
  strip_hide();
  lv_disp_trig_activity(nullptr);
}
void strip_tap(lv_event_t *) {  // a tap on a message takes it away at once
  if (st.shown == Ss::Msg) strip_hide();
}

// The strip for kp, made the first time this keyboard's mic key is pressed.
bool strip_ensure(Keypad &kp) {
  if (strip_live() && st.kp == &kp) return true;
  if (st.root) {  // another keyboard's strip (hidden under a keyboard that slid away): forget it
    lv_obj_t *old = st.root;
    st.root = nullptr;
    lv_obj_del(old);  // never this key's own ancestor, so safe here
  }
  if (!kp.obj) return false;
  const theme::CalColors &k = theme::cal_colors(kp.mode);
  st.root = box(kp.obj, kStripX, kSmallY, kStripW, kSmallH, k.key, 9);
  lv_obj_add_flag(st.root, LV_OBJ_FLAG_CLICKABLE);  // swallow taps meant for the keys under it
  lv_obj_add_event_cb(st.root, strip_deleted, LV_EVENT_DELETE, nullptr);
  lv_obj_add_event_cb(st.root, strip_tap, LV_EVENT_CLICKED, nullptr);
  show(st.root, false);
  st.kp = &kp;
  st.parent = kp.obj;
  st.shown = Ss::None;
  return true;
}

// Shows the strip in state s (s None hides it). Never from inside one of the strip's own events.
void strip_show(Ss s) {
  if (!strip_live()) return;
  Keypad &kp = *st.kp;
  const theme::CalColors &k = theme::cal_colors(kp.mode);
  lv_obj_clean(st.root);
  st.digit = nullptr;
  st.drain = Bar{};
  st.level = Bar{};
  st.dots = Dots{};
  st.digit_shown = -1;
  st.shown = s;
  lv_disp_trig_activity(nullptr);  // the note editor's 60 s idle close must not fire mid-job
  if (s == Ss::None) {
    strip_hide();
    return;
  }
  const Color face = k.key, ink_c = k.ink, dim = gfx::mix(face, ink_c, 0.25f);
  const Color red = stop_red(), cream = hex(0xFBF1DC);
  const int lh20 = (int)lv_font_get_line_height(B20);
  const bool full = s == Ss::Rec;
  lv_obj_set_pos(st.root, kStripX, full ? kFullY : kSmallY);
  lv_obj_set_size(st.root, kStripW, full ? kFullH : kSmallH);
  switch (s) {
    case Ss::Rec: {
      // The whole keypad: "Speak now", the big digit, the bar draining over 3 s, the microphone's
      // level, and STOP over CANCEL at the right, both key-sized or bigger.
      label(st.root, J28, ink_c, 24, 16, "Speak now");
      const int lh = (int)lv_font_get_line_height(J76N);  // digits only
      st.digit = label(st.root, J76N, ink_c, 12, 124 - lh / 2, "3");
      lv_obj_set_width(st.digit, 76);
      lv_obj_set_style_text_align(st.digit, LV_TEXT_ALIGN_CENTER, 0);
      bar_build(st.drain, st.root, 100, 88, 456, 14, dim, ink_c);
      label(st.root, B14, k.muted, 100, 140, "MICROPHONE");
      bar_build(st.level, st.root, 100, 162, 456, 14, dim, mic_blue(kp.mode));
      label(st.root, B16, k.muted, 24, 206, "STOP when you're done; the words go in at the cursor");
      button(st.root, 584, 16, 184, 124, red, cream, "STOP", J28, strip_stop, nullptr);
      button(st.root, 584, 152, 184, 80, k.key_special, ink_c, "CANCEL", J24, strip_cancel, nullptr);
      break;
    }
    case Ss::Work:
    case Ss::Wait: {
      const char *t = s == Ss::Work ? "Turning speech into text" : "Waiting for the other floor's message";
      label(st.root, B20, ink_c, 16, (kSmallH - lh20) / 2, t);
      dots_build(st.dots, st.root, 16 + text_width(t, B20) + 12, kSmallH / 2 - 3);
      button(st.root, 684, (kSmallH - 48) / 2, 92, 48, k.key_special, ink_c, "CANCEL", B16, strip_cancel, nullptr);
      break;
    }
    case Ss::Msg:
      label(st.root, B20, ink_c, 16, (kSmallH - lh20) / 2, st.msg);
      break;
    case Ss::None: break;
  }
  show(st.root, true);
  keys_lock(kp, s != Ss::Msg);
  mic_look(kp, s == Ss::Rec);
}

void strip_message(const char *text, uint32_t ms) {
  snprintf(st.msg, sizeof(st.msg), "%s", text);
  st.msg_at = now();
  st.msg_ms = ms;
  strip_show(Ss::Msg);
}

// One character through the owner, with the keyboard's shift rule (a capital to start a title).
bool type_char(Keypad &kp, char ch) {
  if (kp.shift && !kp.sym && isalpha((unsigned char)ch)) ch = (char)toupper((unsigned char)ch);
  if (!kp.on_char || !kp.on_char(ch)) return false;
  if (kp.shift && isalpha((unsigned char)ch)) {
    kp.shift = false;
    keypad_refresh(kp);
  }
  return true;
}

// The result goes in at the field's cursor (tap to place the cursor, DESIGN 8: the owner registers
// its tc::TextEdit, found by the text on_mic returns; an unregistered field takes it at the end): a
// space before it unless it is at the start or after a space (a space already after the cursor is
// stepped over instead), and one after it when a letter or digit follows; the first capital lowered
// when it is only a sentence start and the cursor is mid-sentence (check finding 2); each character
// through on_char, stopping at the first refusal, which is never silent.
void strip_insert(const VoiceView &v) {
  st.job = false;
  Keypad &kp = *st.kp;
  const char *field = kp.on_mic ? kp.on_mic() : nullptr;
  if (g_act) g_act->voice_done();
  if (!field || !kp.on_char) {
    strip_show(Ss::None);
    return;
  }
  char t[tc::kVoiceTextMax + 1];
  size_t o = 0;
  for (size_t i = 0; v.text[i] && o < sizeof(t) - 1; i++) {  // no doubled spaces (the note refuses them)
    if (v.text[i] == ' ' && (o == 0 || t[o - 1] == ' ')) continue;
    t[o++] = v.text[i];
  }
  while (o && t[o - 1] == ' ') o--;
  t[o] = 0;
  tc::TextEdit *te = tc::te_find(field);
  if (te) tc::te_set(*te, te->cursor);
  const int cursor = te ? te->cursor : (int)strlen(field);
  tc::voice_apply_capital(t, field, cursor, v.text_flags);
  bool all = t[0] != 0;
  if (all && cursor > 0 && field[cursor - 1] != ' ') {
    if (te && field[cursor] == ' ') tc::te_set(*te, cursor + 1);  // the space is there already
    else all = type_char(kp, ' ');
  }
  for (size_t i = 0; all && t[i]; i++) all = type_char(kp, t[i]);
  if (all && te && isalnum((unsigned char)field[te->cursor])) type_char(kp, ' ');  // a word follows
  if (!all && t[0]) strip_message("Only part of it fit", kPartMs);
  else strip_show(Ss::None);
}

void mic_press(Keypad &kp) {
  if (!g_act || st.job || !kp.on_mic || !kp.on_mic()) return;  // no field takes text now
  const VoiceView &v = vv();
  if (v.ui == VoiceUi::Result || v.ui == VoiceUi::Error || v.ui == VoiceUi::Delivered) g_act->voice_done();
  else if (v.ui != VoiceUi::Idle) return;  // one voice job per panel
  if (!strip_ensure(kp)) return;
  st.seq = v.text_seq;
  st.acted_at = now();
  if (!g_act->voice_press(VoicePurpose::Dictation)) {
    char buf[48];
    strip_message(err_text(vv().err, buf, sizeof(buf)), kErrMs);
    return;
  }
  st.job = true;
  press_start(st.press);
  strip_show(Ss::Rec);
}

void strip_tick(const tc::View &view) {
  if (!strip_live()) return;
  const VoiceView &v = view.voice;
  Ss want = st.shown;
  if (st.job) {
    const bool mine = v.purpose == VoicePurpose::Dictation;
    if (mine && v.ui == VoiceUi::Recording) want = Ss::Rec;
    else if (mine && v.ui == VoiceUi::Working) want = Ss::Work;
    else if (mine && v.ui == VoiceUi::Waiting) want = Ss::Wait;
    else if (mine && v.ui == VoiceUi::Result && v.text_seq != st.seq) {
      strip_insert(v);
      return;
    } else if (mine && v.ui == VoiceUi::Error) {
      st.job = false;
      if (g_act) g_act->voice_done();
      char buf[48];
      strip_message(err_text(v.err, buf, sizeof(buf)), kErrMs);
      return;
    } else if (since(st.acted_at) > kGraceMs) {  // the job is gone (another one took over, a restart)
      st.job = false;
      want = Ss::None;
    }
  } else if (st.shown != Ss::Msg) {
    want = Ss::None;
  }
  if (st.shown == Ss::Msg && since(st.msg_at) >= st.msg_ms) want = Ss::None;
  if (want != st.shown) strip_show(want);
  if (st.shown == Ss::Rec) {
    uint32_t left = rec_left(v);
    int d = countdown_digit(left);
    if (st.digit && d != st.digit_shown) {
      char b[12];
      snprintf(b, sizeof(b), "%d", d);
      set_text(st.digit, b);
      st.digit_shown = d;
    }
    bar_set(st.drain, (int)(left / 100) * st.drain.w / (int)(tc::kVoiceRecMs / 100));  // 10 Hz
    level_set(st.level, v.level);
  } else if (st.shown == Ss::Work || st.shown == Ss::Wait) {
    dots_tick(st.dots);
  }
}

// =============================================================================================
// The recorder (Sheet::Voice, DESIGN 2.2; Mike, 2026-11-21: a row across the own tile)
//
// A LONG press on the own badge opens it; nothing records until START. One card across the screen
// over the own tile, its parts lined up on the badge so the finger never covers what moves:
//   left    the timer (3.0 s, tenths) and its bar, full until START; the microphone level under it;
//   centre  where the badge was: START (blue, the microphone) that becomes STOP (red) while
//           recording; after it SEND over CANCEL;
//   right   the text box the words arrive in ("Turning speech into text" while the Pi works), TRY
//           AGAIN and the keyboard button (a keyboard drawn on it, Mike 2026-11-21: the edit card with
//           the keyboard and its blue mic key).
// CLOSE at the top right leaves at any time.

enum class Rs : uint8_t { None, Ready, Rec, Work, Wait, Result, Edit, Sending, Error };
constexpr int kTileUpY = 74, kTileDownY = 279;  // the floor tiles' screen y (x 12)
constexpr int kRowX = 8, kRowW = 784, kRowH = 224;
constexpr int kLeftX = 20, kLeftW = 232;
// The badge is tile x 287-386, y 97-193; the tile is at screen x 12, so card x 291-390 and, with the
// card's bottom 4 px under the tile's (kRowH - 197 = 27 above the tile), card y 124-220.
constexpr int kMidX = 276, kMidW = 130, kMidY = 68, kMidH = 146;
constexpr int kSendH = 74, kCancelY = kMidY + 82, kCancelH = kMidH - 82;
constexpr int kRightX = 426, kRightW = 338;
constexpr int kBoxY = 68, kBoxH = 92, kBtnY = 168, kBtnH = 48;
constexpr int kTimerY = 60, kDrainY = 116, kLevelCapY = 140, kLevelY = 160, kBarH = 14, kHintY = 188;
constexpr int kEditCardX = 8, kEditCardY = 8, kEditCardW = 784, kEditCardH = kKpTop - 16;  // above the keyboard

int row_y() { return (g_set.floor == tc::Floor::Upstairs ? kTileUpY : kTileDownY) + 197 - kRowH; }

struct Recorder {
  Rs shown = Rs::None;
  VoiceErr err = VoiceErr::None;
  bool dirty = false;       // the parts that follow the state, on the next render
  bool full = false;        // the whole card (a new day/night palette)
  bool stop_armed = false;  // the big button's press began while recording: its click is STOP
  char text[tc::kVoiceTextMax + 1] = "";  // the result as shown, maybe edited or typed
  uint16_t seq = 0;         // View::voice.text_seq at START: a different one is this job's result
  uint32_t acted_at = 0;
  lv_obj_t *card = nullptr, *timer = nullptr, *hint = nullptr;
  lv_obj_t *big = nullptr, *big_lbl = nullptr, *big_mic = nullptr, *big_stop = nullptr;
  lv_obj_t *pair = nullptr, *rcol = nullptr;
  Bar drain, level;
  Dots dots;
  int timer_shown = -1, peer_key = -1;
  // KEYBOARD: the text in a 120-character field with the keyboard (its Done comes back here).
  char edit[tc::kVoiceTextMax + 1] = "";
  lv_obj_t *ecard = nullptr, *efield = nullptr, *elabel = nullptr, *ecursor = nullptr, *ecount = nullptr;
  Keypad kp;
};
Recorder &rec = ui_keep<Recorder>();  // about 0.9 KB (the edit keyboard's Keypad): PSRAM on the panel

bool recorder_open() { return g_sheet == Sheet::Voice && g_sheet_root && rec.card; }

void rec_forget() {
  rec.card = rec.timer = rec.hint = rec.big = rec.big_lbl = rec.big_mic = rec.big_stop = nullptr;
  rec.pair = rec.rcol = nullptr;
  rec.ecard = rec.efield = rec.elabel = rec.ecursor = rec.ecount = nullptr;
  rec.kp.obj = nullptr;
  rec.kp.nkeys = 0;
  rec.drain = Bar{};
  rec.level = Bar{};
  rec.dots = Dots{};
  rec.shown = Rs::None;
  rec.stop_armed = false;
}

// SEND's state: 0 SEND, 1 the other panel is offline, 2 it needs the update.
int peer_key(const VoiceView &v) { return v.peer_offline ? 1 : (!v.peer_voice ? 2 : 0); }

void rec_set(Rs s) {
  rec.shown = s;
  rec.dirty = true;
}

void do_send() {
  if (!g_act || !rec.text[0]) return;
  if (g_act->voice_send(rec.text)) {
    rec.acted_at = now();
    rec_set(Rs::Sending);
  }
}

// START, and TRY AGAIN: a new recording replaces the words. Capture starts and BEGIN goes out before
// anything is drawn (no tick sound: the mics are next to the speaker).
void start_recording() {
  if (!g_act) return;
  const VoiceView &v = vv();
  if (v.ui == VoiceUi::Result || v.ui == VoiceUi::Error || v.ui == VoiceUi::Delivered) g_act->voice_done();
  else if (v.ui != VoiceUi::Idle) g_act->voice_cancel();
  rec.seq = v.text_seq;
  rec.text[0] = 0;
  rec.acted_at = now();
  if (g_act->voice_press(VoicePurpose::Message)) {
    rec.err = VoiceErr::None;
    rec_set(Rs::Rec);
  } else {
    rec.err = vv().err;
    rec_set(Rs::Error);
  }
  lv_disp_trig_activity(nullptr);
}

// The big button: START from its PRESS (the 3 s start at once); while recording a NEW press's click
// is STOP. The press that started the recording never stops it: the button only changes its look,
// it is not rebuilt under the finger, and its click is not armed.
void big_event(lv_event_t *e) {
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      if (rec.shown == Rs::Ready) {
        rec.stop_armed = false;
        start_recording();
      } else {
        rec.stop_armed = rec.shown == Rs::Rec;
      }
      break;
    case LV_EVENT_CLICKED:
      if (rec.stop_armed && rec.shown == Rs::Rec && g_act) g_act->voice_stop();
      rec.stop_armed = false;
      break;
    case LV_EVENT_PRESS_LOST: rec.stop_armed = false; break;
    default: break;
  }
}
void cancel_clicked(lv_event_t *) { close_sheet(); }  // the hook ends the job (voice_sheet_closing)
void send_clicked(lv_event_t *) {
  if (rec.shown == Rs::Result || rec.shown == Rs::Error) do_send();
}
void again_clicked(lv_event_t *) {
  if (rec.shown == Rs::Result || rec.shown == Rs::Error) start_recording();
}

// ---- KEYBOARD: the edit card ----
// Tap to place the cursor (DESIGN 8): the keys and the voice text go in at g_ete.cursor.
tc::TextEdit g_ete = {rec.edit, tc::kVoiceTextMax, 0};
uint32_t g_epress_ms = 0;  // the last cursor move by touch (at most 20 a second while sliding)

void edit_refresh() {
  if (!rec.elabel) return;
  set_text(rec.elabel, rec.edit);
  lv_obj_update_layout(rec.elabel);
  tc::te_set(g_ete, g_ete.cursor);
  lv_point_t p = {0, 0};
  lv_label_get_letter_pos(rec.elabel, (uint32_t)g_ete.cursor, &p);
  lv_obj_set_pos(rec.ecursor, 12 + p.x - 1, 8 + p.y + 2);  // 3 px wide, centred on the gap
  char c[24];
  snprintf(c, sizeof(c), "%d / %d", (int)strlen(rec.edit), (int)tc::kVoiceTextMax);
  set_text(rec.ecount, c);
}
constexpr int kEditTextW = 536;
int edit_lines(const char *t) {
  int lh = (int)lv_font_get_line_height(B20);
  return (wrapped_h(t, B20, kEditTextW) + lh - 1) / lh;
}
void edit_flash_end(lv_timer_t *) {
  if (rec.efield) lv_obj_set_style_border_color(rec.efield, C(ink()), 0);
}
void edit_flash() {
  if (!rec.efield) return;
  lv_obj_set_style_border_color(rec.efield, C(theme::cal_colors(g_mode).danger), 0);
  lv_timer_t *t = lv_timer_create(edit_flash_end, 220, nullptr);
  lv_timer_set_repeat_count(t, 1);
}
bool edit_on_char(char ch) {
  tc::te_set(g_ete, g_ete.cursor);
  size_t len = strlen(rec.edit);
  int c = g_ete.cursor;
  if (len >= tc::kVoiceTextMax) {
    edit_flash();
    return false;
  }
  if (ch == ' ' && (c == 0 || rec.edit[c - 1] == ' ' || rec.edit[c] == ' ')) return false;  // both neighbours
  tc::te_insert(g_ete, ch);
  if (edit_lines(rec.edit) > 3) {  // the arrival dialog's three lines
    tc::te_backspace(g_ete);
    edit_flash();
    return false;
  }
  edit_refresh();
  return true;
}
// Backspace at the cursor; a space it leaves leading or doubled goes too; nothing changes if the
// joined words would need a fourth line.
void edit_on_back() {
  tc::te_set(g_ete, g_ete.cursor);
  if (g_ete.cursor > 0) {
    char before[tc::kVoiceTextMax + 1];
    memcpy(before, rec.edit, sizeof(before));
    int was = g_ete.cursor;
    tc::te_backspace(g_ete);
    int c = g_ete.cursor;
    if (rec.edit[c] == ' ' && (c == 0 || rec.edit[c - 1] == ' ')) tc::te_delete(g_ete);
    if (edit_lines(rec.edit) > 3) {
      memcpy(rec.edit, before, sizeof(before));
      g_ete.cursor = was;
      edit_flash();
    }
  }
  edit_refresh();
}

// x of the first n characters of s from where s starts, measured on the label itself (tc::WidthFn;
// ctx: the label), so the hit test sees the label's own wrap.
int edit_letter_w(const char *s, int n, void *ctx) {
  lv_obj_t *l = (lv_obj_t *)ctx;
  lv_point_t a = {0, 0}, b = {0, 0};
  lv_label_get_letter_pos(l, (uint32_t)(s - rec.edit), &a);
  lv_label_get_letter_pos(l, (uint32_t)(s - rec.edit + n), &b);
  return b.x - a.x;
}

// A press in the field: the line by y (line starts read from the label's wrap), the gap by the
// midpoint rule; sliding moves the cursor along, at most 20 times a second.
void edit_field_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if ((code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) || !rec.elabel) return;
  if (code == LV_EVENT_PRESSING && lv_tick_elaps(g_epress_ms) < 50) return;
  g_epress_ms = lv_tick_get();
  lv_indev_t *in = lv_indev_get_act();
  if (!in) return;
  lv_point_t p;
  lv_indev_get_point(in, &p);
  lv_area_t a;
  lv_obj_get_coords(rec.elabel, &a);
  const int len = (int)strlen(rec.edit), lh = (int)lv_font_get_line_height(B20);
  int starts[8] = {0}, n = 1;
  lv_coord_t line_y = 0;
  for (int i = 1; i < len && n < 8; i++) {
    lv_point_t q = {0, 0};
    lv_label_get_letter_pos(rec.elabel, (uint32_t)i, &q);
    if (q.y > line_y) {
      starts[n++] = i;
      line_y = q.y;
    }
  }
  int y = p.y - a.y1, line = y < 0 ? 0 : y / lh;
  if (line > n - 1) line = n - 1;
  int lo = starts[line], hi = line == n - 1 ? len : starts[line + 1] - 1;  // above the last: before its break
  if (hi < lo) hi = lo;
  int c = tc::te_hit(rec.edit, lo, hi, p.x - a.x1, edit_letter_w, rec.elabel);
  if (c == g_ete.cursor) return;
  tc::te_set(g_ete, c);
  edit_refresh();
}
const char *edit_on_mic() { return rec.edit; }

// Back to the row with the edited text (empty: the text as it was; with no text at all, ready to
// record). From the keyboard's Done key or the card's DONE: the edit card and keyboard are hidden
// now and deleted later.
void edit_done() {
  if (rec.shown != Rs::Edit) return;
  voice_keypad_gone(rec.kp);  // a dictation into the field still running is cancelled
  if (rec.edit[0]) snprintf(rec.text, sizeof(rec.text), "%s", rec.edit);
  if (rec.ecard) {
    show(rec.ecard, false);
    lv_obj_del_async(rec.ecard);
  }
  if (rec.kp.obj) {
    show(rec.kp.obj, false);
    lv_obj_del_async(rec.kp.obj);
  }
  rec.ecard = rec.efield = rec.elabel = rec.ecursor = rec.ecount = nullptr;
  rec.kp.obj = nullptr;
  rec.kp.nkeys = 0;
  if (rec.card) show(rec.card, true);
  rec_set(rec.text[0] ? Rs::Result : Rs::Ready);
}
void edit_done_clicked(lv_event_t *) { edit_done(); }

void edit_open() {
  if (!g_sheet_root) return;
  // An error shown from the view is over now (its text, if any, stays here to edit and send).
  if (g_act && vv().ui != VoiceUi::Idle) g_act->voice_done();
  snprintf(rec.edit, sizeof(rec.edit), "%s", rec.text);
  g_ete.cursor = (int)strlen(rec.edit);  // at the end to start
  tc::te_register(&g_ete);               // so the voice strip finds the cursor
  rec.shown = Rs::Edit;
  if (rec.card) show(rec.card, false);
  const Color sheet = sheet_col(), k = ink();
  const theme::CalColors &cc = theme::cal_colors(g_mode);
  rec.ecard = box(g_sheet_root, kEditCardX, kEditCardY, kEditCardW, kEditCardH, sheet, 24);
  lv_obj_add_flag(rec.ecard, LV_OBJ_FLAG_CLICKABLE);
  char title[48];
  snprintf(title, sizeof(title), "MESSAGE TO %s", other_word());
  label(rec.ecard, J23, k, 24, 14, title);
  const int lh = (int)lv_font_get_line_height(B20), fw = kEditTextW + 24, fh = 3 * lh + 16;
  rec.efield = box(rec.ecard, 24, 52, fw, fh, cc.field, 12);
  lv_obj_set_style_border_width(rec.efield, 3, 0);
  lv_obj_set_style_border_color(rec.efield, C(k), 0);
  lv_obj_set_style_border_opa(rec.efield, LV_OPA_COVER, 0);
  rec.elabel = label(rec.efield, B20, k, 12, 8, "");
  lv_obj_set_width(rec.elabel, kEditTextW);
  lv_label_set_long_mode(rec.elabel, LV_LABEL_LONG_WRAP);
  rec.ecursor = box(rec.efield, 12, 10, 3, lh - 4, k, 0);
  // Tap to place the cursor; the press stays with the field while the finger slides.
  lv_obj_add_flag(rec.efield, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(rec.efield, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(rec.efield, edit_field_event, LV_EVENT_ALL, nullptr);
  rec.ecount = label(rec.ecard, B16, muted(), 28, 52 + fh + 6, "");
  button(rec.ecard, kEditCardW - 24 - 156, 52, 156, 64, accent(), cc.cream, "DONE", J24, edit_done_clicked, nullptr);
  rec.kp.shift = rec.edit[0] == 0;
  rec.kp.sym = false;
  rec.kp.on_char = edit_on_char;
  rec.kp.on_back = edit_on_back;
  rec.kp.on_done = edit_done;
  rec.kp.on_mic = voice_offered() ? edit_on_mic : nullptr;
  keypad_build(rec.kp, g_sheet_root, kKpTop, g_mode);
  edit_refresh();
  lv_disp_trig_activity(nullptr);
}
void edit_clicked(lv_event_t *) {
  if (rec.shown == Rs::Ready || rec.shown == Rs::Result || rec.shown == Rs::Error) edit_open();
}

// ---- The row card ----
// A face colour on a pressable box: the face and its ledge (pressable's 0.62 shade).
void face_set(lv_obj_t *b, Color face) {
  lv_color_t want = C(face);
  if (lv_obj_get_style_bg_color(b, 0).full == want.full) return;
  lv_obj_set_style_bg_color(b, want, 0);
  lv_obj_set_style_border_color(b, C(gfx::scale(face, 0.62f)), 0);
}

// The white microphone (a capsule in a cup on a stand) or the white stop square, from boxes: no
// canvas, nothing in internal RAM. Aligned rather than placed: LVGL puts a child its parent's border
// width further in on every side, and the button's 6 px ledge is a border (bottom only), which left the
// microphone 6 px right of centre (Mike, 2026-11-21).
void big_icons(lv_obj_t *b) {
  const Color white = hex(0xFFFFFF);
  rec.big_mic = plain(b, 0, 0, 30, 50);
  lv_obj_align(rec.big_mic, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_clear_flag(rec.big_mic, LV_OBJ_FLAG_CLICKABLE);
  box(rec.big_mic, 8, 0, 14, 28, white, 7);
  lv_obj_t *cup = box(rec.big_mic, 1, 12, 28, 24, white, 14);
  lv_obj_set_style_bg_opa(cup, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cup, 3, 0);
  lv_obj_set_style_border_color(cup, C(white), 0);
  lv_obj_set_style_border_opa(cup, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(cup, (lv_border_side_t)(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
  box(rec.big_mic, 13, 36, 4, 9, white, 0);
  box(rec.big_mic, 6, 45, 18, 4, white, 2);
  rec.big_stop = box(b, 0, 0, 34, 34, white, 4);
  lv_obj_align(rec.big_stop, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_clear_flag(rec.big_stop, LV_OBJ_FLAG_CLICKABLE);
}

// A keyboard drawn from boxes in col (Mike, 2026-11-21: the button's word KEYBOARD became this): a
// rounded outline, two rows of keys and the space bar, 64 x 32, centred on the button's face.
void keyboard_icon(lv_obj_t *btn, Color col) {
  lv_obj_t *g = plain(btn, 0, 0, 64, 32);
  lv_obj_clear_flag(g, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *body = box(g, 0, 0, 64, 32, col, 6);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 3, 0);
  lv_obj_set_style_border_color(body, C(col), 0);
  lv_obj_set_style_border_opa(body, LV_OPA_COVER, 0);
  for (int i = 0; i < 6; i++) box(g, 9 + i * 8, 7, 6, 5, col, 1);  // the top row
  for (int i = 0; i < 5; i++) box(g, 13 + i * 8, 14, 6, 5, col, 1);
  box(g, 18, 22, 28, 4, col, 1);  // the space bar
  lv_obj_align(g, LV_ALIGN_CENTER, 0, -3);  // like a button's label
}

// The timer and its bar: ms left of the 3.0 s cap. Tenths rounded up, so it reads 3.0 at the press
// and 0.0 only when the cap is reached; the bar is exact to the pixel.
void timer_show(uint32_t left) {
  if (left > tc::kVoiceRecMs) left = tc::kVoiceRecMs;
  const int tenths = (int)((left + 99) / 100);
  if (rec.timer && tenths != rec.timer_shown) {
    char b[16];
    snprintf(b, sizeof(b), "%d.%d s", tenths / 10, tenths % 10);
    set_text(rec.timer, b);
    rec.timer_shown = tenths;
  }
  bar_set(rec.drain, (int)((uint64_t)left * (uint32_t)rec.drain.w / tc::kVoiceRecMs));
}

void card_build();  // below

// The text box's contents and the buttons under it (rebuilt on a state change, from the render
// loop only: never inside one of its own events).
void right_build() {
  if (!rec.rcol) return;
  lv_obj_clean(rec.rcol);
  rec.dots = Dots{};
  const Color k = ink();
  const theme::CalColors &cc = theme::cal_colors(g_mode);
  lv_obj_t *tb = box(rec.rcol, 0, kBoxY, kRightW, kBoxH, cc.field, 12);
  lv_obj_set_style_border_width(tb, 3, 0);
  lv_obj_set_style_border_color(tb, C(gfx::mix(cc.field, k, 0.45f)), 0);
  lv_obj_set_style_border_opa(tb, LV_OPA_COVER, 0);
  const int tw = kRightW - 24, room = kBoxH - 16;
  char buf[64];
  lv_obj_t *l = nullptr;
  switch (rec.shown) {
    case Rs::Ready: l = label(tb, B20, muted(), 12, 8, "The words show here"); break;
    case Rs::Rec:
      l = label(tb, B20, muted(), 12, 8, "Listening");
      dots_build(rec.dots, tb, 12 + text_width("Listening", B20) + 10, 8 + 9);
      break;
    case Rs::Work:
    case Rs::Wait: {
      const char *t = rec.shown == Rs::Work ? "Turning speech into text" : "Waiting for the other floor's message";
      l = label(tb, B16, k, 12, 10, t);
      dots_build(rec.dots, tb, 12, 10 + wrapped_h(t, B16, tw) + 14);
      break;
    }
    case Rs::Result:
    case Rs::Sending: {
      // B20 if the words fit the box, else B16.
      const lv_font_t *f = wrapped_h(rec.text, B20, tw) <= room ? B20 : B16;
      l = label(tb, f, k, 12, 8, rec.text);
      break;
    }
    case Rs::Error: l = label(tb, B20, cc.danger, 12, 8, err_text(rec.err, buf, sizeof(buf))); break;
    case Rs::Edit:
    case Rs::None: break;
  }
  if (l) {
    lv_obj_set_width(l, tw);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  }
  const bool can_edit = rec.shown == Rs::Ready || rec.shown == Rs::Result || rec.shown == Rs::Error;
  if (can_edit) {  // a tap on the words opens the keyboard too
    lv_obj_add_flag(tb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tb, edit_clicked, LV_EVENT_CLICKED, nullptr);
  }
  const int bw = (kRightW - 12) / 2;
  if (rec.shown == Rs::Result || rec.shown == Rs::Error)
    button(rec.rcol, 0, kBtnY, bw, kBtnH, chip(), k, "TRY AGAIN", J24, again_clicked, nullptr);
  if (can_edit) keyboard_icon(button(rec.rcol, kRightW - bw, kBtnY, bw, kBtnH, chip(), k, nullptr, J24, edit_clicked, nullptr), k);
}

// SEND over CANCEL in the centre, after START (rebuilt on a state change, from the render loop).
void pair_build() {
  if (!rec.pair) return;
  lv_obj_clean(rec.pair);
  const Color k = ink(), sheet = sheet_col(), cream = theme::cal_colors(g_mode).cream;
  const VoiceView &v = vv();
  rec.peer_key = peer_key(v);
  const bool after = rec.shown == Rs::Result || rec.shown == Rs::Error;
  if (after && rec.text[0] && rec.peer_key == 0) {
    button(rec.pair, 0, 0, kMidW, kSendH, accent(), cream, "SEND", J28, send_clicked, nullptr);
  } else {  // not offered: grey, with the reason when the words are there but the other panel is not
    lv_obj_t *g = box(rec.pair, 0, 0, kMidW, kSendH, gfx::mix(sheet, k, 0.14f), 18);
    if (after && rec.text[0]) {
      char b[48];
      snprintf(b, sizeof(b), "%s\n%s", other_word(), rec.peer_key == 1 ? "offline" : "needs update");
      lv_obj_t *gl = label(g, B16, muted(), 0, 0, b);
      lv_obj_set_width(gl, kMidW);
      lv_obj_set_style_text_align(gl, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(gl, LV_ALIGN_CENTER, 0, 0);
    } else {
      lv_obj_t *gl = label(g, rec.shown == Rs::Sending ? J24 : J28, muted(), 0, 0,
                           rec.shown == Rs::Sending ? "SENDING" : "SEND");
      lv_obj_align(gl, LV_ALIGN_CENTER, 0, -3);
    }
  }
  button(rec.pair, 0, kCancelY - kMidY, kMidW, kCancelH, chip(), k, "CANCEL", J24, cancel_clicked, nullptr);
}

// The parts that follow the state: the big button's look, SEND and CANCEL, the text box, the hint.
void parts_build() {
  rec.dirty = false;
  if (!rec.card) return;
  const bool big = rec.shown == Rs::Ready || rec.shown == Rs::Rec;
  if (rec.big) {
    const bool recording = rec.shown == Rs::Rec;
    face_set(rec.big, recording ? stop_red() : mic_blue(g_mode));
    set_text(rec.big_lbl, recording ? "STOP" : "START");
    show(rec.big_mic, !recording);
    show(rec.big_stop, recording);
    show(rec.big, big);
  }
  if (rec.pair) {
    show(rec.pair, !big);
    if (!big) pair_build();
  }
  right_build();
  // The drain is red while it runs; full and quiet when ready again.
  if (rec.drain.fg) {
    const Color dc = rec.shown == Rs::Rec ? stop_red() : gfx::mix(sheet_col(), ink(), 0.55f);
    lv_obj_set_style_bg_color(rec.drain.fg, C(dc), 0);
  }
  if (rec.shown == Rs::Ready || rec.shown == Rs::Result || rec.shown == Rs::Error) {
    timer_show(tc::kVoiceRecMs);
  } else if (rec.shown == Rs::Work || rec.shown == Rs::Wait) {
    // Held where the recording ended (0.0 at the cap, more after an early stop on quiet).
    const VoiceView &v = vv();
    const uint32_t el = g_mono - v.press_ms;
    if (v.press_ms) timer_show(el >= tc::kVoiceRecMs ? 0 : tc::kVoiceRecMs - el);
  }
  if (rec.shown != Rs::Rec) level_set(rec.level, 0);
  const char *h = rec.shown == Rs::Ready ? "Press START, then speak"
                  : rec.shown == Rs::Rec ? "Speak now"
                  : rec.shown == Rs::Result ? "Check the words, then SEND"
                                            : "";
  set_text(rec.hint, h);
  lv_disp_trig_activity(nullptr);
}

void card_build() {
  if (!rec.card) return;
  lv_obj_clean(rec.card);
  rec.timer = rec.hint = rec.big = rec.big_lbl = rec.big_mic = rec.big_stop = rec.pair = rec.rcol = nullptr;
  rec.drain = Bar{};
  rec.level = Bar{};
  rec.dots = Dots{};
  rec.timer_shown = -1;
  rec.full = false;
  lv_obj_set_style_bg_color(rec.card, C(sheet_col()), 0);
  const Color sheet = sheet_col(), k = ink(), track = gfx::mix(sheet, k, 0.15f);
  char buf[64];
  snprintf(buf, sizeof(buf), "Message to %s", other_word());
  label(rec.card, J28, k, kLeftX, 14, buf);
  button(rec.card, kRowW - 20 - 132, 10, 132, 48, chip(), k, "CLOSE", J24, cancel_clicked, nullptr);
  // Left: the timer, its bar, the level.
  rec.timer = label(rec.card, J44, k, kLeftX, kTimerY, "3.0 s");
  bar_build(rec.drain, rec.card, kLeftX, kDrainY, kLeftW, kBarH, track, gfx::mix(sheet, k, 0.55f));
  label(rec.card, B14, muted(), kLeftX, kLevelCapY, "MICROPHONE");
  bar_build(rec.level, rec.card, kLeftX, kLevelY, kLeftW, kBarH, track, mic_blue(g_mode));
  rec.hint = label(rec.card, B16, muted(), kLeftX, kHintY, "");
  // Centre: the big START / STOP, and the SEND / CANCEL pair in the same place.
  rec.big = box(rec.card, kMidX, kMidY, kMidW, kMidH, mic_blue(g_mode), 18);
  pressable(rec.big, mic_blue(g_mode), kMidH);
  lv_obj_add_flag(rec.big, LV_OBJ_FLAG_PRESS_LOCK);  // a finger held through the 3 s never lands on SEND
  lv_obj_add_event_cb(rec.big, big_event, LV_EVENT_ALL, nullptr);
  big_icons(rec.big);
  rec.big_lbl = label(rec.big, J28, hex(0xFFFFFF), 0, 0, "START");
  lv_obj_align(rec.big_lbl, LV_ALIGN_BOTTOM_MID, 0, -16);
  rec.pair = plain(rec.card, kMidX, kMidY, kMidW, kMidH);
  lv_obj_clear_flag(rec.pair, LV_OBJ_FLAG_CLICKABLE);
  // Right: the text box and its buttons.
  rec.rcol = plain(rec.card, kRightX, 0, kRightW, kRowH);
  lv_obj_clear_flag(rec.rcol, LV_OBJ_FLAG_CLICKABLE);
  parts_build();
}

void recorder_build_sheet(Rs s) {
  lv_obj_t *root = new_sheet(Sheet::Voice, true);  // closes any other sheet first (the hook runs)
  rec.shown = s;
  rec.stop_armed = false;
  rec.ecard = rec.efield = rec.elabel = rec.ecursor = rec.ecount = nullptr;
  rec.kp.obj = nullptr;
  rec.kp.nkeys = 0;
  rec.card = box(root, kRowX, row_y(), kRowW, kRowH, sheet_col(), 24);
  lv_obj_add_flag(rec.card, LV_OBJ_FLAG_CLICKABLE);
  card_build();
}

// Sheet::Voice as the view stands: a message job already running shows where it is; otherwise ready.
void open_from_view() {
  const VoiceView &v = vv();
  Rs s = Rs::Ready;
  if (v.purpose == VoicePurpose::Message) {
    switch (v.ui) {
      case VoiceUi::Recording: s = Rs::Rec; break;
      case VoiceUi::Working: s = Rs::Work; break;
      case VoiceUi::Waiting: s = Rs::Wait; break;
      case VoiceUi::Result: s = Rs::Result; break;
      case VoiceUi::Sending: s = Rs::Sending; break;
      case VoiceUi::Error: s = Rs::Error; break;
      default: break;
    }
  }
  rec.seq = v.text_seq;
  snprintf(rec.text, sizeof(rec.text), "%s", s == Rs::Ready ? "" : v.text);
  rec.err = v.err;
  rec.acted_at = now();
  recorder_build_sheet(s);
}

// The long press on the own badge: the row, ready, nothing recording yet.
void badge_long() {
  if (!g_act || !voice_offered() || g_sheet != Sheet::None || st.job) return;
  const VoiceView &v = vv();
  if (v.purpose == VoicePurpose::Message && v.ui != VoiceUi::Idle && v.ui != VoiceUi::Delivered &&
      v.ui != VoiceUi::Error) {
    open_from_view();  // a message job still running (its sheet was closed by another sheet)
    return;
  }
  if (v.ui == VoiceUi::Result || v.ui == VoiceUi::Error || v.ui == VoiceUi::Delivered) g_act->voice_done();
  else if (v.ui != VoiceUi::Idle) return;  // one voice job per panel (a dictation)
  rec.seq = v.text_seq;
  rec.text[0] = 0;
  rec.err = VoiceErr::None;
  rec.acted_at = now();
  recorder_build_sheet(Rs::Ready);
}

void recorder_tick(const tc::View &view, bool mode_changed) {
  if (!recorder_open()) return;
  if (rec.shown == Rs::Edit) {
    if (mode_changed) rec.full = true;  // the row is rebuilt in the new palette when it comes back
    if (lv_disp_get_inactive_time(nullptr) > kIdleCloseMs) close_sheet();
    return;
  }
  const VoiceView &v = view.voice;
  Rs want = rec.shown;
  if (v.purpose == VoicePurpose::Message && rec.shown != Rs::Ready) {
    switch (v.ui) {
      case VoiceUi::Recording: want = Rs::Rec; break;
      case VoiceUi::Working: want = Rs::Work; break;
      case VoiceUi::Waiting: want = Rs::Wait; break;
      case VoiceUi::Result:
        if (v.text_seq != rec.seq) {  // this job's result: kept here, so KEYBOARD can change it
          snprintf(rec.text, sizeof(rec.text), "%s", v.text);
          rec.seq = v.text_seq;
          want = Rs::Result;
        }
        break;
      case VoiceUi::Sending: want = Rs::Sending; break;
      case VoiceUi::Delivered:
        if (rec.shown == Rs::Sending) {
          rec.shown = Rs::None;  // closing now: the hook has nothing to end
          close_sheet();
          if (g_act) g_act->voice_done();
          toast("Delivered");
          return;
        }
        break;
      case VoiceUi::Error:
        if (rec.shown != Rs::Error || rec.err != v.err) {
          rec.err = v.err;
          want = Rs::Error;
          rec.dirty = true;
        }
        break;
      case VoiceUi::Idle: break;
    }
  }
  // A job that left the view without a word (it can't on purpose): back to ready rather than hang.
  const bool running = rec.shown == Rs::Rec || rec.shown == Rs::Work || rec.shown == Rs::Wait || rec.shown == Rs::Sending;
  if (running && since(rec.acted_at) > kGraceMs &&
      (v.purpose != VoicePurpose::Message || v.ui == VoiceUi::Idle)) {
    want = rec.text[0] ? Rs::Result : Rs::Ready;
  }
  if (want != rec.shown) rec_set(want);
  if ((rec.shown == Rs::Result || rec.shown == Rs::Error) && peer_key(v) != rec.peer_key) rec.dirty = true;  // SEND comes and goes
  if (mode_changed) rec.full = true;
  if (rec.full) card_build();
  else if (rec.dirty) parts_build();
  switch (rec.shown) {
    case Rs::Rec:
      timer_show(rec_left(v));
      level_set(rec.level, v.ui == VoiceUi::Recording ? v.level : 0);
      dots_tick(rec.dots);
      break;
    case Rs::Work:
    case Rs::Wait: dots_tick(rec.dots); break;
    case Rs::Ready:
      // Field fix 2026-11-21: the live microphone meter before START (the always-on mic task's block
      // peaks; nothing records), so the person can see the microphones hear them.
      if (g_act) g_act->voice_meter();
      level_set(rec.level, v.level);
      if (lv_disp_get_inactive_time(nullptr) > kIdleCloseMs) close_sheet();
      break;
    case Rs::Result:
    case Rs::Error:
      if (lv_disp_get_inactive_time(nullptr) > kIdleCloseMs) close_sheet();
      break;
    default: break;
  }
}

// The hook: the recorder closes (CANCEL, CLOSE, the idle close, another sheet).
void recorder_closing() {
  if (g_act) {
    switch (rec.shown) {
      case Rs::Rec:
      case Rs::Work:
      case Rs::Wait: g_act->voice_cancel(); break;  // ABORT user, the clip freed
      case Rs::Result:
      case Rs::Edit:
      case Rs::Error:
      case Rs::Sending: g_act->voice_done(); break;  // a SAY already out keeps its retries
      case Rs::Ready:
      case Rs::None: break;
    }
  }
  rec_forget();
}

// =============================================================================================
// The arrival dialog (DESIGN 2.3)

struct Dialog {
  lv_obj_t *box = nullptr;
  lv_obj_t *rim = nullptr;   // the pulse: four gold bars along the card's edges (its children)
  int rim_step = -1;         // the pulse step shown (kPulseSteps and on: over, the bars hidden)
  bool visible = false;
  uint16_t seq = 0;
  uint8_t from = 0xFF;
  int mode = -1;
  bool plus = false;
  int border = -1;           // the sender tile's status colour it was built with
  uint32_t shown_at = 0;
  bool press_ok = false;     // the press began after the guard
  bool gone = false;         // dismissed here: stays hidden until the view's inbox_seq moves
  uint16_t gone_seq = 0;
};
Dialog &dlg = ui_keep<Dialog>();  // PSRAM on the panel, like the strip

constexpr int kDlgW = 386, kDlgH = 103;  // tile x 0-386, y 90-193 of the sender's tile
constexpr int kHeadH = 26;               // the header strip in the sender's colour
constexpr int kTextY = 28, kTextH = 52;  // the message: up to two lines, as big as fits (2 x B26)
constexpr int kTapY = 80;                // "Tap to dismiss" (B16, 17 px): clear of the 5 px bottom rim
constexpr int kRimW = 5, kRimR = 14;     // the pulse's bars; the card's corner radius
constexpr int kTileX = 12;  // kTileUpY and kTileDownY: with the recorder

void dlg_pressed(lv_event_t *) { dlg.press_ok = dlg.visible && since(dlg.shown_at) >= kGuardMs; }
void dlg_clicked(lv_event_t *) {
  if (!dlg.press_ok || !dlg.visible || !g_act) return;
  dlg.press_ok = false;
  if (g_act->voice_dismiss()) {  // this message leaves the inbox; the next one shows (oldest first)
    show(dlg.box, false);
    dlg.visible = false;
    dlg.gone = true;
    dlg.gone_seq = dlg.seq;
  }
}

// The sender floor's colour: its tile's status colour now (a little darker, so the edge shows on the
// tile under it).
int sender_status(const tc::View &view, uint8_t from) {
  const tc::FloorView &fv = view.floor[from == 1 ? 1 : 0];
  return fv.no_signal ? theme::kNoSignal : (int)fv.status;
}

// The speech bubble (a rounded body with three dots and a tail), from boxes in col on a face colour.
void bubble_icon(lv_obj_t *parent, int x, int y, Color col, Color face) {
  box(parent, x, y, 22, 14, col, 5);
  box(parent, x + 4, y + 11, 7, 7, col, 1);  // the tail, under the body's left
  for (int i = 0; i < 3; i++) box(parent, x + 5 + i * 5, y + 6, 3, 3, face, 1);
}

// The card (Mike, 2026-11-21): a light card by day and at night (dark ink on it), a header strip in the
// sender tile's status colour with the bubble and "MESSAGE FROM UPSTAIRS  9:42 PM" (and "+N" when more
// wait), the message as big as two lines allow, and "Tap to dismiss". No border on the card: a border
// moves every child in by its width.
void dlg_build(const VoiceView &v, int border) {
  if (dlg.box) lv_obj_del(dlg.box);  // from the render loop only, never inside its own event
  dlg.rim = nullptr;
  dlg.rim_step = -1;
  const tc::Floor from = v.top.from == 1 ? tc::Floor::Downstairs : tc::Floor::Upstairs;
  const bool night = g_mode == tc::Mode::Night;
  const Color card = night ? hex(0xD7CBB0) : sheet_col(), k = night ? hex(0x14201A) : ink();
  const theme::StatusColors sc = theme::status_colors(border, g_mode);
  const int y = (from == tc::Floor::Upstairs ? kTileUpY : kTileDownY) + 90;
  dlg.box = box(lv_layer_top(), kTileX, y, kDlgW, kDlgH, card, kRimR);
  lv_obj_add_flag(dlg.box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(dlg.box, dlg_pressed, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(dlg.box, dlg_clicked, LV_EVENT_CLICKED, nullptr);
  // The header strip: rounded on top with the card, square under (a second box over its lower half).
  box(dlg.box, 0, 0, kDlgW, kHeadH, sc.tile, kRimR);
  box(dlg.box, 0, kHeadH / 2, kDlgW, kHeadH - kHeadH / 2, sc.tile, 0);
  bubble_icon(dlg.box, 12, 3, sc.text, sc.tile);
  char head[48];
  snprintf(head, sizeof(head), "MESSAGE FROM %s%s%s", tc::floor_word(from), v.top_when[0] ? "  " : "", v.top_when);
  lv_obj_t *h = label(dlg.box, B14, sc.text, 42, (kHeadH - (int)lv_font_get_line_height(B14)) / 2, head);
  lv_obj_set_style_text_letter_space(h, 1, 0);
  if (v.inbox_n > 1) {  // the others waiting behind this one
    char p[8];
    snprintf(p, sizeof(p), "+%d", v.inbox_n - 1 > 9 ? 9 : v.inbox_n - 1);
    const int pw = text_width(p, B14) + 14;
    lv_obj_t *pill = box(dlg.box, kDlgW - 8 - pw, 4, pw, kHeadH - 8, sc.text, (kHeadH - 8) / 2);
    lv_obj_t *pl = label(pill, B14, sc.tile, 0, 0, p);
    lv_obj_align(pl, LV_ALIGN_CENTER, 0, 0);
  }
  // The message: the biggest font that holds it in two lines of 362 px, centred in its room (one
  // small font for three lines only when even B16 needs more than two).
  const lv_font_t *const fonts[] = {B26, B24, B22, B20, B18, B16};
  const int tw = kDlgW - 24;
  const lv_font_t *f = B14;
  for (const lv_font_t *fo : fonts) {
    const int lh = (int)lv_font_get_line_height(fo), th = wrapped_h(v.top.text, fo, tw);
    if (th <= 2 * lh && th <= kTextH) {
      f = fo;
      break;
    }
  }
  const int th = wrapped_h(v.top.text, f, tw);
  lv_obj_t *t = label(dlg.box, f, k, 12, kTextY + (th < kTextH ? (kTextH - th) / 2 : 0), v.top.text);
  lv_obj_set_width(t, tw);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  if (th > kTextH) lv_obj_set_height(t, kTextH);
  const char *tap = "Tap to dismiss";
  lv_obj_t *tl = label(dlg.box, B16, gfx::mix(card, k, 0.8f), (kDlgW - text_width(tap, B16, 1)) / 2, kTapY, tap);
  lv_obj_set_style_text_letter_space(tl, 1, 0);
  // The pulse's rim: four bars on the straight edges (the corners left alone), each redrawn alone when
  // it shows or hides, so the pulse never redraws the card. Hidden until dialog_tick shows them.
  dlg.rim = plain(dlg.box, 0, 0, kDlgW, kDlgH);
  lv_obj_clear_flag(dlg.rim, LV_OBJ_FLAG_CLICKABLE);
  const Color gold = theme::palette(g_mode).gold;
  const int bars[4][4] = {{kRimR, 0, kDlgW - 2 * kRimR, kRimW},
                          {kRimR, kDlgH - kRimW, kDlgW - 2 * kRimR, kRimW},
                          {0, kRimR, kRimW, kDlgH - 2 * kRimR},
                          {kDlgW - kRimW, kRimR, kRimW, kDlgH - 2 * kRimR}};
  for (const auto &b : bars) show(box(dlg.rim, b[0], b[1], b[2], b[3], gold, 2), false);
  show(dlg.box, false);
  dlg.visible = false;
  dlg.seq = v.inbox_seq;
  dlg.from = v.top.from;
  dlg.mode = (int)g_mode;
  dlg.plus = v.inbox_n > 1;
  dlg.border = border;
}

void dialog_tick(const tc::View &view, bool mode_changed) {
  const VoiceView &v = view.voice;
  if (dlg.gone && (v.inbox_seq != dlg.gone_seq || v.inbox_n == 0)) dlg.gone = false;
  // Never while a sheet is open (close_sheet's hook lets it show on the next frame), nor over the
  // calendar, nor while the screensaver runs (it shows at the next reveal).
  const bool want = v.inbox_n > 0 && v.top.id != 0 && !dlg.gone && g_sheet == Sheet::None && !cal_is_open() &&
                    !saver_running();
  if (!want) {
    if (dlg.box && dlg.visible) {
      show(dlg.box, false);
      dlg.visible = false;
    }
    return;
  }
  const int border = sender_status(view, v.top.from);
  if (!dlg.box || mode_changed || dlg.seq != v.inbox_seq || dlg.from != v.top.from || dlg.plus != (v.inbox_n > 1) ||
      dlg.mode != (int)g_mode || dlg.border != border) {
    const bool was = dlg.visible && dlg.seq == v.inbox_seq;  // same message, new colours: no new guard
    const uint32_t at = dlg.shown_at;
    dlg_build(v, border);
    if (was) {
      show(dlg.box, true);
      dlg.visible = true;
      dlg.shown_at = at;
      if (g_banner) lv_obj_move_foreground(g_banner);
    }
  }
  if (!dlg.visible) {
    show(dlg.box, true);
    lv_obj_move_foreground(dlg.box);
    lv_obj_set_style_opa(dlg.box, LV_OPA_TRANSP, 0);
    anim(dlg.box, 0, 255, kFadeMs, set_opa);
    dlg.visible = true;
    dlg.shown_at = now();  // the 700 ms guard starts when it shows
    dlg.press_ok = false;
    if (g_banner) lv_obj_move_foreground(g_banner);  // a doorbell is never under it
  }
  // The pulse: the gold rim on and off every 400 ms for its first 3.2 s, then off for good. Only the
  // four bars redraw (about 4,000 px a step), 2.5 times a second.
  int step = (int)(since(dlg.shown_at) / kPulseMs);
  if (step > kPulseSteps) step = kPulseSteps;
  if (dlg.rim && step != dlg.rim_step) {
    const bool on = step < kPulseSteps && step % 2 == 0;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(dlg.rim); i++) show(lv_obj_get_child(dlg.rim, i), on);
    dlg.rim_step = step;
  }
}

}  // namespace

// =============================================================================================

bool voice_offered() { return g_act && g_f.view && g_f.view->voice.capable; }

void voice_render(const tc::View &v, uint32_t mono_ms, bool mode_changed) {
  g_mono = mono_ms;
  strip_tick(v);
  recorder_tick(v, mode_changed);
  dialog_tick(v, mode_changed);
}

// The own badge's LONG press (Mike, 2026-11-21): the recorder's row, ready; a tap does nothing.
void voice_badge_long(lv_event_t *) { badge_long(); }

void open_voice_recorder() { open_from_view(); }

void voice_sheet_closing(Sheet s) {
  // A dictation on a keyboard inside this sheet ends with it (the strip dies with the sheet).
  if (st.root && st.kp && g_sheet_root && inside(st.root, g_sheet_root)) {
    if (st.job && g_act) g_act->voice_cancel();
    st.job = false;
    st.root = nullptr;  // its DELETE event then finds nothing to do
    st.parent = nullptr;
    st.kp = nullptr;
    st.shown = Ss::None;
  }
  if (s == Sheet::Voice) recorder_closing();
}

void voice_key_event(lv_event_t *e) {
  KeypadKey &key = *(KeypadKey *)lv_event_get_user_data(e);
  Keypad &kp = *key.kp;
  const bool here = st.kp == &kp && strip_live();
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      if (here && st.job) {  // a press while recording: the red mic key is STOP
        st.press.live = false;
        if (st.shown == Ss::Rec && g_act) g_act->voice_stop();
        return;
      }
      mic_press(kp);
      break;
    case LV_EVENT_PRESSING:
      if (here) press_still(st.press);
      break;
    case LV_EVENT_RELEASED:
      if (!here || !st.press.live) return;
      st.press.live = false;
      if (st.job && st.shown == Ss::Rec && is_hold(st.press) && g_act) g_act->voice_stop();
      break;
    case LV_EVENT_PRESS_LOST:
      if (!here || !st.press.live) return;
      st.press.live = false;
      if (st.job && since(st.press.at) < tc::kVoiceHoldMs) {  // lost before 0.6 s: cancelled, silently
        if (g_act) g_act->voice_cancel();
        st.job = false;
        strip_hide();
      }
      break;
    default: break;
  }
}

void voice_keypad_gone(Keypad &kp) {
  if (st.kp != &kp) return;
  if (st.job && g_act) g_act->voice_cancel();
  st.job = false;
  st.press.live = false;
  strip_hide();
}

}  // namespace detail
}  // namespace ui
