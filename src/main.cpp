#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include <time.h>
#include "Config.h"
#include "Domain.h"
#include "MqttBridge.h"
#include "NvsGuard.h"
#include "ReleaseUpdater.h"
#include "UsbMidiHost.h"

WebServer server(80);
UsbMidiHost midi;
MqttBridge mqtt;
domain::Bulb bulbs[4];
String csrf;
String apName;
bool apActive = false;
bool mqttStarted = false;
bool mqttAttempted = false;
bool otaStarted = false;
bool arduinoActive = false;
bool uploadOwned = false;
bool uploadOkay = false;
bool rebootPending = false;
bool usbWasConnected = false;
bool usbProgrammed = false;
bool configLoaded = false;
uint32_t rebootAt = 0;
uint32_t apStartedAt = 0;
uint32_t lastWifiAttempt = 0;
uint32_t disconnectedSince = 0;
uint32_t wifiHealthySince = 0;
uint32_t uploadLastData = 0;
uint32_t usbReadyAt = 0;
uint32_t lastDraw = 0;
size_t uploadBytes = 0;
uint8_t expectedUploadHash[32];
mbedtls_sha256_context uploadDigest;

String topicFor(uint8_t index, const char *suffix) {
  return config.baseTopic + "/" + config.bulbs[index] + suffix;
}

String escaped(const String &value) {
  String result;
  result.reserve(value.length() + 16);
  for (size_t index = 0; index < value.length(); ++index) {
    switch (value[index]) {
      case '&': result += "&amp;"; break;
      case '<': result += "&lt;"; break;
      case '>': result += "&gt;"; break;
      case '\"': result += "&quot;"; break;
      case '\'': result += "&#39;"; break;
      default: result += value[index];
    }
  }
  return result;
}

bool throughAp() {
  return apActive && server.client().localIP() == WiFi.softAPIP();
}

