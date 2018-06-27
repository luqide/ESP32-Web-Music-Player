#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
//#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

#include "ili9341.h"
#include "sd_card.h"
#include "dirent.h"
#include "i2s_dac.h"

void app_main() {
  gpio_set_direction(PIN_PD, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_PD, 0);
  //initialize spi bus
  esp_err_t ret;
  spi_bus_config_t buscfg = {
    .miso_io_num = PIN_MISO,
    .mosi_io_num = PIN_MOSI,
    .sclk_io_num = PIN_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = PARALLEL_LINES*320*2+8,
  };
  spi_device_interface_config_t devcfg = {
    .clock_speed_hz = 33 * 1000 * 1000,
    .mode = 0,
    .queue_size= 7,
    .pre_cb = lcd_spi_pre_transfer_callback,
  };
  ret = spi_bus_initialize(HSPI_HOST, &buscfg, 1);
  ESP_ERROR_CHECK(ret);
  ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
  ESP_ERROR_CHECK(ret);
  lcd_init();
  //ili9341 init completed

  // nvs_flash_init();
  // tcpip_adapter_init();
  // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  // ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
  // ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
  // ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
  // ESP_ERROR_CHECK( esp_wifi_set_auto_connect(1) );
  // wifi_config_t sta_config = {
  //     .sta = {
  //         .ssid = "Molly_AP",
  //         .password = "qazwsx741",
  //         .bssid_set = false
  //     }
  // };
  // ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
  // ESP_ERROR_CHECK( esp_wifi_start() );
  // ESP_ERROR_CHECK( esp_wifi_connect() );

  lcd_fill(bgr_to_uint(0, 0, 0));
  srand((void*)time(NULL));
  setCursor(0, 20);
  setTextsize(1);
  setTextcolor(bgr_to_uint(255,255,255));
  setTextBgcolor(bgr_to_uint(0,0,0));

  sdmmc_card_t card;
  sdmmc_mount(&card);

  DIR *dirp = opendir("/sdcard/");

  struct dirent *entry;
  while((entry = readdir(dirp))) {
    printf("%s\n", entry->d_name);
  }

  FILE *wavFile = fopen("/sdcard/怪盗V-Tension.WAV", "rb");
  if(wavFile != NULL)  {
    printf("Wav File Opened.\n");
    setVolume(-25);
    wavPlay(wavFile);

    fclose(wavFile);
    printf("Wav File Closed.\n");
  }
  else {
    printf("Failed to open wav file.\n");
  }
}
