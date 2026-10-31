#include "dog.h"

namespace tc {

bool DogModel::tap(Floor me, const WallTime &now) {
  DogOne &d = s_.dog[dog_index(own_dog(me))];
  d.out = !d.out;
  d.since = now.valid ? now.utc : 0;
  d.ver++;
  return true;
}

static bool newer(const DogOne &in, const DogOne &cur) {
  if (in.ver != cur.ver) return in.ver > cur.ver;
  if (in.since != cur.since) return in.since > cur.since;
  return in.out && !cur.out;  // a true tie: both panels keep the same one
}

bool DogModel::apply_remote(const DogState &in) {
  bool changed = false;
  for (int i = 0; i < 2; i++)
    if (!(in.dog[i] == s_.dog[i]) && newer(in.dog[i], s_.dog[i])) {
      s_.dog[i] = in.dog[i];
      changed = true;
    }
  return changed;
}

bool DogModel::apply_lucy(bool out, uint32_t since) {
  DogOne &lucy = s_.dog[0];
  if (lucy.out == out) return false;  // already out, or already in
  lucy.out = out;
  lucy.since = since;
  lucy.ver++;
  return true;
}

}  // namespace tc
