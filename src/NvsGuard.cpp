#include "NvsGuard.h"
#include <nvs_flash.h>
#include <esp_wifi.h>

bool nvsHealthy = false;

extern "C" esp_err_t __real_nvs_flash_init();
extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t *configuration);

extern "C" esp_err_t __wrap_nvs_flash_init() {
  const esp_err_t result = __real_nvs_flash_init();
  nvsHealthy = result == ESP_OK;
  if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND)
    return ESP_FAIL;
  return result;
}

extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *configuration) {
  wifi_init_config_t safeConfiguration = *configuration;
  if (!nvsHealthy) safeConfiguration.nvs_enable = false;
  return __real_esp_wifi_init(&safeConfiguration);
}
