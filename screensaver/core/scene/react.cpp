// The hiker's doorbell reaction. See react.h.
#include "react.h"
#include "day_plink.h"
#include "day_taichi.h"
#include "day_trip.h"
#include "reeds.h"


#include "../anim/ease.h"

namespace ss {

namespace {

// Table sines (anim/ease.h sinT) like the rig's, so the PC and the board agree to the bit.

// The passer-by wave's near arm, in the rig's angles (from straight down, + toward the way he faces;
// pi is straight up): the upper arm up and out, the forearm up and out and swinging about its middle.
constexpr float kWaveUpper = 2.00f, kWaveFore = 2.36f, kWaveSwing = 0.32f;

// 0 before a, 1 after b, smoothstep between.
float ramp(float t, float a, float b) { return easeInOut(clamp01((t - a) / (b - a))); }

// A half-sine bump from a to a + len: 0 outside, 1 at its middle.
float bump(float t, float a, float len) { return t > a && t < a + len ? sinT(0.5f * (t - a) / len) : 0.f; }

}  // namespace

bool reactCanStart(const World& w) {
  return !w.react.active && !spineSetPiece(w.plan.family) && w.fx.on && w.hiker.visible && !w.hiker_rect.empty() &&
         w.fade < 0.5f && (!w.fi.on || fireIceWaveOk(w));
}

bool reactWaveOk(const World& w) {
  if (!reactCanStart(w) || w.hiker.carry != CARRY_NONE || w.pack.held) return false;
  for (const Item& it : w.items)
    if (it.held && !it.critter) return false;
  if (w.day.kind == DAY_ARCHERY && w.day.archery.a.bow_on) return false;
  if (plinkHandsFullNow(w)) return false;   // (the rifle, the stick or the sack in his hands: belt and braces, a major is busy anyway)
  // Stage 22 (INTEGRATION_PLAN 3.1 point 13; Mike, 2026-11-18): his hand is busy with a reed, or with
  // the mushrooms. It keeps trying, as for the rest.
  if (reedsPickActive(w) || tripHandsFull(w)) return false;
  return w.fi.on || !w.busy;   // fire and ice sets busy itself; reactCanStart has asked it
}

void reactStart(World& w, bool wave) {
  DoorbellReact& r = w.react;
  r.active = true;
  r.wave = wave;
  r.ticks = 0;
  r.fx = w.fx;
  r.facing = w.hiker.facing;
  // With the bow in his hands (the archery major) he only looks round: a hop, a turn or a wave
  // would swing the bow and the string about mid-shot.
  r.look_only = (w.day.kind == DAY_ARCHERY && w.day.archery.a.bow_on) || plinkRifleInHands(w);   // Mike, 2026-11-17: the .22 never swings either
  r.look_only = r.look_only || taichiInForm(w);   // Mike, 2026-11-18: he holds the form and only lifts his head
  r.hands_full = w.hiker.carry != CARRY_NONE || r.look_only || plinkHandsFullNow(w) || taichiHatInHand(w) ||
                 tripHandsFull(w);   // the hat off in his hand (tai chi), the mushrooms (the trip)
}

void reactStep(World& w) {
  DoorbellReact& r = w.react;
  if (!r.active) return;
  ++r.ticks;
  HikerFx& fx = w.fx;
  Hiker& h = w.hiker;
  fx = r.fx;   // each tick: the offsets he had, plus the reaction
  if (r.ticks >= (r.wave ? kWaveTicks : kReactTicks)) {
    h.facing = r.facing;
    r.active = false;
    return;
  }
  const float t = (float)r.ticks * SS_TICK_DT;
  if (r.wave) {   // a passer-by: he looks up and out at them, leaning back a touch, and waves
    // Mike, 2026-11-16: more enthusiastic. Head up higher, a little more lean back, and a few
    // small happy bounces (the hips only; no leap, so the redraw area stays small).
    const float k = ramp(t, 0.f, 0.3f) * (1.f - ramp(t, 3.3f, 3.75f));
    fx.head -= 0.26f * k;
    fx.lean -= 0.05f * k;
    fx.lift += 0.025f * (bump(t, 0.6f, 0.4f) + bump(t, 1.35f, 0.4f) + bump(t, 2.1f, 0.4f));
    return;
  }

  // His head up toward the sound, and back down at the end; then the jump.
  fx.head -= 0.22f * ramp(t, 0.f, 0.2f) * (1.f - ramp(t, 3.0f, 3.4f));
  if (r.look_only) return;
  fx.lift += 0.04f * bump(t, 0.f, 0.3f);

  // Facing away from the chip's side: round at 0.3 s and back at 3.15 s, each
  // on a small dip so the flip reads as a turn.
  if (r.facing < 0) {
    h.facing = t >= 0.3f && t < 3.15f ? 1 : r.facing;
    fx.lift -= 0.02f * (bump(t, 0.25f, 0.12f) + bump(t, 3.1f, 0.12f));
  }

  if (r.hands_full) {   // something in his hands: a second hop instead of the wave
    fx.lift += 0.03f * bump(t, 0.9f, 0.3f);
    return;
  }
  fx.lean -= 0.05f * ramp(t, 0.35f, 0.6f) * (1.f - ramp(t, 3.0f, 3.35f));
}

void reactPose(const World& w, HikerPose& p) {
  const DoorbellReact& r = w.react;
  if (!r.active || r.hands_full) return;
  // The wave: near arm up with the forearm swinging, then straight out at the
  // chip, then down, eased in from whatever his near arm was doing.
  const float t = (float)r.ticks * SS_TICK_DT;
  const float up = r.wave ? ramp(t, 0.1f, 0.4f) * (1.f - ramp(t, 3.3f, 3.75f))
                          : ramp(t, 0.35f, 0.6f) * (1.f - ramp(t, 3.0f, 3.35f));
  if (up <= 0.f) return;
  if (r.wave) {
    // Mike, 2026-11-16: an enthusiastic wave, but readable in silhouette. The upper arm goes up and
    // out to the side he faces (a little over 40 degrees above level) so the elbow clears the hat
    // brim; the forearm points up and out and swings side to side in a wide, fast arc (3 Hz, about
    // ten waves), always out beyond the brim. The far arm stays as it was. The swing eases in and out.
    const float swing_w = ramp(t, 0.3f, 0.65f) * (1.f - ramp(t, 3.2f, 3.6f));
    const float s = sinT(3.f * (t - 0.3f));
    const float sh = kWaveUpper + 0.06f * swing_w * s;
    const float fore = kWaveFore + kWaveSwing * swing_w * s;   // forearm, absolute (pi = straight up)
    p.shoulder[0] = lerp(p.shoulder[0], sh, up);
    p.elbow[0] = lerp(p.elbow[0], fore - sh, up);
    return;
  }
  const float point = r.wave ? 0.f : ramp(t, 2.3f, 2.55f);
  const float swing = 0.45f * sinT(2.2f * (t - (r.wave ? 0.15f : 0.35f))) * (1.f - point);
  p.shoulder[0] = lerp(p.shoulder[0], lerp(2.75f, 1.45f, point), up);
  p.elbow[0] = lerp(p.elbow[0], lerp(0.6f + swing, 0.05f, point), up);
}

}  // namespace ss
