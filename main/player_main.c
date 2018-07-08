#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

#include "tftspi.h"
#include "tft.h"
#include "sd_card.h"
#include "dirent.h"
#include "i2s_dac.h"
#include "ui.h"


static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static const char *TAG = "APP_MAIN";
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
      case SYSTEM_EVENT_STA_START:
          esp_wifi_connect();
          break;
      case SYSTEM_EVENT_STA_GOT_IP:
          ESP_LOGI(TAG, "got ip:%s",
                   ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
          xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
          break;
      case SYSTEM_EVENT_AP_STACONNECTED:
          ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d",
                   MAC2STR(event->event_info.sta_connected.mac),
                   event->event_info.sta_connected.aid);
          break;
      case SYSTEM_EVENT_AP_STADISCONNECTED:
          ESP_LOGI(TAG, "station:"MACSTR"leave, AID=%d",
                   MAC2STR(event->event_info.sta_disconnected.mac),
                   event->event_info.sta_disconnected.aid);
          break;
      case SYSTEM_EVENT_STA_DISCONNECTED:
          esp_wifi_connect();
          xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
          break;
      default:
          break;
    }
    return ESP_OK;
}

void app_main() {
  gpio_set_direction(PIN_PD, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_NUM_CS, 1);
  gpio_set_level(PIN_PD, 0);

  TFT_PinsInit();
  //initialize spi bus
  esp_err_t ret;
  spi_lobo_bus_config_t buscfg = {
    .miso_io_num = PIN_NUM_MISO,
    .mosi_io_num = PIN_NUM_MOSI,
    .sclk_io_num = PIN_NUM_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 240*320*2*8,
  };

  spi_lobo_device_interface_config_t lcd_devcfg = {
      .clock_speed_hz=8000000,                // Initial clock out at 8 MHz
      .mode=0,                                // SPI mode 0
      .spics_io_num=-1,                       // we will use external CS pin
    .spics_ext_io_num=PIN_NUM_CS,           // external CS pin
    .flags=LB_SPI_DEVICE_HALFDUPLEX,        // ALWAYS SET  to HALF DUPLEX MODE!! for display spi
  };

  spi_lobo_device_handle_t spi;
  ret = spi_lobo_bus_add_device(VSPI_HOST, &buscfg, &lcd_devcfg, &spi);
  ESP_ERROR_CHECK(ret);
  disp_spi = spi;

  TFT_display_init();
  TFT_setclipwin(0, 0, 320, 240);
  TFT_fillRect(0, 0, 320, 24, TFT_PINK);

  nvs_flash_init();
  tcpip_adapter_init();
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
  ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
  ESP_ERROR_CHECK( esp_wifi_set_auto_connect(1) );
  wifi_config_t sta_config = {
      .sta = {
          .ssid = "Molly_AP",
          .password = "qazwsx741",
          .bssid_set = false
      }
  };
  ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
  ESP_ERROR_CHECK( esp_wifi_start() );
  ESP_ERROR_CHECK( esp_wifi_connect() );


  gpio_set_level(PIN_NUM_CS, 1);
  sdmmc_card_t card;
  sdmmc_mount(&card);
  i2s_init();
  dac_mute(false);
  while(1) {
    DIR *dirp = opendir("/sdcard/WAV");
    if(dirp == NULL) {
      printf("Failed to open directory.\n");
      break;
    }
    struct dirent *entry;
    while((entry = readdir(dirp))) {

      char filename[255];
      memset(filename, 0, sizeof(filename));
      sprintf(filename, "/sdcard/WAV/%s", entry->d_name);
      printf("Filename: %s\n", filename);
      FILE *wavFile = fopen(filename, "rb");
      if(wavFile != NULL)  {
        printf("Wav File Opened.\nNow Playing: %s\n", filename);
        setVolume(-20);
        player_pause(false);
        wavPlay(wavFile);

        player_pause(true);
        fclose(wavFile);
        printf("Wav File Closed.\n");
      }
      else {
        printf("Failed to open wav file.\n");
      }
    }
  }
  while(1);
}
