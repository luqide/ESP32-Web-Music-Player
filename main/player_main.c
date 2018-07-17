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

#include "sd_card.h"
#include "dirent.h"
#include "i2s_dac.h"
#include "ui.h"
#include "keypad_control.h"
#include "mp3dec.h"

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
TaskHandle_t batHandle = NULL;

void app_main() {
  esp_err_t ret;
  gpio_set_direction(PIN_PD, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_PD, 0);

  //spiffs mount
  esp_vfs_spiffs_conf_t conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 5,
    .format_if_mount_failed = false
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
    return;
  }

  ESP_LOGI(TAG, "Mount spiffs succeeded.");

  uint32_t tot=0, used=0;
  esp_spiffs_info(NULL, &tot, &used);
  ESP_LOGI("TAG", "SPIFFS: free %d KB of %d KB\n", (tot-used) / 1024, tot / 1024);
  //sdcard init
  sdmmc_card_t card;
  sdmmc_mount(&card);

  //WiFi Init
  nvs_flash_init();
  tcpip_adapter_init();
  tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, "mPlayer");
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
  ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
  ESP_ERROR_CHECK( esp_wifi_set_auto_connect(1) );
  wifi_config_t sta_config = {
      .sta = {
          .ssid = "Molly_2.4G",
          .password = "bakaFANCY520",
          .bssid_set = false
      }
  };
  ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
  ESP_ERROR_CHECK( esp_wifi_start() );
  ESP_ERROR_CHECK( esp_wifi_connect() );

  //keypad init
  ESP_ERROR_CHECK(keyQueueCreate());
  if(xTaskCreatePinnedToCore(taskScanKey,"KEYSCAN",2000,NULL,(portPRIVILEGE_BIT | 2),&keyHandle,1) == pdPASS) 
    ESP_LOGI(TAG, "KeyScan task created.");
  else ESP_LOGE(TAG, "Failed to create KeyScan task.");
  //battery task init
  if(xTaskCreatePinnedToCore(taskBattery,"BATTERY",3000,NULL,(portPRIVILEGE_BIT | 2),&batHandle,1) == pdPASS) 
    ESP_LOGI(TAG, "Battery voltage scanning task created.");
  else ESP_LOGE(TAG, "Failed to create Battery voltage scanning task.");

  // if(xTaskCreatePinnedToCore(taskUI_Char,"UI",8192,NULL,(portPRIVILEGE_BIT | 4),&uiHandle,0) == pdPASS) 
  //   ESP_LOGI(TAG, "UI_Char task created.");
  // else ESP_LOGE(TAG, "Failed to create UI_Char task.");

  //i2s init
  i2s_init();

  setNowPlaying("/sdcard/MP3/无题 - 陈亮.mp3");
  player_pause(true);
  dac_mute(false);

  FILE *musicFile = NULL;
  while(1) {
    if(isPaused() == false) { 
      parseMusicType();
      if(getMusicType() != NONE) {
        musicFile = musicFileOpen();
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
              //mp3Play(musicFile);
              break;
            case APE:
              break;
            case FLAC:
              break;
            default:
              break;
          }
          fclose(musicFile);
        }
      } else {
          ESP_LOGE(TAG, "Unsupported music file type.");
          player_pause(true);
      }


    }
  }
}