bool authorized() {
  if (config.adminPassword.isEmpty() ||
      server.authenticate("admin", config.adminPassword.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "Launchpad OTA");
  return false;
}

bool validPost() {
  if (server.arg("csrf") == csrf && !csrf.isEmpty()) return true;
  server.send(403, "text/plain", "Invalid form token. Reload the page.");
  return false;
}

void scheduleReboot() {
  rebootPending = true;
  rebootAt = millis() + 1000;
}

void draw() {
  if (!midi.connected() || !usbProgrammed || !usbReadyAt || int32_t(millis() - usbReadyAt) < 0) return;
  static const uint8_t rgbPalette[8] = {45, 41, 37, 3, 13, 9, 5, 7};
  static const uint8_t cctPalette[8] = {45, 41, 3, 3, 13, 13, 9, 9};
  for (uint8_t index = 0; index < 4; ++index) {
    const bool online = mqtt.connected() && bulbs[index].reachable;
    for (uint8_t row = 0; row < 8; ++row) {
      MidiPacket brightness = {{9, 0x90,
          uint8_t(11 + index * 2 + row * 10),
          uint8_t(online && bulbs[index].brightness > row ? 3 : 0)}};
      MidiPacket color = {{9, 0x90, uint8_t(12 + index * 2 + row * 10),
          uint8_t(online ? (index == 0 ? rgbPalette[row] : cctPalette[row]) : 0)}};
      midi.send(brightness);
      midi.send(color);
    }
  }
  lastDraw = millis();
}

bool commandBulb(uint8_t index) {
  if (index >= 4 || !mqtt.connected() || !bulbs[index].reachable) return false;
  StaticJsonDocument<256> document;
  const auto &bulb = bulbs[index];
  document["state"] = bulb.brightness ? "ON" : "OFF";
  document["brightness"] = domain::brightness(bulb.brightness);
  if (bulb.warmPreset || index != 0) {
    document["color_temp"] = bulb.warmPreset ? 370 : domain::colorTemperature(bulb.color);
  } else {
    static const uint16_t hue[8] = {240, 210, 180, 60, 40, 20, 10, 0};
    static const uint8_t saturation[8] = {100, 100, 60, 0, 30, 70, 90, 100};
    JsonObject color = document.createNestedObject("color");
    color["hue"] = hue[bulb.color];
    color["saturation"] = saturation[bulb.color];
  }
  String payload;
  serializeJson(document, payload);
  return mqtt.publish(topicFor(index, "/set"), payload);
}

void brokerLoop() {
  if (!mqttAttempted && WiFi.status() == WL_CONNECTED && config.confirmed) {
    mqttAttempted = true;
    mqttStarted = mqtt.begin();
  }
  if (!mqttStarted) return;
  if (mqtt.consumeOverflow()) {
    mqtt.invalidateCommands();
    for (auto &bulb : bulbs) bulb.reachable = false;
    for (uint8_t index = 0; index < 4; ++index) {
      mqtt.subscribe(topicFor(index, "/availability"));
      mqtt.subscribe(topicFor(index, ""));
    }
    draw();
  }
  BrokerEvent event;
  uint8_t processed = 0;
  while (processed++ < 16 && mqtt.receive(event)) {
    if (event.kind == 1 || event.kind == 2) {
      for (auto &bulb : bulbs) bulb.reachable = false;
      if (event.kind == 1) {
        for (uint8_t index = 0; index < 4; ++index) {
          mqtt.subscribe(topicFor(index, "/availability"));
          mqtt.subscribe(topicFor(index, ""));
        }
      }
      draw();
      continue;
    }
    StaticJsonDocument<1024> document;
    const bool jsonValid = deserializeJson(document, event.payload) == DeserializationError::Ok;
    for (uint8_t index = 0; index < 4; ++index) {
      if (topicFor(index, "/availability") == event.topic) {
        String state = jsonValid ? String(document["state"] | "") : String(event.payload);
        state.trim();
        state.toLowerCase();
        if (state != "online" && state != "offline") break;
        const bool preset = domain::availability(bulbs[index], state == "online");
        if (state == "offline") mqtt.invalidateCommands();
        if (preset) commandBulb(index);
        draw();
        break;
      }
      if (jsonValid && topicFor(index, "") == event.topic) {
        if (document["state"] == "OFF") bulbs[index].brightness = 0;
        else if (document["brightness"].is<uint8_t>())
          bulbs[index].brightness = std::min(8, (int(document["brightness"]) * 8 + 127) / 254);
        draw();
        break;
      }
    }
  }
}

void midiLoop() {
  if (midi.connected() && !usbWasConnected) {
    usbWasConnected = true;
    usbProgrammed = false;
    usbReadyAt = millis() + 200;
  }
  if (!midi.connected()) {
    usbWasConnected = false;
    usbReadyAt = 0;
    usbProgrammed = false;
    lastDraw = 0;
    return;
  }
  if (usbReadyAt && int32_t(millis() - usbReadyAt) >= 0 && !usbProgrammed) {
    const uint8_t message[] = {0xf0, 0, 0x20, 0x29, 2, 0x0d, 0x0e, 1, 0xf7};
    size_t offset = 0;
    while (offset < sizeof(message)) {
      MidiPacket packet;
      offset += domain::sysexPacket(message + offset, sizeof(message) - offset, packet.bytes);
      midi.send(packet);
    }
    usbReadyAt = millis() + 100;
    usbProgrammed = true;
    lastDraw = 0;
  }
  if (usbProgrammed && int32_t(millis() - usbReadyAt) >= 0 &&
      (!lastDraw || millis() - lastDraw > 1000))
    draw();
  MidiPacket packet;
  uint8_t processed = 0;
  while (processed++ < 32 && midi.receive(packet)) {
    if ((packet.bytes[0] >> 4) != 0 || (packet.bytes[0] & 15) != 9 ||
        (packet.bytes[1] & 0xf0) != 0x90 || !packet.bytes[3]) continue;
    uint8_t column, row;
    if (!domain::pad(packet.bytes[2], column, row)) continue;
    const uint8_t index = column / 2;
    if (!mqtt.connected() || !bulbs[index].reachable) continue;
    if (column % 2 == 0) {
      const uint8_t desired = row + 1;
      bulbs[index].brightness = desired == bulbs[index].brightness ? 0 : desired;
    } else {
      bulbs[index].color = row;
      bulbs[index].warmPreset = false;
    }
    commandBulb(index);
    draw();
  }
}

void startAp() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP(apName.c_str())) {
    apActive = true;
    apStartedAt = millis();
    Serial.printf("Setup AP: %s at http://192.168.4.1\n", apName.c_str());
  }
}

