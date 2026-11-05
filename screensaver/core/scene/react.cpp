// The hiker's doorbell reaction. See react.h.
#include "react.h"


#include "../anim/ease.h"

namespace ss {

namespace {

// Table sines (anim/ease.h sinT) like the rig's, so the PC and the board agree to the bit.

// 0 before a, 1 after b, smoothstep between.
float ramp(float t, float a, float b) { return easeInOut(clamp01((t - a) / (b - a))); }

// A half-sine bump from a to a + len: 0 outside, 1 at its middle.
float bump(float t, float a, float len) { return t > a && t < a + len ? sinT(0.5f * (t - a) / len) : 0.f; }

}  // namespace

bool reactCanStart(const World& w) {
  return !w.react.active && w.plan.family != SF_LOOKOUT && w.fx.on && w.hiker.visible && !w.hiker_rect.empty() &&
         w.fade < 0.5f;
}

void reactStart(World& w) {
  DoorbellReact& r = w.react;
  r.active = true;
  r.ticks = 0;
  r.fx = w.fx;
  r.facing = w.hiker.facing;
  // With the bow in his hands (the archery major) he only looks round: a hop, a turn or a wave
  // would swing the bow and the string about mid-shot.
  r.look_only = w.day.kind == DAY_ARCHERY && w.day.archery.a.bow_on;
  r.hands_full = w.hiker.carry != CARRY_NONE || r.look_only;
}

void reactStep(World& w) {
  DoorbellReact& r = w.react;
  if (!r.active) return;
  ++r.ticks;
  HikerFx& fx = w.fx;
  Hiker& h = w.hiker;
  fx = r.fx;   // each tick: the offsets he had, plus the reaction
  if (r.ticks >= kReactTicks) {
    h.facing = r.facing;
    r.active = false;
    return;
  }
  const float t = (float)r.ticks * SS_TICK_DT;

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
  const float up = ramp(t, 0.35f, 0.6f) * (1.f - ramp(t, 3.0f, 3.35f));
  if (up <= 0.f) return;
  const float point = ramp(t, 2.3f, 2.55f);
  const float swing = 0.45f * sinT(2.2f * (t - 0.35f)) * (1.f - point);
  p.shoulder[0] = lerp(p.shoulder[0], lerp(2.75f, 1.45f, point), up);
  p.elbow[0] = lerp(p.elbow[0], lerp(0.6f + swing, 0.05f, point), up);
}

}  // namespace ss
