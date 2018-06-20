#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"

#include "ili9341.h"
//#include "sd_card.h"

void app_main() {
  //initialize spi bus
  esp_err_t ret;
  spi_device_handle_t spi;
  spi_bus_config_t buscfg = {
    .miso_io_num = PIN_MISO,
    .mosi_io_num = PIN_MOSI,
    .sclk_io_num = PIN_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = LCD_HEIGHT * LCD_WIDTH * 2 + 8
  };
  spi_device_interface_config_t devcfg = {
    .clock_speed_hz = 33 * 1000 * 1000,
    .mode = 0,
    .queue_size = 7,
    .pre_cb = lcd_spi_pre_transfer_callback
  };
  ret = spi_bus_initialize(HSPI_HOST, &buscfg, 1);
  ESP_ERROR_CHECK(ret);
  ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
  ESP_ERROR_CHECK(ret);
  printf("ILI9341 init\n");
  lcd_init(spi);
  lcd_fill(bgr_to_uint(0, 0, 0));
  lcd_display(spi);
  srand(time((void*)NULL));
  while(1) {
    uint16_t c = bgr_to_uint(rand() % 256, rand() % 256, rand() % 256);
    drawChar(159, 119, 'A', c, bgr_to_uint(0, 0, 0), 2);
    lcd_display(spi);
    //vTaskDelay(500 / portTICK_RATE_MS);
  }

}