void wifiLoop() {
  const uint32_t now = millis();
  const bool healthy = WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
  if (healthy) {
    disconnectedSince = 0;
    if (!wifiHealthySince) wifiHealthySince = now;
    if (apActive && config.confirmed && now - wifiHealthySince > 60000) {
      WiFi.softAPdisconnect(false);
      WiFi.mode(WIFI_STA);
      apActive = false;
    }
  } else {
    wifiHealthySince = 0;
    if (!disconnectedSince) disconnectedSince = now;
    if (now - lastWifiAttempt > 30000 && !config.ssid.isEmpty() && !releaseUpdater.busy()) {
      lastWifiAttempt = now;
      WiFi.disconnect(false, false);
      WiFi.begin(config.ssid.c_str(), config.wifiPassword.c_str());
    }
    if (now - disconnectedSince > 60000) startAp();
  }
  if (!config.confirmed) startAp();
  static uint32_t bootHeldSince = 0;
  if (digitalRead(0) == LOW) {
    if (!bootHeldSince) bootHeldSince = now;
    if (now - bootHeldSince > 3000) startAp();
  } else bootHeldSince = 0;
}

void statusPage() {
  StaticJsonDocument<4096> document;
  document["version"] = FW_VERSION;
  document["board"] = "waveshare-esp32-s3-zero-4mb";
  document["uptime_s"] = millis() / 1000;
  document["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  document["wifi_ip"] = WiFi.localIP().toString();
  document["mqtt_connected"] = mqtt.connected();
  document["mqtt_started"] = mqttStarted;
  document["mqtt_host"] = config.mqttHost;
  document["mqtt_port"] = config.mqttPort;
  document["launchpad_connected"] = midi.connected();
  document["usb_dropped"] = midi.dropped();
  document["config_source"] = configSource;
  document["config_confirmed"] = config.confirmed;
  document["nvs_healthy"] = nvsHealthy;
  document["setup_ap"] = apActive;
  document["auto_update"] = config.autoUpdate;
  document["update_status"] = releaseUpdater.status();
  document["latest_release_version"] = releaseUpdater.latestVersion();
  document["update_busy"] = releaseUpdater.busy();
  document["flash_bytes"] = ESP.getFlashChipSize();
  document["firmware_bytes"] = ESP.getSketchSize();
  document["heap_free"] = ESP.getFreeHeap();
  JsonArray partitions = document.createNestedArray("partitions");
  esp_partition_iterator_t iterator = esp_partition_find(ESP_PARTITION_TYPE_ANY,
                                                         ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (iterator) {
    const auto *partition = esp_partition_get(iterator);
    JsonObject item = partitions.createNestedObject();
    item["label"] = partition->label;
    item["type"] = partition->type;
    item["subtype"] = partition->subtype;
    item["offset"] = partition->address;
    item["size"] = partition->size;
    iterator = esp_partition_next(iterator);
  }
  JsonArray states = document.createNestedArray("bulbs");
  for (uint8_t index = 0; index < 4; ++index) {
    JsonObject item = states.createNestedObject();
    item["index"] = index;
    item["set_topic"] = topicFor(index, "/set");
    item["avail_topic"] = topicFor(index, "/availability");
    item["rgb"] = index == 0;
    item["online"] = mqtt.connected() && bulbs[index].reachable;
    item["availability_known"] = bulbs[index].known;
    item["brightLevel"] = bulbs[index].brightness;
    item["brightness"] = domain::brightness(bulbs[index].brightness);
    item["colorLevel"] = bulbs[index].color;
  }
  String body;
  serializeJson(document, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

String pageStart(const char *title) {
  return String("<!doctype html><html lang='pl'><meta charset='utf-8'>") +
    "<meta name='viewport' content='width=device-width,initial-scale=1'><title>" + title +
    "</title><style>body{font:16px system-ui;max-width:850px;margin:2rem auto;padding:1rem;"
    "background:#121821;color:#eaf0f7}a{color:#8bd4ff}label{display:block;margin-top:1rem}"
    "input{padding:.5rem;max-width:95%}button{margin-top:1.5rem;padding:.8rem}"
    "table{border-collapse:collapse}td,th{padding:.5rem;border:1px solid #567}"
    ".lamps{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:1rem;margin-top:1rem}"
    ".lamp{border:1px solid #567;border-radius:12px;padding:1rem}.lamp h2{margin-top:0}"
    "fieldset{border:0;padding:0;margin:0}fieldset:disabled{opacity:.5}select{padding:.5rem;max-width:100%}"
    ".actions{display:flex;gap:.5rem;flex-wrap:wrap}.actions button{margin-top:1rem}"
    "output{display:inline-block;min-width:3rem}</style><body><h1>" +
    title + "</h1>";
}

void rootPage() {
  if (throughAp()) {
    server.sendHeader("Location", "/settings");
    server.send(302);
    return;
  }
  String body = pageStart("Launchpad → TRÅDFRI");
  body += "<p>Firmware " FW_VERSION " · Wi-Fi: " + WiFi.localIP().toString() +
      " · MQTT: " + String(mqtt.connected() ? "online" : "offline") +
      " · USB: " + String(midi.connected() ? "online" : "offline") + "</p>";
  body += "<p><a href='/status'>Status JSON</a> · <a href='/settings'>Ustawienia</a> · "
          "<a href='/update'>Aktualizacja OTA</a></p><table><tr><th>Żarówka</th><th>Typ</th>"
          "<th>Dostępność</th><th>Jasność</th></tr>";
  for (uint8_t index = 0; index < 4; ++index)
    body += "<tr><td>" + escaped(config.bulbs[index]) + "</td><td>" +
      String(index == 0 ? "RGB" : "CCT") + "</td><td>" +
      String(mqtt.connected() && bulbs[index].reachable ? "online" : "offline") +
      "</td><td>" + String(domain::brightness(bulbs[index].brightness)) + "</td></tr>";
  body += "</table><h2>Sterowanie żarówkami</h2><p>Poziomy jasności i kolorów odpowiadają padom "
          "Launchpada. Wysyłanie poleceń jest dostępne tylko dla lamp online.</p><div class='lamps'>";
  static const char *rgbNames[8] = {"Niebieski", "Błękitny", "Turkusowy", "Biały",
      "Kremowy", "Pomarańczowy", "Czerwono-pomarańczowy", "Czerwony"};
  for (uint8_t index = 0; index < 4; ++index) {
    const auto &bulb = bulbs[index];
    const bool online = mqtt.connected() && bulb.reachable;
    body += "<section class='lamp'><h2>" + escaped(config.bulbs[index]) + "</h2><p>" +
        String(index == 0 ? "RGB" : "CCT") + " · " + String(online ? "online" : "offline") +
        "</p><form method='post' action='/control'><input type='hidden' name='csrf' value='" +
        csrf + "'><input type='hidden' name='bulb' value='" + String(index) + "'><fieldset " +
        String(online ? "" : "disabled") + "><label>Jasność<br><input name='brightness' "
        "type='range' min='0' max='8' step='1' value='" + String(bulb.brightness) +
        "' oninput=\"this.nextElementSibling.value=Math.round(this.value*100/8)+'%'\"> "
        "<output>" + String((bulb.brightness * 100 + 4) / 8) + "%</output></label><label>" +
        String(index == 0 ? "Kolor" : "Temperatura bieli") + "<br><select name='color'>";
    body += "<option value='warm' " + String(bulb.warmPreset ? "selected" : "") +
        ">Ciepła biel ~2700 K</option>";
    for (uint8_t color = 0; color < 8; ++color) {
      const String label = index == 0 ? String(rgbNames[color]) :
          String(1000000UL / domain::colorTemperature(color)) + " K";
      body += "<option value='" + String(color) + "' " +
          String(!bulb.warmPreset && bulb.color == color ? "selected" : "") + ">" +
          label + "</option>";
    }
    body += "</select></label><div class='actions'><button name='action' value='apply'>Zastosuj</button>"
        "<button name='action' value='on'>Włącz</button><button name='action' value='off'>Wyłącz</button>"
        "<button name='action' value='warm'>Ciepła biel 100%</button></div></fieldset></form></section>";
  }
  const String latestVersion = releaseUpdater.latestVersion();
  body += "</div><h2>Aktualizacje</h2><p>Zainstalowana wersja: <strong>" FW_VERSION
          "</strong><br>Najnowsze sprawdzone wydanie GitHub: <strong id='release-latest'>" +
          escaped(latestVersion.isEmpty() ? String("jeszcze nie sprawdzono") : latestVersion) +
          "</strong><br>Status: <span id='release-status'>" + escaped(releaseUpdater.status()) +
          "</span></p><form method='post' action='/check-update'><input type='hidden' name='csrf' value='" +
          csrf + "'><button id='release-check-button' " + String(releaseUpdater.busy() ? "disabled" : "") +
          ">Sprawdź wersję na GitHub</button></form><p>Przycisk sprawdza opublikowane wydanie. "
          "Automatyczne instalowanie włączysz w ustawieniach.</p>"
          "<script>async function refreshRelease(){try{const response=await fetch('/status',{cache:'no-store'});"
          "if(!response.ok)throw new Error('status');const status=await response.json();"
          "document.getElementById('release-latest').textContent=status.latest_release_version||'jeszcze nie sprawdzono';"
          "document.getElementById('release-status').textContent=status.update_status;"
          "document.getElementById('release-check-button').disabled=status.update_busy;"
          "if(status.update_busy)setTimeout(refreshRelease,1500)}catch(error){"
          "document.getElementById('release-status').textContent='Nie udało się odświeżyć statusu';"
          "document.getElementById('release-check-button').disabled=false}}refreshRelease();</script></body></html>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", body);
}

void controlBulb() {
  if (!validPost()) return;
  uint32_t index, level, color = 4;
  const bool warm = server.arg("color") == "warm";
  if (!domain::parseBounded(server.arg("bulb").c_str(), 3, index) ||
      !domain::parseBounded(server.arg("brightness").c_str(), 8, level) ||
      (!warm && !domain::parseBounded(server.arg("color").c_str(), 7, color))) {
    server.send(400, "text/plain; charset=utf-8", "Nieprawidłowe parametry sterowania.");
    return;
  }
  const domain::Bulb previous = bulbs[index];
  const auto result = domain::control(bulbs[index], mqtt.connected(),
      server.arg("action").c_str(), level, color, warm);
  if (result == domain::ControlResult::Offline) {
    server.send(409, "text/plain; charset=utf-8", "Żarówka lub MQTT jest offline. Polecenie zablokowane.");
    return;
  }
  if (result != domain::ControlResult::Ready) {
    server.send(400, "text/plain; charset=utf-8", "Nieprawidłowe polecenie.");
    return;
  }
  if (!commandBulb(index)) {
    bulbs[index] = previous;
    server.send(503, "text/plain; charset=utf-8", "Nie udało się przekazać polecenia do MQTT.");
    return;
  }
  draw();
  server.sendHeader("Location", "/");
  server.send(303);
}

void checkUpdate() {
  if (!validPost()) return;
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/plain; charset=utf-8", "Brak połączenia Wi-Fi.");
    return;
  }
  if (time(nullptr) < 1735689600) {
    server.send(503, "text/plain; charset=utf-8", "Oczekiwanie na synchronizację czasu NTP. Spróbuj za chwilę.");
    return;
  }
  if (!releaseUpdater.checkNow()) {
    server.send(409, "text/plain; charset=utf-8", "Sprawdzanie lub aktualizacja już trwa albo nie można uruchomić zadania.");
    return;
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

String inputField(const char *name, const char *label, const String &value,
                  const char *type = "text") {
  return String("<label>") + label + "<br><input name='" + name + "' type='" + type +
      "' value='" + escaped(value) + "' maxlength='128'></label>";
}

void settingsPage() {
  String body = pageStart("Ustawienia Launchpad");
  body += "<p>Puste hasło zachowuje poprzednią wartość. Zapis nie kasuje wcześniejszych "
          "namespace NVS. Po zapisie urządzenie uruchomi się ponownie.</p>"
          "<form method='post' action='/settings'><input type='hidden' name='csrf' value='" +
          csrf + "'>";
  body += inputField("ssid", "Nazwa sieci Wi-Fi", config.ssid);
  body += inputField("wifi_pass", "Hasło Wi-Fi (puste = bez zmian)", "", "password");
  body += "<label><input type='checkbox' name='wifi_open'> Wi-Fi bez hasła</label>";
  body += inputField("mqtt_host", "Adres MQTT", config.mqttHost);
  body += inputField("mqtt_port", "Port MQTT", String(config.mqttPort), "number");
  body += inputField("mqtt_user", "Użytkownik MQTT", config.mqttUser);
  body += inputField("mqtt_pass", "Hasło MQTT (puste = bez zmian)", "", "password");
  body += "<label><input type='checkbox' name='mqtt_clear'> Usuń hasło MQTT</label>";
  body += inputField("base", "Temat bazowy Zigbee2MQTT", config.baseTopic);
  for (uint8_t index = 0; index < 4; ++index) {
    const String name = "bulb" + String(index);
    const String label = "Żarówka " + String(index + 1) + (index == 0 ? " (RGB)" : " (CCT)");
    body += inputField(name.c_str(), label.c_str(), config.bulbs[index]);
  }
  body += "<label><input name='auto' type='checkbox' " +
          String(config.autoUpdate ? "checked" : "") +
          "> Automatyczne podpisane wydania GitHub</label>"
          "<button>Zapisz i uruchom ponownie</button></form></body></html>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", body);
}

void saveSettings() {
  if (!validPost()) return;
  if (releaseUpdater.busy()) { server.send(409, "text/plain", "OTA in progress"); return; }
  Config next = config;
  next.ssid = server.arg("ssid");
  if (server.hasArg("wifi_open")) next.wifiPassword = "";
  else if (!server.arg("wifi_pass").isEmpty()) next.wifiPassword = server.arg("wifi_pass");
  next.mqttHost = server.arg("mqtt_host");
  const long port = server.arg("mqtt_port").toInt();
  if (port < 1 || port > 65535) { server.send(400, "text/plain", "Invalid port"); return; }
  next.mqttPort = port;
  next.mqttUser = server.arg("mqtt_user");
  if (server.hasArg("mqtt_clear")) next.mqttPassword = "";
  else if (!server.arg("mqtt_pass").isEmpty()) next.mqttPassword = server.arg("mqtt_pass");
  next.baseTopic = server.arg("base");
  for (uint8_t index = 0; index < 4; ++index)
    next.bulbs[index] = server.arg("bulb" + String(index));
  next.autoUpdate = server.hasArg("auto");
  next.confirmed = true;
  if (next.ssid.isEmpty() || !saveConfig(next)) {
    server.send(400, "text/plain", "Invalid settings or NVS write failed.");
    return;
  }
  server.send(200, "text/plain; charset=utf-8", "Zapisano w NVS. Restart za chwilę.");
  scheduleReboot();
}

bool uploadHash(const String &value) {
  if (value.length() != 64) return false;
  for (uint8_t index = 0; index < 32; ++index) {
    char pair[3] = {value[index * 2], value[index * 2 + 1], 0};
    if (!isxdigit(pair[0]) || !isxdigit(pair[1])) return false;
    expectedUploadHash[index] = strtoul(pair, nullptr, 16);
  }
  return true;
}

void abortUpload() {
  if (uploadOwned) {
    Update.abort();
    mbedtls_sha256_free(&uploadDigest);
    releaseUpdater.release();
    uploadOwned = false;
  }
  uploadOkay = false;
}

void updatePage() {
  if (!authorized()) return;
  String body = pageStart("Aktualizacja firmware");
  body += "<p>Wgraj wyłącznie firmware.bin aplikacji ESP32-S3. SHA256 jest w wydaniu GitHub. "
          "Partycje i NVS pozostają bez zmian.</p><form method='post' action='/update' "
          "enctype='multipart/form-data'><input type='hidden' name='csrf' value='" + csrf +
          "'>" + inputField("sha256", "SHA256 pliku", "") +
          "<label>Firmware <input type='file' name='firmware' accept='.bin' required></label>"
          "<button>Wgraj i uruchom ponownie</button></form></body></html>";
  server.send(200, "text/html; charset=utf-8", body);
}

void handleUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadOkay = false;
    if (!authorized() || !validPost() || !uploadHash(server.arg("sha256")) ||
        !releaseUpdater.acquire()) return;
    uploadOwned = true;
    uploadBytes = 0;
    uploadLastData = millis();
    mbedtls_sha256_init(&uploadDigest);
    mbedtls_sha256_starts_ret(&uploadDigest, 0);
    const auto *next = esp_ota_get_next_update_partition(nullptr);
    const auto *running = esp_ota_get_running_partition();
    if (!next || !running || next->address == running->address ||
        !Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) abortUpload();
  } else if (upload.status == UPLOAD_FILE_WRITE && uploadOwned) {
    if (!uploadBytes && (upload.currentSize < sizeof(esp_image_header_t) ||
        reinterpret_cast<esp_image_header_t *>(upload.buf)->magic != ESP_IMAGE_HEADER_MAGIC ||
        reinterpret_cast<esp_image_header_t *>(upload.buf)->chip_id != ESP_CHIP_ID_ESP32S3)) {
      abortUpload();
      return;
    }
    uploadLastData = millis();
    uploadBytes += upload.currentSize;
    if (mbedtls_sha256_update_ret(&uploadDigest, upload.buf, upload.currentSize) ||
        Update.write(upload.buf, upload.currentSize) != upload.currentSize) abortUpload();
  } else if (upload.status == UPLOAD_FILE_END && uploadOwned) {
    uint8_t actual[32];
    const bool hashOkay = mbedtls_sha256_finish_ret(&uploadDigest, actual) == 0 &&
                         memcmp(actual, expectedUploadHash, 32) == 0;
    if (!hashOkay || uploadBytes != upload.totalSize || !Update.end(true)) {
      abortUpload();
      return;
    }
    mbedtls_sha256_free(&uploadDigest);
    uploadOwned = false;
    uploadOkay = true;
    releaseUpdater.release();
  } else if (upload.status == UPLOAD_FILE_ABORTED) abortUpload();
}

void otaLoop() {
  if (!otaStarted && WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.setHostname("launchpad-esp32");
    if (!config.adminPassword.isEmpty()) ArduinoOTA.setPassword(config.adminPassword.c_str());
    ArduinoOTA.onStart([] {
      arduinoActive = releaseUpdater.acquire();
      uploadLastData = millis();
      if (!arduinoActive) Update.abort();
    });
    ArduinoOTA.onProgress([](unsigned int, unsigned int) { uploadLastData = millis(); });
    ArduinoOTA.onError([](ota_error_t) {
      if (arduinoActive) releaseUpdater.release();
      arduinoActive = false;
    });
    ArduinoOTA.onEnd([] {
      if (arduinoActive) releaseUpdater.release();
      arduinoActive = false;
    });
    ArduinoOTA.begin();
    otaStarted = true;
  }
  if (otaStarted && (!releaseUpdater.busy() || arduinoActive)) ArduinoOTA.handle();
  if ((uploadOwned || arduinoActive) && millis() - uploadLastData > 60000) {
    if (uploadOwned) abortUpload();
    else { Update.abort(); arduinoActive = false; releaseUpdater.release(); }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  char randomToken[33];
  snprintf(randomToken, sizeof(randomToken), "%08lx%08lx%08lx%08lx",
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  csrf = randomToken;
  const uint64_t mac = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06lx", static_cast<unsigned long>(mac & 0xffffff));
  apName = String("Launchpad-Setup-") + suffix;
  WiFi.mode(WIFI_STA);
  configLoaded = loadConfig();
  WiFi.persistent(false);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  WiFi.setHostname("launchpad-esp32");
  WiFi.setAutoReconnect(true);
  if (!config.ssid.isEmpty()) WiFi.begin(config.ssid.c_str(), config.wifiPassword.c_str());
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  releaseUpdater.begin();
  server.on("/", HTTP_GET, rootPage);
  server.on("/status", HTTP_GET, statusPage);
  server.on("/control", HTTP_POST, controlBulb);
  server.on("/check-update", HTTP_POST, checkUpdate);
  server.on("/settings", HTTP_GET, settingsPage);
  server.on("/settings", HTTP_POST, saveSettings);
  server.on("/update", HTTP_GET, updatePage);
  server.on("/update", HTTP_POST, [] {
    if (!authorized()) return;
    if (!uploadOkay) { server.send(400, "text/plain", "Update rejected: auth, token, SHA256, partition or image."); return; }
    server.send(200, "text/plain", "Update OK. Restarting.");
    scheduleReboot();
  }, handleUpload);
  server.onNotFound([] { server.send(404, "text/plain", "Not found"); });
  server.begin();
  if (!midi.begin()) Serial.println("USB host initialization failed");
  Serial.printf("Launchpad firmware %s; config %s\n", FW_VERSION, configSource.c_str());
}

void loop() {
  wifiLoop();
  server.handleClient();
  otaLoop();
  brokerLoop();
  midiLoop();
  releaseUpdater.loop(config.autoUpdate && config.confirmed && configLoaded && !rebootPending);
  if (rebootPending && int32_t(millis() - rebootAt) >= 0 && !releaseUpdater.busy()) ESP.restart();
  if (millis() >= 86400000UL && !releaseUpdater.busy()) ESP.restart();
  delay(1);
}
