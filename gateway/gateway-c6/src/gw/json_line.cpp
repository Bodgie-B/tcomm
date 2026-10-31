#include "json_line.h"
#include <string.h>

namespace gw {

namespace {

struct P {
  const char *s;
  size_t n, i = 0;
  int depth = 0;

  bool end() const { return i >= n; }
  char peek() const { return i < n ? s[i] : 0; }
  void ws() {
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
  }
  bool eat(char c) {
    ws();
    if (peek() != c) return false;
    i++;
    return true;
  }
  bool word(const char *w) {
    size_t k = strlen(w);
    if (n - i < k || memcmp(s + i, w, k) != 0) return false;
    i += k;
    return true;
  }

  static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  // A string, decoded into out (cap bytes with its terminator; out may be null to skip it).
  bool str(char *out, size_t cap) {
    ws();
    if (peek() != '"') return false;
    i++;
    size_t k = 0;
    auto put = [&](char c) {
      if (out && k + 1 < cap) out[k++] = c;
    };
    while (true) {
      if (end()) return false;
      unsigned char c = (unsigned char)s[i++];
      if (c == '"') break;
      if (c == '\\') {
        if (end()) return false;
        char e = s[i++];
        switch (e) {
          case '"': put('"'); break;
          case '\\': put('\\'); break;
          case '/': put('/'); break;
          case 'b': case 'f': case 'n': case 'r': case 't': put('?'); break;
          case 'u': {
            if (n - i < 4) return false;
            int v = 0;
            for (int q = 0; q < 4; q++) {
              int h = hex(s[i + q]);
              if (h < 0) return false;
              v = v * 16 + h;
            }
            i += 4;
            if (v >= 0xDC00 && v <= 0xDFFF) break;  // the second half of a pair: its "?" is out
            put(v >= 0x20 && v <= 0x7E ? (char)v : '?');
            break;
          }
          default: return false;
        }
      } else if (c >= 0x80) {
        if ((c & 0xC0) != 0x80) put('?');  // one "?" per UTF-8 character: its first byte
      } else {
        put(c >= 0x20 && c <= 0x7E ? (char)c : '?');
      }
    }
    if (out) out[k] = 0;
    return true;
  }

  // A number: an integer into v (Int), or anything else valid (Number).
  bool num(JType &t, int64_t &v) {
    ws();
    size_t start = i;
    bool neg = false;
    if (peek() == '-') {
      neg = true;
      i++;
    }
    if (end() || s[i] < '0' || s[i] > '9') return false;
    bool overflow = false;
    uint64_t u = 0;
    if (s[i] == '0') {
      i++;
    } else {
      while (!end() && s[i] >= '0' && s[i] <= '9') {
        uint64_t d = (uint64_t)(s[i] - '0');
        if (u > (UINT64_C(9223372036854775807) - d) / 10) overflow = true;
        else u = u * 10 + d;
        i++;
      }
    }
    bool integer = true;
    if (peek() == '.') {
      integer = false;
      i++;
      if (end() || s[i] < '0' || s[i] > '9') return false;
      while (!end() && s[i] >= '0' && s[i] <= '9') i++;
    }
    if (peek() == 'e' || peek() == 'E') {
      integer = false;
      i++;
      if (peek() == '+' || peek() == '-') i++;
      if (end() || s[i] < '0' || s[i] > '9') return false;
      while (!end() && s[i] >= '0' && s[i] <= '9') i++;
    }
    (void)start;
    if (integer && !overflow) {
      t = JType::Int;
      v = neg ? -(int64_t)u : (int64_t)u;
    } else {
      t = JType::Number;
      v = 0;
    }
    return true;
  }

  bool skip_value();

  bool skip_container(char open, char close) {
    if (++depth > 16) return false;
    i++;  // the opener
    ws();
    if (peek() == close) {
      i++;
      depth--;
      return true;
    }
    while (true) {
      if (open == '{') {
        if (!str(nullptr, 0) || !eat(':')) return false;
      }
      if (!skip_value()) return false;
      ws();
      if (peek() == ',') {
        i++;
        continue;
      }
      if (peek() == close) {
        i++;
        depth--;
        return true;
      }
      return false;
    }
  }
};

bool P::skip_value() {
  ws();
  char c = peek();
  if (c == '"') return str(nullptr, 0);
  if (c == '{') return skip_container('{', '}');
  if (c == '[') return skip_container('[', ']');
  if (word("true") || word("false") || word("null")) return true;
  JType t;
  int64_t v;
  return num(t, v);
}

}  // namespace

const JField *JObject::get(const char *key) const {
  for (int k = 0; k < n; k++)
    if (!strcmp(f[k].key, key)) return &f[k];
  return nullptr;
}

bool json_parse_object(const char *text, size_t len, JObject &out) {
  out.n = 0;
  P p{text, len};
  if (!p.eat('{')) return false;
  p.ws();
  if (p.peek() == '}') {
    p.i++;
  } else {
    while (true) {
      JField tmp;
      JField &f = out.n < (int)(sizeof(out.f) / sizeof(out.f[0])) ? out.f[out.n] : tmp;
      memset(&f, 0, sizeof(f));
      if (!p.str(f.key, sizeof(f.key)) || !p.eat(':')) return false;
      p.ws();
      char c = p.peek();
      if (c == '"') {
        f.type = JType::String;
        if (!p.str(f.s, sizeof(f.s))) return false;
      } else if (c == '{' || c == '[') {
        f.type = JType::Other;
        if (!p.skip_value()) return false;
      } else if (p.word("true")) {
        f.type = JType::Bool;
        f.b = true;
      } else if (p.word("false")) {
        f.type = JType::Bool;
        f.b = false;
      } else if (p.word("null")) {
        f.type = JType::Null;
      } else if (!p.num(f.type, f.i)) {
        return false;
      }
      if (&f != &tmp) out.n++;
      p.ws();
      if (p.peek() == ',') {
        p.i++;
        continue;
      }
      if (p.peek() == '}') {
        p.i++;
        break;
      }
      return false;
    }
  }
  p.ws();
  return p.end();
}

}  // namespace gw
