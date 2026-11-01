#pragma once
#include <stdint.h>

struct LightSettings {
  bool automatic = true;
  bool useLightSensor = true;
  uint32_t holdSeconds = 30;
  uint16_t darkThreshold = 50;
  uint8_t brightness = 70;
  uint8_t red = 255, green = 180, blue = 100;
  uint32_t fadeInMs = 700, fadeOutMs = 1200;
};

// Hardware-independent state machine. All elapsed-time arithmetic tolerates millis wrap.
class LightController {
 public:
  LightSettings settings;
  void configure(const LightSettings& next, uint32_t now) {
    const bool modeChanged = settings.automatic != next.automatic;
    settings = next;
    darkInitialized_ = false;
    if (modeChanged && settings.automatic) on_ = false;
    retarget(now);
  }
  void power(bool on, uint32_t now) {
    settings.automatic = false;
    on_ = on;
    retarget(now);
  }
  void tick(uint32_t now, bool motion, uint16_t ambient, bool ready = true) {
    if (!darkInitialized_) {
      dark_ = ambient < settings.darkThreshold;
      darkInitialized_ = true;
    } else if (ambient < settings.darkThreshold) {
      dark_ = true;
    } else if (ambient >= (settings.darkThreshold > 1003 ? 1023 : settings.darkThreshold + 20)) {
      dark_ = false;
    }
    if (settings.automatic) {
      // Once lit, ignore light reflected from our own strip; motion still extends the timer.
      if (ready && motion && (on_ || !settings.useLightSensor || dark_)) {
        lastMotion_ = now;
        if (!on_) { on_ = true; retarget(now); }
      }
      if (on_ && static_cast<uint32_t>(now - lastMotion_) >= settings.holdSeconds * 1000UL) {
        on_ = false;
        retarget(now);
      }
    }
    advance(now);
  }
  bool on() const { return on_; }
  bool dark() const { return dark_; }
  uint16_t channel(unsigned index) const { return current_[index]; }
  uint32_t remainingSeconds(uint32_t now) const {
    if (!settings.automatic || !on_) return 0;
    const uint32_t elapsed = now - lastMotion_;
    const uint32_t duration = settings.holdSeconds * 1000UL;
    return elapsed >= duration ? 0 : (duration - elapsed + 999) / 1000;
  }
 private:
  bool on_ = false, dark_ = false, darkInitialized_ = false;
  uint32_t lastMotion_ = 0, transitionStart_ = 0, duration_ = 0;
  uint16_t current_[3] = {}, start_[3] = {}, target_[3] = {};
  void advance(uint32_t now) {
    const uint32_t elapsed = now - transitionStart_;
    if (elapsed >= duration_) duration_ = 0;
    for (unsigned i = 0; i < 3; ++i) {
      if (!duration_ || elapsed >= duration_) current_[i] = target_[i];
      else current_[i] = static_cast<int32_t>(start_[i]) +
          (static_cast<int32_t>(target_[i]) - start_[i]) * static_cast<int32_t>(elapsed) /
          static_cast<int32_t>(duration_);
    }
  }
  void retarget(uint32_t now) {
    advance(now);
    const uint8_t rgb[] = {settings.red, settings.green, settings.blue};
    for (unsigned i = 0; i < 3; ++i) {
      start_[i] = current_[i];
      target_[i] = on_ ? static_cast<uint32_t>(rgb[i]) * settings.brightness * 1023 / 25500 : 0;
    }
    transitionStart_ = now;
    duration_ = on_ ? settings.fadeInMs : settings.fadeOutMs;
    advance(now);
  }
};
