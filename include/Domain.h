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

inline bool parseBounded(const char *value, uint32_t maximum, uint32_t &result) {
  if (!value || !*value) return false;
  uint32_t parsed = 0;
  while (*value) {
    if (*value < '0' || *value > '9') return false;
    const uint32_t digit = *value++ - '0';
    if (parsed > maximum / 10 ||
        (parsed == maximum / 10 && digit > maximum % 10)) return false;
    parsed = parsed * 10 + digit;
  }
  result = parsed;
  return true;
}

enum class ControlResult { Ready, Offline, Invalid };

inline ControlResult control(Bulb &bulb, bool brokerConnected, const char *action,
                             uint8_t level, uint8_t color, bool warm) {
  if (!brokerConnected || !bulb.reachable) return ControlResult::Offline;
  if (!action) return ControlResult::Invalid;
  if (strcmp(action, "on") == 0) {
    if (!bulb.brightness) bulb.brightness = 8;
  } else if (strcmp(action, "off") == 0) {
    bulb.brightness = 0;
  } else if (strcmp(action, "warm") == 0) {
    bulb.brightness = 8;
    bulb.color = 4;
    bulb.warmPreset = true;
  } else if (strcmp(action, "apply") == 0) {
    if (level > 8 || color > 7) return ControlResult::Invalid;
    bulb.brightness = level;
    bulb.color = color;
    bulb.warmPreset = warm;
  } else return ControlResult::Invalid;
  return ControlResult::Ready;
}
}
