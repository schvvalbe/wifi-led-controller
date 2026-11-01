#include "LightController.h"
#include <assert.h>
#include <stdio.h>

static LightSettings instant() {
  LightSettings s;
  s.fadeInMs = s.fadeOutMs = 0;
  s.holdSeconds = 2;
  s.brightness = 100;
  s.red = 255; s.green = 0; s.blue = 0;
  return s;
}

int main() {
  LightController c;
  auto s = instant();
  c.configure(s, 0);
  c.tick(0, true, 900);
  assert(!c.on()); // Daylight blocks first motion.
  c.tick(10, true, 10, false);
  assert(!c.on()); // PIR warmup does not activate strip.
  c.tick(20, true, 10);
  assert(c.on() && c.channel(0) == 1023 && c.channel(1) == 0);
  c.tick(1900, true, 900); // Light from the LEDs must not prevent extending the timer.
  assert(c.remainingSeconds(2000) == 2);
  c.tick(3899, false, 900); assert(c.on());
  c.tick(3900, false, 900); assert(!c.on() && c.channel(0) == 0);

  c.power(true, 4000);
  assert(!c.settings.automatic && c.on());
  c.tick(20000, false, 900); assert(c.on()); // Manual on has no timeout.
  c.power(false, 20001);
  c.tick(21000, true, 0); assert(!c.on()); // Manual off ignores motion.
  s.useLightSensor = false;
  c.configure(s, 22000);
  c.tick(22000, true, 1023); assert(c.on());
  c.power(false, 22001); assert(!c.settings.automatic && !c.on());

  s.fadeInMs = 1000; s.fadeOutMs = 2000;
  c.configure(s, 23000); c.power(true, 23000);
  c.tick(23500, false, 10); assert(c.channel(0) == 511);
  c.power(false, 23500); // Reverse an unfinished fade without a jump.
  assert(c.channel(0) == 511);
  c.tick(24500, false, 10); assert(c.channel(0) == 256);
  c.tick(25500, false, 10); assert(c.channel(0) == 0);
  s = instant(); s.brightness = 0;
  c.configure(s, 26000); c.power(true, 26000);
  assert(c.channel(0) == 0); // Zero brightness is valid, no divide-by-zero.
  s.brightness = 100; s.red = 0; s.blue = 255;
  s.automatic = false;
  c.configure(s, 26001);
  assert(c.channel(2) == 1023 && c.channel(0) == 0); // Live color while on.
  s.automatic = true;
  c.configure(s, 26002); assert(!c.on()); // Return to auto starts idle.

  LightController wrap;
  s = instant(); s.useLightSensor = false;
  wrap.configure(s, UINT32_MAX - 1000);
  wrap.tick(UINT32_MAX - 1000, true, 0);
  wrap.tick(998, false, 0); assert(wrap.on());
  wrap.tick(999, false, 0); assert(!wrap.on());
  s.fadeInMs = 1000;
  wrap.configure(s, UINT32_MAX - 500); wrap.power(true, UINT32_MAX - 500);
  wrap.tick(499, false, 0); assert(wrap.channel(0) == 1023);
  wrap.tick(UINT32_MAX - 500, false, 0); assert(wrap.channel(0) == 1023); // Completed fade must not replay after 49 days.

  LightController hysteresis;
  s = instant(); hysteresis.configure(s, 0);
  hysteresis.tick(0, false, 49); assert(hysteresis.dark());
  hysteresis.tick(1, false, 60); assert(hysteresis.dark());
  hysteresis.tick(2, false, 70); assert(!hysteresis.dark());
  s.darkThreshold = 1023;
  hysteresis.configure(s, 3);
  hysteresis.tick(3, false, 1022); assert(hysteresis.dark());
  hysteresis.tick(4, false, 1023); assert(!hysteresis.dark());
  puts("PASS: motion, daylight, warmup, manual override, timer extension, fades, color, zero brightness, hysteresis, millis wrap");
}
