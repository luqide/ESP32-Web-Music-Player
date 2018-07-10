#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "driver/adc.h"

#include "tftspi.h"
#include "tft.h"
#include "i2s_dac.h"
#include "keypad_control.h"
#include "ui.h"

int batteryVoltage = 0;
int batteryPercentage = 0;
int rawData = 0;
uint8_t menuID = 0;

static const char *TAG = "UI";

void getBatteryPecentage() {
	adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0,ADC_ATTEN_DB_0);
    rawData = adc1_get_raw(ADC1_CHANNEL_0);
    batteryVoltage = (double)(rawData + 10) / 4096.0 * 3300.0 *1.27;
    batteryPercentage = (double)batteryVoltage / 42.0;
}

void taskBattery(void *parameter) {
	TickType_t xLastWakeTime;
 	const TickType_t xFrequency = 30*1000 / portTICK_RATE_MS;
 	xLastWakeTime = xTaskGetTickCount();
	while(1) {
		getBatteryPecentage();
		ESP_LOGI(TAG, "Battery voltage: %i mV", batteryVoltage);
//		ESP_LOGI(TAG, "ADC RawData: %i", rawData);

		vTaskDelayUntil(&xLastWakeTime, xFrequency);
	}
}

void taskUI_Char(void *parameter) {
	TFT_fillRect(0, 0, 320, 24, TFT_BLACK);
	TFT_fillRect(0, 24, 320, 216, TFT_WHITE);
	char status[32];

	while(1) {
		memset(status, 0, sizeof(status));
		sprintf(status, "Battery: %i%% Volume: %i%% %s", batteryPercentage, getVolumePercentage(), isPaused() ? "Paused" : "Playing");

  		TFT_print(status, 0, 0);
  		switch (menuID) {
  			case 0:

  				break;

  			default:
  				menuID = 0;
  				break;

  		}
	}
}