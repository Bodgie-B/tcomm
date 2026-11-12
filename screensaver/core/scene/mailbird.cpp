// Mail by bird. See mailbird.h.
#include "mailbird.h"

#include <math.h>

#include "../anim/ease.h"
#include "director.h"
#include "script.h"
#include "world.h"

namespace ss {

namespace {

constexpr float kDt = SS_TICK_DT;
constexpr float kInS = 3.2f;     // the flight in, PROPOSED
constexpr float kFlitS = 1.6f;   // across to the other perch
constexpr float kOffS = 2.6f;    // the flight off
constexpr float kDropAt = 0.8f;  // seconds after landing it lets the letter go
constexpr float kFallS = 0.8f;   // the letter's fall
constexpr float kGlanceS = 1.6f;
constexpr float kBirdScale = 1.5f;   // a little bigger than the story's small birds, so it reads across the room (PROPOSED)

enum Idle : uint8_t { MI_NONE = 0, MI_LOOK, MI_HOP, MI_PREEN };

uint32_t nextR(MailBird& m) {
  uint32_t x = m.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  m.rng = x ? x : 0x9E3779B9u;
  return m.rng;
}
float rnd(MailBird& m, float a, float b) { return a + (b - a) * (float)(nextR(m) >> 8) * (1.f / 16777216.f); }

inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }

Vec2 bez(Vec2 a, Vec2 c, Vec2 b, float u) {
  const float v = 1.f - u;
  return Vec2{v * v * a.x + 2.f * v * u * c.x + u * u * b.x, v * v * a.y + 2.f * v * u * c.y + u * u * b.y};
}
void flyAlong(Critter& c, Vec2 a, Vec2 ctl, Vec2 b, float u) {
  const Vec2 p = bez(a, ctl, b, u);
  if (fabsf(p.x - c.feet.x) > 0.05f) c.facing = p.x < c.feet.x ? -1 : 1;
  c.feet = p;
}

bool tentPerch(const World& w) { return w.tent.state == TENT_UP && w.tent.progress >= 1.f && w.tent.sag < 0.3f; }
constexpr float kPerchSettle = 1.5f;   // the tent must stay up (or down) this long before it flits across

// The tent's back pole top (the morning's ridge birds use the front), or the bag branch's top
// surface 40 px out from the trunk (landscape.h: it rises 10 px over 82 px, 5 px thick).
Vec2 perchOf(const World& w, bool tent) {
  if (tent) {
    const Vec2 e = tentRidgeEnd(w.tent, 0);
    return Vec2{e.x + 1.f, e.y - 0.5f};
  }
  const float d = 40.f;
  const float y = (float)LS_BRANCH_ROOT_Y - (float)(LS_BRANCH_ROOT_Y - LS_BRANCH_TIP_Y) * d / (float)LS_BRANCH_LEN;
  return Vec2{(float)w.land.camp_x - d, y - 2.5f};
}
int16_t perchSort(const World& w, bool tent) {
  return tent ? (int16_t)(w.tent.pos.y + 1.f) : (int16_t)(w.land.camp.base_y - 1);
}
// Where the letter comes to rest: on the ground in front of the tent's back half, or under the branch.
Vec2 letterRest(const World& w, bool tent, Vec2 perch) {
  if (tent) return Vec2{w.tent.pos.x - 10.f, w.tent.pos.y + 8.f};
  const float x = perch.x + 3.f;
  return Vec2{x, scriptGroundAt(w, x, (float)w.land.camp.base_y + 8.f)};
}

Critter* bird(World& w) {
  const CritterSlot& s = w.crit[CRITTER_MAIL];
  return s.in_use && s.tag == CT_MAIL ? &w.critters[CRITTER_MAIL] : nullptr;
}

bool sceneReady(const World& w) { return !spineSetPiece(w.plan.family) && buildDone(w.build) && w.fade < 1.f; }

void spawn(World& w, Vec2 at, int facing) {
  directorSpawnAt(w, CRITTER_MAIL, CR_BIRD, at, facing);
  CritterSlot& s = w.crit[CRITTER_MAIL];
  s.tag = CT_MAIL;
  s.flags = CF_BRAVE;
  Critter& c = w.critters[CRITTER_MAIL];
  c.speed_dial = c.jitter_dial = 0.5f;
  c.scale = kBirdScale;
}

void toStage(MailBird& m, uint8_t s) {
  m.stage = s;
  m.t = 0.f;
}

// It faces the camp from its perch.
int faceCamp(const World& w, Vec2 p) { return w.fire.pos.x + 60.f < p.x ? -1 : 1; }

}  // namespace

