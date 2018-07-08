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
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "nvs_flash.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

#include "tftspi.h"
#include "tft.h"
#include "sd_card.h"
#include "dirent.h"
#include "i2s_dac.h"
#include "ui.h"
#include "keypad_control.h"

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static const char *TAG = "APP_MAIN";
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event) {
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

TaskHandle_t keyHandle = NULL; 
TaskHandle_t playerHandle = NULL;
TaskHandle_t uiHandle = NULL;

void app_main() {
  esp_err_t ret;
  gpio_set_direction(PIN_PD, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_PD, 0);

  //spiffs mount
  esp_vfs_spiffs_conf_t conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 5,
    .format_if_mount_failed = true
  };
  
  // Use settings defined above to initialize and mount SPIFFS filesystem.
  // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
  ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to find SPIFFS partition");
    } else {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%d)", ret);
    }
    return 0;
  }

  vTaskDelay(1000 / portTICK_RATE_MS);


  TFT_PinsInit();
  //initialize spi bus
  spi_lobo_bus_config_t buscfg = {
    .miso_io_num = PIN_NUM_MISO,
    .mosi_io_num = PIN_NUM_MOSI,
    .sclk_io_num = PIN_NUM_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 240*320*2*8,
  };

  spi_lobo_device_interface_config_t lcd_devcfg = {
      .clock_speed_hz=30000000,                // Initial clock out at 8 MHz
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
  //TFT_fillRect(0, 0, 320, 240, TFT_WHITE);

  

  //WiFi Init
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

  //keypad init
  keyPinInit();
  ESP_ERROR_CHECK(keyQueueCreate());
  if(xTaskCreatePinnedToCore(taskScanKey,"KEYSCAN",3000,NULL,(portPRIVILEGE_BIT | 2),&keyHandle,1) == pdPASS) 
    ESP_LOGI(TAG, "KeyScan task created.");
  else ESP_LOGE(TAG, "Failed to create KeyScan task.");

  //sdcard init
  sdmmc_card_t card;
  sdmmc_mount(&card);

  //i2s init
  i2s_init();

  setNowPlaying("/sdcard/WAV/无题 - 陈亮.wav");
  player_pause(false);

  while(1) {
    if(isPaused() == false) { 
      parseMusicType();
      if(getMusicType() != NONE) {
        FILE *musicFile = musicFileOpen();
        if(musicFile == NULL) {
          ESP_LOGE(TAG, "Failed to open music file.");
          player_pause(true);
        } else {
          switch(getMusicType()) {
            case NONE:
              break;
            case WAV:
              wavPlay(musicFile);
              break;
            case MP3:
              
              break;
            case APE:
              break;
            case FLAC:
              break;
            default:
              break;
          }
        }
      } else {
          ESP_LOGE(TAG, "Unsupported music file type.");
          player_pause(true);
      }


    }
  }
}
