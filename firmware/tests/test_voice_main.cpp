// Runs every voice-notes host test (tests/test_voice_*.cpp, VTEST blocks; tests/voice_test.h).
// Usage: test_voice.exe [vectors folder]  (default tests/vectors). Exit 0 only if every check passed.
#include "voice_test.h"
#include <stdlib.h>
#include <string.h>

namespace vt {
namespace {

struct Entry {
  const char *name;
  Fn fn;
};
std::vector<Entry> &registry() {
  static std::vector<Entry> r;  // built before main by the static Reg objects
  return r;
}
const char *g_test = "";
const char *g_vectors = "tests/vectors";
int g_pass = 0, g_fail = 0;

bool read_lines(const char *name, std::vector<std::string> &lines) {
  std::string path = std::string(g_vectors) + "/" + name;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) {
    printf("  cannot open %s\n", path.c_str());
    return false;
  }
  std::string cur;
  int c;
  while ((c = fgetc(f)) != EOF) {
    if (c == '\n') {
      if (!cur.empty() && cur[0] != '#') lines.push_back(cur);
      cur.clear();
    } else if (c != '\r') {
      cur += (char)c;
    }
  }
  if (!cur.empty() && cur[0] != '#') lines.push_back(cur);
  fclose(f);
  return true;
}

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

Reg::Reg(const char *name, Fn fn) { registry().push_back({name, fn}); }

void check(bool ok, const char *expr, const char *file, int line) {
  if (ok) {
    g_pass++;
    return;
  }
  g_fail++;
  printf("FAIL %s: %s (%s:%d)\n", g_test, expr, file, line);
}

const char *vectors_dir() { return g_vectors; }

bool read_samples(const char *name, std::vector<int16_t> &out) {
  std::vector<std::string> lines;
  if (!read_lines(name, lines)) return false;
  for (const std::string &l : lines) {
    if (l.size() % 4) return false;
    for (size_t i = 0; i < l.size(); i += 4) {
      int v = 0;
      for (int k = 0; k < 4; k++) {
        int h = hexval(l[i + k]);
        if (h < 0) return false;
        v = v * 16 + h;
      }
      out.push_back((int16_t)(uint16_t)v);
    }
  }
  return true;
}

bool read_chunks(const char *name, std::vector<Chunk> &out) {
  std::vector<std::string> lines;
  if (!read_lines(name, lines)) return false;
  for (const std::string &l : lines) {
    Chunk c;
    int len = 0, used = 0;
    if (sscanf(l.c_str(), "%d %d %d %d %n", &c.idx, &c.pred, &c.step, &len, &used) != 4) return false;
    const char *h = l.c_str() + used;
    if ((int)strlen(h) != len * 2) return false;
    for (int i = 0; i < len; i++) {
      int a = hexval(h[2 * i]), b = hexval(h[2 * i + 1]);
      if (a < 0 || b < 0) return false;
      c.data.push_back((uint8_t)(a * 16 + b));
    }
    out.push_back(c);
  }
  return true;
}

}  // namespace vt

int main(int argc, char **argv) {
  if (argc > 1) vt::g_vectors = argv[1];
  for (const vt::Entry &e : vt::registry()) {
    vt::g_test = e.name;
    int before = vt::g_fail;
    e.fn();
    if (vt::g_fail != before) printf("  in %s\n", e.name);
  }
  printf("voice: %d tests, %d passed, %d failed\n", (int)vt::registry().size(), vt::g_pass, vt::g_fail);
  return vt::g_fail ? 1 : 0;
}
