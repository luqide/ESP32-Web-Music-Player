#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "../lvgl/lvgl.h"
#include "keypad_control.h"
#include "ledc.h"

uint8_t backlight_duty;
uint32_t backlight_timeout;
void ledc_init() {
  gpio_set_direction(LEDC_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(LEDC_GPIO, 0);
  backlight_duty = 100;
  backlight_timeout = 0 * 1000 / portTICK_RATE_MS;//ms
  ledc_timer_config_t ledc_timer = {
      .duty_resolution = LEDC_TIMER_13_BIT, // resolution of PWM duty
      .freq_hz = 2000,                      // frequency of PWM signal
      .speed_mode = LEDC_MODE,           // timer mode
      .timer_num = LEDC_TIMER            // timer index
  };
  ledc_timer_config(&ledc_timer);
  ledc_channel_config_t ledc_channel = {
      .channel    = LEDC_CHANNEL,
      .duty       = 0,
      .gpio_num   = LEDC_GPIO,
      .speed_mode = LEDC_MODE,
      .timer_sel  = LEDC_TIMER
  };
  ledc_channel_config(&ledc_channel);
}

void taskBacklight(void *parameter) {
  while(1) {
    if(xTaskGetTickCount() - key_last_tick >= backlight_timeout && backlight_timeout != 0) {
      ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
      ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    } else {
      ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, (double)(backlight_duty / 100.0) * (1 << LEDC_TIMER_13_BIT));
      ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
    vTaskDelay(50 / portTICK_RATE_MS);
  }
}
