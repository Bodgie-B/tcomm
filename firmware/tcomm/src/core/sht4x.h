// Sensirion SHT40 temperature and humidity sensor: the commands TComm uses and the conversion of
// its 6-byte answer. Plain C++ (tested on the PC); the I2C transfers are in board/.
// Datasheet: Sensirion SHT4x, sections 4.4 (commands), 4.5 (conversion) and 4.4.3 (CRC-8).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tc {
namespace sht4x {

constexpr uint8_t kAddress = 0x44;      // SHT40-AD1B; the -BD1B variant is 0x45
constexpr uint8_t kMeasureHigh = 0xFD;  // high repeatability, up to 8.3 ms
constexpr uint8_t kReadSerial = 0x89;
constexpr uint8_t kSoftReset = 0x94;
constexpr uint32_t kMeasureMs = 10;     // wait after kMeasureHigh before reading

// Sensirion's CRC-8: polynomial 0x31, initial value 0xFF, no reflection (crc8({0xBE, 0xEF}) = 0x92).
uint8_t crc8(const uint8_t *data, size_t n);

struct Reading {
  bool ok;                // both CRCs matched
  int16_t temp_tenths_f;  // 715 = 71.5 F
  uint16_t hum_tenths;    // 442 = 44.2 %, clamped to 0..100 % as the datasheet says
};

// The answer to kMeasureHigh: T MSB, T LSB, CRC, RH MSB, RH LSB, CRC.
// T = -45 + 175 * raw / 65535 C (in tenths of a degree F: -490 + 3150 * raw / 65535, rounded);
// RH = -6 + 125 * raw / 65535 %.
Reading decode(const uint8_t raw[6]);

// The answer to kReadSerial: two 16-bit words, each with its CRC. True if both CRCs match.
bool decode_serial(const uint8_t raw[6], uint32_t *serial);

}  // namespace sht4x
}  // namespace tc