void mailBeginLoop(World& w, uint64_t loop_seed, bool mail_now) {
  MailBird& m = w.mail;
  m.rng = (uint32_t)(loop_seed * 0x2545F4914F6CDD1Dull >> 32) | 1u;
  m.want = mail_now;
  m.there = mail_now;   // already waiting: the bird is simply there once the camp is up
  m.glance = -1.f;
  m.glance_next = rnd(m, 6.f, 14.f);
}

void mailSetInputs(World& w, bool mail) { w.mail.want = mail; }

void mailStep(World& w) {
  MailBird& m = w.mail;
  if (m.stage == MB_NONE && !m.want && !m.letter) return;   // nothing to do: the usual case
  if (spineSetPiece(w.plan.family)) return;
  m.t += kDt;
  if (m.letter && m.lt < kFallS) m.lt = minf(kFallS, m.lt + kDt);
  Critter* c = bird(w);

  switch (m.stage) {
    case MB_NONE:
      if (!m.want || !sceneReady(w) || w.crit[CRITTER_MAIL].in_use) break;
      m.on_tent = tentPerch(w);
      if (m.there) {   // simply there, its letter on the ground
        const Vec2 p = perchOf(w, m.on_tent);
        spawn(w, p, faceCamp(w, p));
        critterSetAction(w.critters[CRITTER_MAIL], CA_IDLE, 0.01f);
        w.crit[CRITTER_MAIL].sort_y = perchSort(w, m.on_tent);
        m.letter = true;
        m.lp = m.lp0 = letterRest(w, m.on_tent, p);
        m.lt = kFallS;
        m.there = false;
        m.next = rnd(m, 2.f, 5.f);
        toStage(m, MB_PERCH);
        m.t = 10.f;   // long since landed
      } else {
        m.a = Vec2{(float)SS_WIDTH + 24.f, 140.f};
        spawn(w, m.a, -1);
        Critter& b = w.critters[CRITTER_MAIL];
        b.carry = CC_LETTER;
        critterSetAction(b, CA_MOVE, 0.1f);
        w.crit[CRITTER_MAIL].sort_y = perchSort(w, m.on_tent);   // in front of what it lands on
        toStage(m, MB_IN);
      }
      break;
    case MB_IN: {
      if (!c) {
        toStage(m, MB_NONE);
        break;
      }
      if (!m.want) {   // collected before it even landed: it turns for home with it
        m.a = c->feet;
        c->carry = CC_NONE;
        toStage(m, MB_OFF);
        break;
      }
      if (tentPerch(w) != m.on_tent) {
        m.on_tent = !m.on_tent;
        w.crit[CRITTER_MAIL].sort_y = perchSort(w, m.on_tent);
      }
      const Vec2 b = perchOf(w, m.on_tent);
      const float u = clamp01(m.t / kInS);
      flyAlong(*c, m.a, Vec2{lerp(m.a.x, b.x, 0.55f), b.y + 22.f}, b, easeOutCubic(u));
      if (u >= 1.f) {
        c->facing = faceCamp(w, b);
        critterSetAction(*c, CA_IDLE, 0.25f);
        m.next = rnd(m, 2.5f, 4.5f);
        toStage(m, MB_PERCH);
      }
      break;
    }
    case MB_PERCH: {
      if (!c) {
        toStage(m, MB_NONE);
        break;
      }
      if (!m.want) {   // collected: the letter is gone, a hop and away
        m.letter = false;
        c->carry = CC_NONE;
        m.a = c->feet;
        critterSetAction(*c, CA_HOP, 0.25f);
        toStage(m, MB_OFF);
        break;
      }
      m.perch_t = tentPerch(w) != m.on_tent ? m.perch_t + kDt : 0.f;
      if (m.on_tent && w.tent.state == TENT_PACKED) m.perch_t = kPerchSettle;   // gone altogether: at once
      if (m.perch_t >= kPerchSettle) {   // the tent went up or came down: across to the other perch
        m.perch_t = 0.f;
        m.a = c->feet;
        m.on_tent = !m.on_tent;
        critterSetAction(*c, CA_MOVE, 0.12f);
        w.crit[CRITTER_MAIL].sort_y = perchSort(w, m.on_tent);
        toStage(m, MB_FLIT);
        break;
      }
      c->feet = perchOf(w, m.on_tent);   // rides the tent's sway
      if (c->carry == CC_LETTER && m.t >= kDropAt) {   // lets the letter go: it flutters down
        c->carry = CC_NONE;
        m.letter = true;
        m.lt = 0.f;
        m.lp0 = Vec2{c->feet.x + (float)c->facing * 6.f, c->feet.y - 4.f};
        m.lp = letterRest(w, m.on_tent, c->feet);
      }
      // Small moves now and then: a look round, a turn, a hop, preening.
      if (m.idle != MI_NONE && m.t >= 1.4f && c->action != CA_IDLE) critterSetAction(*c, CA_IDLE, 0.3f);
      if (m.t >= m.next + kDropAt) {
        const float r = rnd(m, 0.f, 1.f);
        if (r < 0.3f) {
          m.idle = MI_LOOK;
          c->facing = -c->facing;   // a quick turn, as small birds do
          critterSetAction(*c, CA_LOOK, 0.25f);
        } else if (r < 0.55f) {
          m.idle = MI_HOP;
          if (rnd(m, 0.f, 1.f) < 0.5f) c->facing = -c->facing;
          critterSetAction(*c, CA_HOP, 0.35f);
        } else if (r < 0.85f) {
          m.idle = MI_PREEN;
          critterSetAction(*c, CA_EAT, 0.2f);   // the peck, at its own wing
        } else {
          m.idle = MI_LOOK;
          critterSetAction(*c, CA_LOOK, 0.3f);
        }
        m.t = kDropAt;
        m.next = rnd(m, 2.5f, 6.f);
      }
      // The hiker glances up at it now and then, while he stands or sits still.
      m.glance_next -= kDt;
      if (m.glance_next <= 0.f) {
        const Hiker& h = w.hiker;
        if (h.visible && !h.walking && !w.react.active && !w.fi.on && !w.busy) m.glance = kGlanceS;
        m.glance_next = rnd(m, 10.f, 22.f);
      }
      break;
    }
    case MB_FLIT: {
      if (!c) {
        toStage(m, MB_NONE);
        break;
      }
      const Vec2 b = perchOf(w, m.on_tent);
      const float u = clamp01(m.t / kFlitS);
      flyAlong(*c, m.a, Vec2{0.5f * (m.a.x + b.x), minf(m.a.y, b.y) - 30.f}, b, easeInOut(u));
      if (u >= 1.f) {
        c->facing = faceCamp(w, b);
        critterSetAction(*c, CA_IDLE, 0.25f);
        m.idle = MI_NONE;
        m.next = rnd(m, 2.5f, 5.f);
        toStage(m, MB_PERCH);
        m.t = kDropAt;   // the letter was dropped already
      }
      break;
    }
    case MB_OFF: {
      if (!c) {
        toStage(m, MB_NONE);
        break;
      }
      if (m.t < 0.3f) break;   // the hop first
      if (c->action != CA_MOVE) critterSetAction(*c, CA_MOVE, 0.12f);
      const Vec2 b{-30.f, 110.f};
      const float u = clamp01((m.t - 0.3f) / kOffS);
      flyAlong(*c, m.a, Vec2{lerp(m.a.x, b.x, 0.4f), m.a.y - 50.f}, b, easeInOut(u));
      if (u >= 1.f) {
        directorRelease(w, CRITTER_MAIL);
        m.idle = MI_NONE;
        toStage(m, MB_NONE);
      }
      break;
    }
    default: break;
  }
  if (m.glance > 0.f) m.glance -= kDt;
}

