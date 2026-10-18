#pragma once
#include <Arduino.h>
#include <atomic>

class ReleaseUpdater {
 public:
  void begin();
  void loop(bool enabled);
  bool busy() const { return busy_.load(); }
  bool acquire();
  void release();
  String status();

 private:
  static void task(void *argument);
  void check();
  void setStatus(const String &message);
  SemaphoreHandle_t statusMutex_ = nullptr;
  std::atomic<bool> busy_{false};
  String status_ = "waiting-for-network-and-confirmed-settings";
  uint32_t lastCheck_ = 0;
};

extern ReleaseUpdater releaseUpdater;
