#include <esp_ota_ops.h>
#include <esp_attr.h>
#include <sdkconfig.h>

const __attribute__((section(".rodata_desc"))) esp_app_desc_t esp_app_desc = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .secure_version = 0,
    .version = FW_VERSION,
    .project_name = "Launchpad_mqtt",
    .time = __TIME__,
    .date = __DATE__,
    .idf_ver = IDF_VER,
};

static DRAM_ATTR char cachedHash[65];

__attribute__((constructor)) void esp_ota_init_app_elf_sha256(void) {
  const volatile uint8_t *bytes = esp_app_desc.app_elf_sha256;
  const char alphabet[] = "0123456789abcdef";
  for (unsigned index = 0; index < 32; ++index) {
    cachedHash[index * 2] = alphabet[bytes[index] >> 4];
    cachedHash[index * 2 + 1] = alphabet[bytes[index] & 15];
  }
  cachedHash[64] = 0;
}

const esp_app_desc_t *esp_ota_get_app_description(void) { return &esp_app_desc; }

int IRAM_ATTR esp_ota_get_app_elf_sha256(char *destination, size_t size) {
  if (!destination || !size) return 0;
  const size_t count = size > 65 ? 64 : size - 1;
  for (size_t index = 0; index < count; ++index) destination[index] = cachedHash[index];
  destination[count] = 0;
  return count + 1;
}
