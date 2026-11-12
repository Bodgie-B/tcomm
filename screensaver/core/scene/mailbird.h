// Mail by bird (stage 14, Mike 11 Nov): the DOWNSTAIRS panel's screensaver.
//
// When Mike marks "mail" upstairs, the downstairs screensaver shows its mail badge (the host's
// overlay, ss_host.cpp) and now a bird brings it: it flies in from the upper right with a letter in
// its beak, lands on the tent's ridge (its back pole, out of the way of the morning's ridge birds),
// or on the camp pine's bag branch while there is no tent up, and drops the letter to the ground
// below. Then it stays, with small idle moves now and then (a look round, a turn, a hop, preening),
// until the mail is acknowledged: the letter has been collected (it is gone) and the bird takes off
// and leaves to the upper left. If the tent goes up or comes down while it waits, it flits across
// to the other perch. If mail is already waiting when a loop begins (the screensaver starting, or
// the next loop) the bird and its letter are simply there once the camp has built up.
//
// The hiker glances up at it now and then while he stands or sits still.
//
// The bird is a CR_BIRD critter in its own director slot (CRITTER_MAIL, world.h), tagged CT_MAIL
// and CF_BRAVE: nobody scatters it, the story's critters keep all their slots, and no beat waits
// for it (directorCount leaves it out). The Lookout: no bird (its ledge has no tent or camp pine);
// it comes in the next camp loop. Inputs::mail false: nothing here runs, draws or costs anything.
//
// Tuning marked PROPOSED is a guess; how it looks is UNVERIFIED on the panel.
#pragma once

#include "../gfx/raster.h"
#include "../palette.h"
#include "hiker.h"

namespace ss {

struct World;

enum MailStage : uint8_t {
  MB_NONE = 0,   // no bird
  MB_IN,         // flying in with the letter
  MB_PERCH,      // sitting on its perch
  MB_FLIT,       // across to the other perch (the tent went up or came down)
  MB_OFF         // the mail was collected: flying off
};

struct MailBird {
  bool want;         // Inputs::mail, this tick
  uint8_t stage;     // MailStage
  uint8_t idle;      // the idle move it is doing (mailbird.cpp)
  bool on_tent;      // its perch is the tent's ridge (else the branch)
  bool there;        // mail was waiting as the loop began: no flight in, it is simply there
  float t;           // seconds in the stage
  float next;        // seconds to the next idle move
  float glance;      // the hiker's glance: seconds left of it (< 0 none)
  float glance_next; // seconds to the next one
  float perch_t;     // seconds the other perch has been the right one (it flits after a moment)
  uint32_t rng;      // its own stream (from the loop seed)
  Vec2 a;            // where a flight began (it ends at the perch, or off the frame)
  // The letter: lying on the ground at `lp` (or falling to it from `lp0` while lt < kFall).
  bool letter;
  float lt;
  Vec2 lp0, lp;
  Rect d_letter;
};

// Loop start, after the World is cleared: its stream from the loop seed, and whether mail is
// waiting already (Inputs::mail).
void mailBeginLoop(World& w, uint64_t loop_seed, bool mail_now);
// Inputs::mail this tick (app.cpp, every tick).
void mailSetInputs(World& w, bool mail);
// Every tick from worldTick (also while fire and ice holds the story), before the director.
void mailStep(World& w);
// The hiker's glance up at it, over his composed pose (stepHiker).
void mailPose(const World& w, HikerPose& p);
// The letter on the ground: sorted among the ground actors (ACT_MAIL); its dirty marks.
bool mailLetterY(const World& w, int16_t& y);
Rect mailLetterBounds(const World& w);
void mailDrawLetter(const World& w, Surface& s, const Rect& clip, const Palette& pal);

}  // namespace ss
