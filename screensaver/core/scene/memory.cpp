// World memory across loops. See memory.h.
#include "memory.h"

#include <stdio.h>

#include "world.h"

namespace ss {

void memoryBeginLoop(World& w, Rng& rng) {
  Memory& m = w.memory;
  // One rng call every loop, used or not, so the sequence never depends on it.
  const uint32_t roll = rng.next();
  if (m.loop_count <= 1 || m.persona_age >= 2) {
    m.brave = (uint16_t)(roll & ((1u << CR_COUNT) - 1u));
    m.persona_age = 0;
  }
  ++m.persona_age;
  m.mouse_tent_runs = 0;
  m.shoe_back = false;
  // Last loop's cairn, one stone shorter (the plan holds the count), stands
  // from the end of the build-up (the arrival beat sets it: during the build
  // the ground it stands on is not there yet).
  w.tent.bite = m.mouse_hole;
}

void memoryEndLoop(World& w) {
  Memory& m = w.memory;
  m.cairn_n = w.cairn.n;
  if (m.mouse_tent_runs >= 2) m.mouse_hole = true;
}

void memoryNoteMouseTent(Memory& m) {
  if (m.mouse_tent_runs < 255) ++m.mouse_tent_runs;
  if (m.mouse_tent_runs >= 2) m.mouse_hole = true;   // the tent has a hole next loop
}

void memoryPushMajor(Memory& m, uint8_t id) {
  if (m.ring_n < MEMORY_RING) {
    m.ring[m.ring_n++] = id;
    return;
  }
  for (int i = 1; i < MEMORY_RING; ++i) m.ring[i - 1] = m.ring[i];
  m.ring[MEMORY_RING - 1] = id;
}

void memoryPushSet(Memory& m, uint32_t set) {
  if (m.sets_n < MEMORY_SETS) {
    m.sets[m.sets_n++] = set;
    return;
  }
  for (int i = 1; i < MEMORY_SETS; ++i) m.sets[i - 1] = m.sets[i];
  m.sets[MEMORY_SETS - 1] = set;
}

bool memoryRecentSet(const Memory& m, uint32_t set) {
  for (int i = 0; i < m.sets_n; ++i)
    if (m.sets[i] == set) return true;
  return false;
}

int memoryMajorAge(const Memory& m, uint8_t id) {
  for (int i = (int)m.ring_n - 1; i >= 0; --i)
    if (m.ring[i] == id) return (int)m.ring_n - 1 - i;
  return -1;
}

bool memoryBrave(const Memory& m, CritterKind k) { return k < CR_COUNT && ((m.brave >> k) & 1u) != 0; }

void memoryApplyPersona(const Memory& m, Critter& c) {
  // The slot's own dials still vary it a little within the kind's mood.
  if (memoryBrave(m, c.kind)) {
    c.speed_dial = 0.7f + 0.3f * c.speed_dial;
    c.jitter_dial = 0.3f * c.jitter_dial;
  } else {
    c.speed_dial = 0.35f * c.speed_dial;
    c.jitter_dial = 0.65f + 0.35f * c.jitter_dial;
  }
}

int memoryFormat(const Memory& m, char* buf, size_t n) {
  if (!buf || n == 0) return 0;
  int k = 0;
  auto put = [&](const char* s) {
    if (k < 0 || (size_t)k >= n) return;
    const int r = snprintf(buf + k, n - (size_t)k, "%s", s);
    if (r > 0) k += r;
  };
  char head[96];
  snprintf(head, sizeof head, "loop=%u cairn=%u shoe=%d stolen=%s hole=%d rod=%d brave=", (unsigned)m.loop_count,
           (unsigned)m.cairn_n, m.shoe_taken ? 1 : 0,
           m.last_stolen ? itemName((ItemId)(m.last_stolen - 1)) : "none", m.mouse_hole ? 1 : 0, m.rod_lost ? 1 : 0);
  put(head);
  bool any = false;
  for (int kind = 0; kind < CR_COUNT; ++kind) {
    if (!memoryBrave(m, (CritterKind)kind)) continue;
    if (any) put(",");
    put(critterName((CritterKind)kind));
    any = true;
  }
  if (!any) put("none");
  put(" ring=");
  for (int i = 0; i < m.ring_n; ++i) {
    if (i) put(",");
    put(majorName(m.ring[i]));
  }
  if (m.ring_n == 0) put("none");
  return k;
}

}  // namespace ss