void mailPose(const World& w, HikerPose& p) {
  const MailBird& m = w.mail;
  if (m.glance <= 0.f || w.react.active || w.fi.on) return;
  const float t = kGlanceS - m.glance;   // seconds into it
  const float k = easeInOut(clamp01(minf(t / 0.35f, m.glance / 0.35f)));
  p.head -= 0.3f * k;   // chin up, looking up at it
}

namespace {
// The letter's corners: lying on the ground (a flat envelope seen from above it), or falling, tipping.
void letterPts(const MailBird& m, Vec2 out[4], Vec2& flap) {
  const float u = m.lt / kFallS;
  const Vec2 p{lerp(m.lp0.x, m.lp.x, u) + 3.f * sinT(u * 1.5f) * (1.f - u), lerp(m.lp0.y, m.lp.y, u * u)};
  const float tip = 0.35f * sinT(u * 2.5f) * (1.f - u);
  const float h = u >= 1.f ? 3.4f : 4.8f;   // flat on the ground it is foreshortened
  const float c = cosT(tip * 0.159f), s = sinT(tip * 0.159f);
  const float pts[4][2] = {{-4.f, -h}, {4.f, -h}, {4.f, 0.f}, {-4.f, 0.f}};
  for (int i = 0; i < 4; ++i) out[i] = Vec2{p.x + pts[i][0] * c - pts[i][1] * s, p.y + pts[i][0] * s + pts[i][1] * c};
  flap = Vec2{p.x - (-0.55f * h) * s, p.y + (-0.55f * h) * c};
}
}  // namespace

