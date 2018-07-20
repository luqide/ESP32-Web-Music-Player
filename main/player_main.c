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
#include "esp_freertos_hooks.h"

#include "../lvgl/lvgl.h"
#include "../drv/disp_spi.h"
#include "../drv/ili9341.h"
#include "sd_card.h"
#include "dirent.h"
#include "i2s_dac.h"
#include "ui.h"
#include "keypad_control.h"
#include "mp3dec.h"

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static const char *TAG = "APP_MAIN";
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event);
static void lv_tick_task(void);

bool keypad_read(lv_indev_data_t *data);

typedef FILE* pc_file_t;
static lv_fs_res_t pcfs_open(void * file_p, const char * fn, lv_fs_mode_t mode);
static lv_fs_res_t pcfs_close(void * file_p);
static lv_fs_res_t pcfs_read(void * file_p, void * buf, uint32_t btr, uint32_t * br);
static lv_fs_res_t pcfs_seek(void * file_p, uint32_t pos);
static lv_fs_res_t pcfs_tell(void * file_p, uint32_t * pos_p);

TaskHandle_t keyHandle = NULL; 
TaskHandle_t playerHandle = NULL;
TaskHandle_t uiHandle = NULL;
TaskHandle_t batHandle = NULL;

sdmmc_card_t card;

void app_main() {
  esp_err_t ret;
  gpio_set_direction(PIN_PD, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_PD, 1);

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
  sdmmc_mount(&card);

  //littlevgl init
  lv_init();
  disp_spi_init();
  ili9431_init();
  lv_disp_drv_t disp;
  lv_disp_drv_init(&disp);
  disp.disp_flush = ili9431_flush;
  lv_disp_drv_register(&disp);

  lv_fs_drv_t pcfs_drv;
  memset(&pcfs_drv, 0, sizeof(lv_fs_drv_t));
  pcfs_drv.file_size = sizeof(pc_file_t);       /*Set up fields...*/
  pcfs_drv.letter = 'S';
  pcfs_drv.open = pcfs_open;
  pcfs_drv.close = pcfs_close;
  pcfs_drv.read = pcfs_read;
  pcfs_drv.seek = pcfs_seek;
  pcfs_drv.tell = pcfs_tell;
  lv_fs_add_drv(&pcfs_drv);


  //set up littlevgl input device
  // lv_indev_drv_t indev_drv;
  // lv_indev_drv_init(&indev_drv);
  // indev_drv.type = LV_INDEV_TYPE_KEYPAD;
  // indev_drv.read = keypad_read;
  // lv_indev_drv_register(&indev_drv);

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
  if(xTaskCreatePinnedToCore(taskBattery,"BATTERY",2000,NULL,(portPRIVILEGE_BIT | 2),&batHandle,1) == pdPASS) 
    ESP_LOGI(TAG, "Battery voltage scanning task created.");
  else ESP_LOGE(TAG, "Failed to create Battery voltage scanning task.");

  if(xTaskCreatePinnedToCore(taskUI_Char,"UI",8192,NULL,(portPRIVILEGE_BIT | 4),&uiHandle,0) == pdPASS) 
    ESP_LOGI(TAG, "UI_Char task created.");
  else ESP_LOGE(TAG, "Failed to create UI_Char task.");

  if(xTaskCreatePinnedToCore(taskPlay,"Player",4096,NULL,(portPRIVILEGE_BIT | 3),&uiHandle,1) == pdPASS) 
    ESP_LOGI(TAG, "Music Player task created.");
  else ESP_LOGE(TAG, "Failed to create Player task.");

  //i2s init
  i2s_init();
  setNowPlaying("/sdcard/MP3/Do You Wanna Build a Snowman.mp3");
  player_pause(false);
  playerState.started = true;

  esp_register_freertos_tick_hook(lv_tick_task);
  while(1) {
    vTaskDelay(5 / portTICK_RATE_MS);
    lv_task_handler();
  }
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event) {
    switch(event->event_id) {
      case SYSTEM_EVENT_STA_START:
          esp_wifi_connect();
          break;
      case SYSTEM_EVENT_STA_GOT_IP:
          wifi_set_stat(true);
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
          wifi_set_stat(false);
          esp_wifi_connect();
          xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
          break;
      default:
          break;
    }
    return ESP_OK;
}

static void lv_tick_task(void) {
  lv_tick_inc(portTICK_RATE_MS);
}

static lv_fs_res_t pcfs_open(void * file_p, const char * fn, lv_fs_mode_t mode)
{
    errno = 0;

    const char * flags = "";

    if(mode == LV_FS_MODE_WR) flags = "wb";
    else if(mode == LV_FS_MODE_RD) flags = "rb";
    else if(mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) flags = "a+";

    /*Make the path relative to the current directory (the projects root folder)*/
    // char buf[256];
    // sprintf(buf, "./%s", fn);

    pc_file_t f = fopen(fn, flags);
    if((long int)f <= 0) return LV_FS_RES_UNKNOWN;
    else {
        fseek(f, 0, SEEK_SET);

        /* 'file_p' is pointer to a file descriptor and
         * we need to store our file descriptor here*/
        pc_file_t * fp = file_p;        /*Just avoid the confusing casings*/
        *fp = f;
    }

    return LV_FS_RES_OK;
}

static lv_fs_res_t pcfs_close(void * file_p)
{
    pc_file_t * fp = file_p;        /*Just avoid the confusing casings*/
    fclose(*fp);
    return LV_FS_RES_OK;
}

static lv_fs_res_t pcfs_read(void * file_p, void * buf, uint32_t btr, uint32_t * br)
{
    pc_file_t * fp = file_p;        /*Just avoid the confusing casings*/
    *br = fread(buf, 1, btr, *fp);
    return LV_FS_RES_OK;
}

static lv_fs_res_t pcfs_seek(void * file_p, uint32_t pos)
{
    pc_file_t * fp = file_p;        /*Just avoid the confusing casings*/
    fseek(*fp, pos, SEEK_SET);
    return LV_FS_RES_OK;
}

static lv_fs_res_t pcfs_tell(void * file_p, uint32_t * pos_p)
{
    pc_file_t * fp = file_p;        /*Just avoid the confusing casings*/
    *pos_p = ftell(*fp);
    return LV_FS_RES_OK;
}