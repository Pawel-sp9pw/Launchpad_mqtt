#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace domain {
struct Bulb {
  bool known = false;
  bool online = false;
  bool reachable = false;
  bool warmPreset = true;
  uint8_t brightness = 8;
  uint8_t color = 4;
};

inline bool availability(Bulb &bulb, bool online) {
  const bool preset = bulb.known && !bulb.online && online;
  bulb.known = true;
  bulb.online = online;
  bulb.reachable = online;
  if (preset) {
    bulb.brightness = 8;
    bulb.color = 4;
    bulb.warmPreset = true;
  }
  return preset;
}

inline bool pad(uint8_t note, uint8_t &column, uint8_t &row) {
  const uint8_t tens = note / 10;
  const uint8_t units = note % 10;
  if (tens < 1 || tens > 8 || units < 1 || units > 8) return false;
  column = units - 1;
  row = tens - 1;
  return true;
}

inline uint8_t brightness(uint8_t level) {
  if (level > 8) level = 8;
  return (254U * level + 4U) / 8U;
}

inline uint16_t colorTemperature(uint8_t level) {
  if (level > 7) level = 7;
  return 250U + (204U * level + 3U) / 7U;
}

inline uint8_t sysexPacket(const uint8_t *bytes, size_t remaining, uint8_t *packet,
                          uint8_t cable = 0) {
  const uint8_t count = remaining > 3 ? 3 : remaining;
  if (!count) return 0;
  packet[0] = (cable << 4) | (remaining > 3 ? 4 : 4 + count);
  for (uint8_t index = 0; index < 3; ++index)
    packet[index + 1] = index < count ? bytes[index] : 0;
  return count;
}

inline bool parseVersion(const char *value, uint32_t *parts) {
  for (uint8_t part = 0; part < 3; ++part) {
    if (*value < '0' || *value > '9') return false;
    uint32_t number = 0;
    uint8_t digits = 0;
    while (*value >= '0' && *value <= '9') {
      number = number * 10 + (*value++ - '0');
      if (++digits > 4) return false;
    }
    parts[part] = number;
    if (part < 2 && *value++ != '.') return false;
  }
  return *value == '\0';
}

inline bool newer(const char *candidate, const char *current) {
  uint32_t candidateParts[3], currentParts[3];
  if (!parseVersion(candidate, candidateParts) || !parseVersion(current, currentParts))
    return false;
  for (uint8_t part = 0; part < 3; ++part) {
    if (candidateParts[part] != currentParts[part])
      return candidateParts[part] > currentParts[part];
  }
  return false;
}
}
