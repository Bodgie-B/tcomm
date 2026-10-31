#include "sht4x.h"

namespace tc {
namespace sht4x {

uint8_t crc8(const uint8_t *data, size_t n) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

Reading decode(const uint8_t raw[6]) {
  Reading r{false, 0, 0};
  if (crc8(raw, 2) != raw[2] || crc8(raw + 3, 2) != raw[5]) return r;
  uint32_t t = (uint32_t)(raw[0] << 8 | raw[1]);
  uint32_t h = (uint32_t)(raw[3] << 8 | raw[4]);
  // Integer forms of the datasheet formulas, rounded to the nearest tenth.
  r.temp_tenths_f = (int16_t)((int32_t)((3150u * t + 32767u) / 65535u) - 490);
  int32_t rh = (int32_t)((1250u * h + 32767u) / 65535u) - 60;
  r.hum_tenths = (uint16_t)(rh < 0 ? 0 : rh > 1000 ? 1000 : rh);
  r.ok = true;
  return r;
}

bool decode_serial(const uint8_t raw[6], uint32_t *serial) {
  if (crc8(raw, 2) != raw[2] || crc8(raw + 3, 2) != raw[5]) return false;
  if (serial) *serial = (uint32_t)raw[0] << 24 | (uint32_t)raw[1] << 16 | (uint32_t)raw[3] << 8 | raw[4];
  return true;
}

}  // namespace sht4x
}  // namespace tc
