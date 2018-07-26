#include "sd_card.h"

#define SD_PIN_NUM_CD   32
#define SD_PIN_NUM_MISO 12
#define SD_PIN_NUM_MOSI 13
#define SD_PIN_NUM_CLK  14
#define SD_PIN_NUM_CS 15

static const char *TAG = "sdcard";
esp_err_t sdmmc_mount(sdmmc_card_t *card) {
  ESP_LOGI(TAG, "Initializing SD Card");

  ESP_LOGI(TAG, "Using SPI peripheral");

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = HSPI_HOST;
  host.max_freq_khz = 20*1000;
  sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
  slot_config.gpio_miso = SD_PIN_NUM_MISO;
  slot_config.gpio_mosi = SD_PIN_NUM_MOSI;
  slot_config.gpio_sck  = SD_PIN_NUM_CLK;
  slot_config.gpio_cs   = SD_PIN_NUM_CS;
  slot_config.gpio_cd   = SD_PIN_NUM_CD;
  slot_config.dma_channel = 2;

  // Options for mounting the filesystem.
  // If format_if_mount_failed is set to true, SD card will be partitioned and
  // formatted in case when mounting fails.
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024
  };
  esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

  if (ret != ESP_OK) {
       if (ret == ESP_FAIL) {
           ESP_LOGE(TAG, "Failed to mount filesystem. "
               "If you want the card to be formatted, set format_if_mount_failed = true.");
       } else {
           ESP_LOGE(TAG, "Failed to initialize the card (%s). "
               "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
       }
       return ret;
   }

  sdmmc_card_print_info(stdout, card);
  return ESP_OK;
}

esp_err_t sdmmc_unmount() {
  return esp_vfs_fat_sdmmc_unmount();
}

bool sdmmc_is_valid() {
  gpio_set_direction(SD_PIN_NUM_CD, GPIO_MODE_INPUT);
  return !gpio_get_level(SD_PIN_NUM_CD);
}
