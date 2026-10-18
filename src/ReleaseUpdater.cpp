#include "ReleaseUpdater.h"
#include "Config.h"
#include "Domain.h"
#include "ReleaseKey.h"
#include "TrustRoots.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <time.h>

ReleaseUpdater releaseUpdater;
static const char *releaseBase = "https://github.com/Pawel-sp9pw/Launchpad_mqtt/releases/";

static bool downloadText(const String &url, String &body, size_t limit) {
  WiFiClientSecure client;
  client.setCACert(TRUST_ROOTS);
  client.setTimeout(15);
  HTTPClient request;
  request.setConnectTimeout(12000);
  request.setTimeout(15000);
  request.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  request.setRedirectLimit(5);
  request.useHTTP10(true);
  if (!request.begin(client, url)) return false;
  const int result = request.GET();
  const int size = request.getSize();
  if (result != HTTP_CODE_OK || size <= 0 || static_cast<size_t>(size) > limit) {
    request.end();
    return false;
  }
  body = request.getString();
  request.end();
  return body.length() == static_cast<size_t>(size);
}

static int hexDigit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

static bool decodeHex(const String &encoded, uint8_t *decoded, size_t capacity,
                      size_t &length) {
  length = encoded.length() / 2;
  if (encoded.length() % 2 || length > capacity) return false;
  for (size_t index = 0; index < length; ++index) {
    const int high = hexDigit(encoded[index * 2]);
    const int low = hexDigit(encoded[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    decoded[index] = (high << 4) | low;
  }
  return true;
}

static bool verifyManifest(const String &body, String signature) {
  signature.trim();
  uint8_t signatureBytes[80];
  size_t signatureLength = 0;
  if (!decodeHex(signature, signatureBytes, sizeof(signatureBytes), signatureLength))
    return false;
  uint8_t digest[32];
  if (mbedtls_sha256_ret(reinterpret_cast<const uint8_t *>(body.c_str()),
                         body.length(), digest, 0)) return false;
  mbedtls_pk_context publicKey;
  mbedtls_pk_init(&publicKey);
  const int parsed = mbedtls_pk_parse_public_key(&publicKey,
      reinterpret_cast<const uint8_t *>(RELEASE_PUBLIC_KEY), sizeof(RELEASE_PUBLIC_KEY));
  const bool verified = !parsed && mbedtls_pk_can_do(&publicKey, MBEDTLS_PK_ECDSA) &&
      mbedtls_pk_verify(&publicKey, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                        signatureBytes, signatureLength) == 0;
  mbedtls_pk_free(&publicKey);
  return verified;
}

void ReleaseUpdater::begin() {
  statusMutex_ = xSemaphoreCreateMutex();
}

bool ReleaseUpdater::acquire() {
  bool expected = false;
  return busy_.compare_exchange_strong(expected, true);
}

void ReleaseUpdater::release() { busy_ = false; }

String ReleaseUpdater::status() {
  if (!statusMutex_) return "not-started";
  xSemaphoreTake(statusMutex_, portMAX_DELAY);
  const String result = status_;
  xSemaphoreGive(statusMutex_);
  return result;
}

String ReleaseUpdater::latestVersion() {
  if (!statusMutex_) return "";
  xSemaphoreTake(statusMutex_, portMAX_DELAY);
  const String result = latestVersion_;
  xSemaphoreGive(statusMutex_);
  return result;
}

void ReleaseUpdater::setStatus(const String &message) {
  xSemaphoreTake(statusMutex_, portMAX_DELAY);
  status_ = message;
  xSemaphoreGive(statusMutex_);
}

void ReleaseUpdater::loop(bool enabled) {
  if (!enabled || WiFi.status() != WL_CONNECTED || millis() < 60000 ||
      time(nullptr) < 1735689600) return;
  if (lastCheck_ && millis() - lastCheck_ < 3600000UL) return;
  startCheck(true);
}

bool ReleaseUpdater::checkNow() {
  if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1735689600) return false;
  return startCheck(false);
}

bool ReleaseUpdater::startCheck(bool install) {
  if (!statusMutex_ || !acquire()) return false;
  installRequested_ = install;
  lastCheck_ = millis();
  setStatus("checking-published-release");
  if (xTaskCreate(task, "release-ota", 12288, this, 1, nullptr) != pdPASS) {
    setStatus("worker-allocation-failed");
    release();
    return false;
  }
  return true;
}

void ReleaseUpdater::task(void *argument) {
  auto *updater = static_cast<ReleaseUpdater *>(argument);
  updater->check(updater->installRequested_);
  updater->release();
  vTaskDelete(nullptr);
}

void ReleaseUpdater::check(bool install) {
  setStatus("checking-published-release");
  String body, signature;
  if (!downloadText(String(releaseBase) + "latest/download/manifest.json", body, 2048) ||
      !downloadText(String(releaseBase) + "latest/download/manifest.sig", signature, 200)) {
    setStatus("no-release-or-https-error");
    return;
  }
  if (!verifyManifest(body, signature)) {
    setStatus("manifest-signature-rejected");
    return;
  }
  StaticJsonDocument<2048> document;
  if (deserializeJson(document, body) || document["schema"] != 1 ||
      document["board"] != "waveshare-esp32-s3-zero-4mb" ||
      !document["version"].is<const char *>() || !document["size"].is<uint32_t>() ||
      !document["sha256"].is<const char *>() || !document["url"].is<const char *>()) {
    setStatus("manifest-schema-rejected");
    return;
  }
  const String version = document["version"].as<String>();
  uint32_t versionParts[3];
  if (!domain::parseVersion(version.c_str(), versionParts)) {
    setStatus("release-version-rejected");
    return;
  }
  const String url = document["url"].as<String>();
  if (url != String(releaseBase) + "download/v" + version + "/firmware.bin") {
    setStatus("release-url-rejected");
    return;
  }
  uint8_t expectedDigest[32];
  size_t digestLength = 0;
  if (!decodeHex(document["sha256"].as<String>(), expectedDigest, 32, digestLength) ||
      digestLength != 32) {
    setStatus("sha256-format-rejected");
    return;
  }
  const uint32_t size = document["size"];
  if (size < 1024 || size > 0x140000) {
    setStatus("release-size-rejected");
    return;
  }
  xSemaphoreTake(statusMutex_, portMAX_DELAY);
  latestVersion_ = version;
  xSemaphoreGive(statusMutex_);
  if (!domain::newer(version.c_str(), FW_VERSION)) {
    setStatus("up-to-date");
    return;
  }
  if (!install) {
    setStatus("new-release-available");
    return;
  }
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!next || !running || next->address == running->address || size < 1024 ||
      size > next->size || ESP.getFlashChipSize() != 4 * 1024 * 1024) {
    setStatus("partition-or-image-size-rejected");
    return;
  }
  WiFiClientSecure client;
  client.setCACert(TRUST_ROOTS);
  client.setTimeout(15);
  HTTPClient request;
  request.setConnectTimeout(12000);
  request.setTimeout(15000);
  request.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  request.setRedirectLimit(5);
  request.useHTTP10(true);
  if (!request.begin(client, url) || request.GET() != HTTP_CODE_OK ||
      request.getSize() != static_cast<int>(size)) {
    request.end();
    setStatus("firmware-https-or-size-error");
    return;
  }
  uint8_t buffer[4096];
  WiFiClient *stream = request.getStreamPtr();
  const size_t headerSize = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
                            sizeof(esp_app_desc_t);
  const size_t initial = stream->readBytes(buffer, headerSize);
  const auto *header = reinterpret_cast<const esp_image_header_t *>(buffer);
  const auto *description = reinterpret_cast<const esp_app_desc_t *>(
      buffer + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
  if (initial != headerSize || header->magic != ESP_IMAGE_HEADER_MAGIC ||
      header->chip_id != ESP_CHIP_ID_ESP32S3 ||
      description->magic_word != ESP_APP_DESC_MAGIC_WORD ||
      strnlen(description->version, sizeof(description->version)) == sizeof(description->version) ||
      version != description->version) {
    request.end();
    setStatus("image-chip-or-version-rejected");
    return;
  }
  if (!Update.begin(size, U_FLASH)) {
    request.end();
    setStatus("ota-begin-failed");
    return;
  }
  setStatus("downloading-verified-release");
  mbedtls_sha256_context digest;
  mbedtls_sha256_init(&digest);
  bool okay = mbedtls_sha256_starts_ret(&digest, 0) == 0;
  uint32_t received = initial;
  okay = okay && mbedtls_sha256_update_ret(&digest, buffer, initial) == 0 &&
          Update.write(buffer, initial) == initial;
  uint32_t lastData = millis();
  while (okay && received < size && millis() - lastData < 20000) {
    const int available = stream->available();
    if (available <= 0) {
      if (!request.connected()) break;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    const size_t count = stream->readBytes(buffer,
        std::min<size_t>(std::min<size_t>(available, sizeof(buffer)), size - received));
    if (!count) break;
    okay = mbedtls_sha256_update_ret(&digest, buffer, count) == 0 &&
           Update.write(buffer, count) == count;
    received += count;
    lastData = millis();
    vTaskDelay(1);
  }
  uint8_t actualDigest[32];
  okay = okay && received == size &&
          mbedtls_sha256_finish_ret(&digest, actualDigest) == 0 &&
          memcmp(expectedDigest, actualDigest, 32) == 0;
  mbedtls_sha256_free(&digest);
  request.end();
  if (!okay) {
    Update.abort();
    setStatus("download-or-sha256-rejected");
    return;
  }
  if (!Update.end(false)) {
    setStatus("image-validation-failed");
    return;
  }
  setStatus("installed-restarting");
  vTaskDelay(pdMS_TO_TICKS(500));
  ESP.restart();
}