bool mailLetterY(const World& w, int16_t& y) {
  if (!w.mail.letter) return false;
  y = (int16_t)floorf(w.mail.lp.y);
  return true;
}

Rect mailLetterBounds(const World& w) {
  const MailBird& m = w.mail;
  if (!m.letter) return Rect{0, 0, 0, 0};
  Vec2 q[4], f;
  letterPts(m, q, f);
  float x0 = q[0].x, y0 = q[0].y, x1 = x0, y1 = y0;
  for (const Vec2& v : q) {
    x0 = minf(x0, v.x);
    y0 = minf(y0, v.y);
    x1 = maxf(x1, v.x);
    y1 = maxf(y1, v.y);
  }
  return Rect{(int)floorf(x0) - 2, (int)floorf(y0) - 2, (int)ceilf(x1) + 2, (int)ceilf(y1) + 2};
}

void mailDrawLetter(const World& w, Surface& s, const Rect& clip, const Palette& pal) {
  const MailBird& m = w.mail;
  if (!m.letter || mailLetterBounds(w).clipped(clip).empty()) return;
  Vec2 q[4], f;
  letterPts(m, q, f);
  fillPolygon(s, q, 4, pal[CLOUD_LIT], clip);
  // The envelope's flap: a thin V from the top corners to the middle.
  const Pixel line = mixRGB(pal[CLOUD_LIT], pal[ACTOR], 0.55f);
  strokeCapsule(s, q[0], f, 0.7f, line, clip);
  strokeCapsule(s, q[1], f, 0.7f, line, clip);
}

}  // namespace ss
