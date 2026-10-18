#include "Domain.h"
#include <assert.h>
#include <stdio.h>

int main() {
  domain::Bulb bulb;
  assert(!domain::availability(bulb, true));
  bulb.brightness = 2;
  assert(!domain::availability(bulb, true));
  assert(bulb.brightness == 2);
  assert(!domain::availability(bulb, false));
  assert(!bulb.reachable);
  assert(domain::availability(bulb, true));
  assert(bulb.brightness == 8 && bulb.warmPreset);
  bulb.reachable = false;
  bulb.brightness = 3;
  assert(!domain::availability(bulb, true));
  assert(bulb.brightness == 3);
  assert(domain::brightness(0) == 0);
  assert(domain::brightness(4) == 127);
  assert(domain::brightness(8) == 254);
  assert(domain::brightness(255) == 254);
  for (uint8_t level = 1; level <= 8; ++level)
    assert(domain::brightness(level) > domain::brightness(level - 1));
  assert(domain::colorTemperature(0) == 250);
  assert(domain::colorTemperature(7) == 454);
  uint8_t column = 0, row = 0;
  unsigned count = 0;
  for (unsigned note = 0; note < 128; ++note) {
    if (domain::pad(note, column, row)) {
      ++count;
      assert(column < 8 && row < 8);
      assert(note == unsigned(11 + column + 10 * row));
      assert(column / 2 < 4);
    }
  }
  assert(count == 64);
  const uint8_t message[] = {0xf0, 0, 0x20, 0x29, 2, 0x0d, 0x0e, 1, 0xf7};
  uint8_t packet[4];
  assert(domain::sysexPacket(message, 9, packet) == 3 && packet[0] == 4);
  assert(domain::sysexPacket(message + 3, 6, packet) == 3 && packet[0] == 4);
  assert(domain::sysexPacket(message + 6, 3, packet) == 3 && packet[0] == 7);
  assert(packet[3] == 0xf7);
  assert(domain::sysexPacket(message + 7, 2, packet) == 2 && packet[0] == 6);
  assert(domain::sysexPacket(message + 8, 1, packet) == 1 && packet[0] == 5);
  assert(domain::sysexPacket(message, 0, packet) == 0);
  assert(domain::newer("1.0.1", "1.0.0"));
  assert(domain::newer("1.10.0", "1.9.99"));
  assert(!domain::newer("1.0.0", "1.0.0"));
  assert(!domain::newer("0.9.9", "1.0.0"));
  assert(!domain::newer("1.0.1-rc1", "1.0.0"));
  assert(!domain::newer("1.0.1/../../x", "1.0.0"));
  assert(!domain::newer("999999999999.0.0", "1.0.0"));
  assert(!domain::newer("2.0", "1.0.0"));
  puts("domain tests passed: availability, mapping, SysEx termination, stable version policy");
}
