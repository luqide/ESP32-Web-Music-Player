#include "sd_card.h"

esp_err_t sdmmc_mount(sdmmc_card_t *card) {
  ESP_LOGI(TAG, "Initializing SD Card");

  ESP_LOGI(TAG, "Using SDMMC peripheral");
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();

  // To use 1-line SD mode, uncomment the following line:
  // host.flags = SDMMC_HOST_FLAG_1BIT;

  // This initializes the slot without card detect (CD) and write protect (WP) signals.
  // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

  // GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
  // Internal pull-ups are not sufficient. However, enabling internal pull-ups
  // does make a difference some boards, so we do that here.
  gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);   // CMD, needed in 4- and 1- line modes
  gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);    // D0, needed in 4- and 1-line modes
  gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);    // D1, needed in 4-line mode only
  gpio_set_pull_mode(12, GPIO_PULLUP_ONLY);   // D2, needed in 4-line mode only
  gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);   // D3, needed in 4- and 1-line modes

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
