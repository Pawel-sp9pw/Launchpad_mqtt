#pragma once
#include <Arduino.h>

struct Config {
  String ssid;
  String wifiPassword;
  String mqttHost = "192.168.211.2";
  uint16_t mqttPort = 1883;
  String mqttUser;
  String mqttPassword;
  String baseTopic = "zigbee2mqtt";
  String bulbs[4] = {"Ala_nocna", "Ala_biurko", "Ala_lewa", "Ala_prawa"};
  String adminPassword;
  bool autoUpdate = true;
  bool confirmed = false;
};

extern Config config;
extern String configSource;
bool loadConfig();
bool saveConfig(const Config &next);
bool validConfig(const Config &next);
