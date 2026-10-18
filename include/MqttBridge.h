#pragma once
#include <Arduino.h>
#include <atomic>
#include <mqtt_client.h>

struct BrokerEvent {
  uint8_t kind;
  char topic[200];
  char payload[512];
};

struct BrokerCommand {
  uint32_t generation;
  char topic[200];
  char payload[256];
};

class MqttBridge {
 public:
  bool begin();
  bool connected() const { return connected_.load(); }
  bool receive(BrokerEvent &event);
  bool publish(const String &topic, const String &payload);
  void subscribe(const String &topic);
  bool consumeOverflow() { return overflow_.exchange(false); }
 private:
  static void event(void *argument, esp_event_base_t base, int32_t kind, void *data);
  static void publisher(void *argument);
  esp_mqtt_client_handle_t client_ = nullptr;
  QueueHandle_t incoming_ = nullptr;
  QueueHandle_t outgoing_ = nullptr;
  std::atomic<bool> connected_{false};
  std::atomic<bool> overflow_{false};
  std::atomic<uint32_t> generation_{0};
  BrokerEvent partial_ = {};
  int received_ = 0;
};
