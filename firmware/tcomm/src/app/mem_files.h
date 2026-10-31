// The calendar's files kept in memory (Platform::read_file and write_file): the demo's simulated
// panel's "SD card", and the PC's tests and simulator.
#pragma once
#include <map>
#include <string>
#include <string.h>
#include <vector>
#include "controller.h"

namespace tc {

class MemFiles {
 public:
  uint8_t *read(Platform &p, const char *name, size_t &len) {
    len = 0;
    auto it = files.find(name);
    if (it == files.end()) return nullptr;
    uint8_t *b = (uint8_t *)p.alloc_big(it->second.size() ? it->second.size() : 1);
    if (!b) return nullptr;
    memcpy(b, it->second.data(), it->second.size());
    len = it->second.size();
    return b;
  }
  void write(Platform &p, const char *name, uint8_t *buf, size_t len) {
    files[name].assign(buf, buf + len);
    writes[name]++;
    p.free_big(buf);
  }
  std::map<std::string, std::vector<uint8_t>> files;
  std::map<std::string, int> writes;
};

}  // namespace tc
