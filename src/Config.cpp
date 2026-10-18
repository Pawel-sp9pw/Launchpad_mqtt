#include "Config.h"
#include "NvsGuard.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_wifi.h>

Config config;
String configSource = "unconfigured";

bool validConfig(const Config &next) {
  if (next.ssid.length() > 32 || next.wifiPassword.length() > 64 ||
      next.mqttHost.isEmpty() || next.mqttHost.length() > 128 || !next.mqttPort ||
      next.mqttUser.length() > 128 || next.mqttPassword.length() > 128 ||
      next.adminPassword.length() < 16 || next.adminPassword.length() > 128 ||
      next.baseTopic.isEmpty() || next.baseTopic.length() > 64) return false;
  if (next.baseTopic.indexOf('#') >= 0 || next.baseTopic.indexOf('+') >= 0) return false;
  for (uint8_t index = 0; index < 4; ++index) {
    if (next.bulbs[index].isEmpty() || next.bulbs[index].length() > 64 ||
        next.bulbs[index].indexOf('#') >= 0 || next.bulbs[index].indexOf('+') >= 0)
      return false;
    for (uint8_t other = 0; other < index; ++other)
      if (next.bulbs[index] == next.bulbs[other]) return false;
  }
  return true;
}

bool saveConfig(const Config &next) {
  if (!nvsHealthy || !validConfig(next)) return false;
  StaticJsonDocument<2048> document;
  document["schema"] = 1;
  document["ssid"] = next.ssid;
  document["wifi_pass"] = next.wifiPassword;
  document["mqtt_host"] = next.mqttHost;
  document["mqtt_port"] = next.mqttPort;
  document["mqtt_user"] = next.mqttUser;
  document["mqtt_pass"] = next.mqttPassword;
  document["base"] = next.baseTopic;
  document["admin"] = next.adminPassword;
  document["auto"] = next.autoUpdate;
  document["confirmed"] = next.confirmed;
  JsonArray names = document.createNestedArray("bulbs");
  for (const String &name : next.bulbs) names.add(name);
  String encoded;
  serializeJson(document, encoded);
  Preferences preferences;
  if (!preferences.begin("lp_mqtt_v1", false)) return false;
  const bool saved = preferences.putString("config", encoded) == encoded.length();
  preferences.end();
  return saved;
}

bool loadConfig() {
  if (!nvsHealthy) {
    configSource = "nvs-incompatible-preserved-read-only";
    return false;
  }
  Preferences preferences;
  if (preferences.begin("lp_mqtt_v1", true)) {
    const String encoded = preferences.getString("config", "");
    preferences.end();
    if (!encoded.isEmpty()) {
      StaticJsonDocument<2048> document;
      if (deserializeJson(document, encoded) || document["schema"] != 1) {
        configSource = "unsupported-or-corrupt-schema";
        return false;
      }
      config.ssid = document["ssid"].as<String>();
      config.wifiPassword = document["wifi_pass"].as<String>();
      config.mqttHost = document["mqtt_host"].as<String>();
      config.mqttPort = document["mqtt_port"] | 1883;
      config.mqttUser = document["mqtt_user"].as<String>();
      config.mqttPassword = document["mqtt_pass"].as<String>();
      config.baseTopic = document["base"].as<String>();
      config.adminPassword = document["admin"].as<String>();
      config.autoUpdate = document["auto"] | true;
      config.confirmed = document["confirmed"] | false;
      for (uint8_t index = 0; index < 4; ++index)
        config.bulbs[index] = document["bulbs"][index].as<String>();
      configSource = validConfig(config) ? "lp_mqtt_v1/schema1" : "invalid-config";
      return validConfig(config);
    }
  }
  wifi_config_t stored = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &stored) == ESP_OK && stored.sta.ssid[0]) {
    config.ssid = String(reinterpret_cast<char *>(stored.sta.ssid),
                         strnlen(reinterpret_cast<char *>(stored.sta.ssid), 32));
    config.wifiPassword = String(reinterpret_cast<char *>(stored.sta.password),
                                 strnlen(reinterpret_cast<char *>(stored.sta.password), 64));
    configSource = "esp_wifi-driver-read-only";
  }
  return false;
}
