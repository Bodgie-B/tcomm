// One line of JSON from the Pi's bridge: a flat object of strings, numbers and booleans. Nested
// objects and arrays are read past (and so allowed), but nothing in them is kept. Plain C++, so
// the host tests run it too.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gw {

enum class JType : uint8_t { Int, Number, String, Bool, Null, Other };

struct JField {
  char key[24];  // longer keys are cut (they're never ones the gateway uses)
  JType type;
  int64_t i;     // Int
  bool b;        // Bool
  // String: ASCII only, as decoded; anything outside 0x20 to 0x7E is "?" (one per character),
  // and only the first sizeof(s) - 1 characters are kept.
  char s[64];
};

struct JObject {
  JField f[12];  // fields after the twelfth are read and dropped
  int n = 0;
  const JField *get(const char *key) const;
};

// True if text (len bytes, no newline) is one JSON object, with only whitespace around it.
bool json_parse_object(const char *text, size_t len, JObject &out);

}  // namespace gw
