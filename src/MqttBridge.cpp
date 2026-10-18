#include "MqttBridge.h"
#include "Config.h"

bool MqttBridge::begin() {
  incoming_ = xQueueCreate(24, sizeof(BrokerEvent));
  outgoing_ = xQueueCreate(16, sizeof(BrokerCommand));
  if (!incoming_ || !outgoing_) return false;
  esp_mqtt_client_config_t settings = {};
  settings.host = config.mqttHost.c_str();
  settings.port = config.mqttPort;
  settings.username = config.mqttUser.isEmpty() ? nullptr : config.mqttUser.c_str();
  settings.password = config.mqttPassword.isEmpty() ? nullptr : config.mqttPassword.c_str();
  settings.client_id = "launchpad-esp32";
  settings.keepalive = 30;
  settings.reconnect_timeout_ms = 5000;
  settings.network_timeout_ms = 3000;
  settings.buffer_size = 1024;
  settings.task_stack = 6144;
  client_ = esp_mqtt_client_init(&settings);
  if (!client_ || esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, event, this) != ESP_OK)
    return false;
  if (xTaskCreate(publisher, "mqtt-publish", 4096, this, 1, nullptr) != pdPASS) return false;
  return esp_mqtt_client_start(client_) == ESP_OK;
}

bool MqttBridge::receive(BrokerEvent &message) {
  return incoming_ && xQueueReceive(incoming_, &message, 0) == pdTRUE;
}

bool MqttBridge::publish(const String &topic, const String &payload) {
  if (!connected() || !outgoing_ || topic.length() >= 200 || payload.length() >= 256)
    return false;
  BrokerCommand command = {};
  command.generation = generation_;
  strlcpy(command.topic, topic.c_str(), sizeof(command.topic));
  strlcpy(command.payload, payload.c_str(), sizeof(command.payload));
  return xQueueSend(outgoing_, &command, 0) == pdTRUE;
}

void MqttBridge::subscribe(const String &topic) {
  if (connected() && esp_mqtt_client_subscribe(client_, topic.c_str(), 1) < 0)
    overflow_ = true;
}

void MqttBridge::publisher(void *argument) {
  auto *bridge = static_cast<MqttBridge *>(argument);
  BrokerCommand command;
  for (;;) {
    if (xQueueReceive(bridge->outgoing_, &command, portMAX_DELAY) != pdTRUE) continue;
    if (bridge->connected() && command.generation == bridge->generation_)
      esp_mqtt_client_publish(bridge->client_, command.topic, command.payload, 0, 0, 0);
  }
}

void MqttBridge::event(void *argument, esp_event_base_t, int32_t kind, void *data) {
  auto *bridge = static_cast<MqttBridge *>(argument);
  const auto *eventData = static_cast<esp_mqtt_event_handle_t>(data);
  BrokerEvent message = {};
  if (kind == MQTT_EVENT_CONNECTED || kind == MQTT_EVENT_DISCONNECTED) {
    bridge->connected_ = kind == MQTT_EVENT_CONNECTED;
    ++bridge->generation_;
    message.kind = kind == MQTT_EVENT_CONNECTED ? 1 : 2;
    if (xQueueSend(bridge->incoming_, &message, 0) != pdTRUE) bridge->overflow_ = true;
    return;
  }
  if (kind != MQTT_EVENT_DATA) return;
  if (eventData->current_data_offset == 0) {
    bridge->received_ = 0;
    bridge->partial_ = {};
    if (eventData->topic_len <= 0 || eventData->topic_len >= 200 ||
        eventData->total_data_len <= 0 || eventData->total_data_len >= 512) return;
    memcpy(bridge->partial_.topic, eventData->topic, eventData->topic_len);
    bridge->partial_.kind = 3;
  }
  if (bridge->partial_.kind != 3 || eventData->current_data_offset != bridge->received_ ||
      eventData->data_len < 0 || bridge->received_ + eventData->data_len >= 512) return;
  memcpy(bridge->partial_.payload + bridge->received_, eventData->data, eventData->data_len);
  bridge->received_ += eventData->data_len;
  if (bridge->received_ == eventData->total_data_len) {
    if (xQueueSend(bridge->incoming_, &bridge->partial_, 0) != pdTRUE)
      bridge->overflow_ = true;
    bridge->partial_.kind = 0;
  }
}
