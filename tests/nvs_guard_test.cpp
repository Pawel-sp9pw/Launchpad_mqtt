#include "NvsGuard.h"
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <assert.h>
#include <stdio.h>

static esp_err_t simulatedNvsResult = ESP_OK;
static wifi_init_config_t observed;
extern "C" esp_err_t __real_nvs_flash_init() { return simulatedNvsResult; }
extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t *configuration) {
  observed = *configuration;
  return ESP_OK;
}
extern "C" esp_err_t __wrap_nvs_flash_init();
extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *configuration);

int main() {
  const wifi_init_config_t configuration = {true, 42};
  assert(__wrap_nvs_flash_init() == ESP_OK && nvsHealthy);
  assert(__wrap_esp_wifi_init(&configuration) == ESP_OK);
  assert(observed.nvs_enable && observed.preserved_field == 42);
  simulatedNvsResult = ESP_ERR_NVS_NEW_VERSION_FOUND;
  assert(__wrap_nvs_flash_init() == ESP_FAIL && !nvsHealthy);
  assert(__wrap_esp_wifi_init(&configuration) == ESP_OK);
  assert(!observed.nvs_enable && observed.preserved_field == 42);
  assert(configuration.nvs_enable);
  simulatedNvsResult = ESP_ERR_NVS_NO_FREE_PAGES;
  assert(__wrap_nvs_flash_init() == ESP_FAIL && !nvsHealthy);
  simulatedNvsResult = 99;
  assert(__wrap_nvs_flash_init() == 99 && !nvsHealthy);
  simulatedNvsResult = ESP_OK;
  assert(__wrap_nvs_flash_init() == ESP_OK && nvsHealthy);
  puts("NVS safety tests passed: newer format, full partition, RAM-only Wi-Fi, healthy storage");
}
