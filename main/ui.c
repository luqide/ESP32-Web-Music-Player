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

#include "i2s_dac.h"
#include "keypad_control.h"
#include "ui.h"

int batteryVoltage = 0;
int batteryPercentage = 0;
enum {
	EMPTY = 0, QUARTER, HALF, QUARTER_TO, FULL
} batteryIcon = FULL, batteryIcon_calc;
int rawData = 0;
uint8_t menuID = 0;

#define HOME_LIST_NUM 4
static const char *TAG = "UI";
static const char homeMenuList[HOME_LIST_NUM][16] = {
	"Now Playing", "Library", "Gallery", "Settings"
};

void getBatteryPecentage() {
	adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0,ADC_ATTEN_DB_11);
    rawData = adc1_get_raw(ADC1_CHANNEL_0);

	ESP_LOGI(TAG, "ADC rawData: %i", rawData);
    batteryVoltage = (double)rawData / 4096.0 * 3578.0 * 175.0 / 100.0;
    batteryPercentage = ((double)batteryVoltage - 3700) / 500.0 * 100;
}

void taskBattery(void *parameter) {
	TickType_t xLastWakeTime;
 	const TickType_t xFrequency = 30*1000 / portTICK_RATE_MS;
 	xLastWakeTime = xTaskGetTickCount();
	while(1) {
		getBatteryPecentage();
		ESP_LOGI(TAG, "Battery voltage: %i mV", batteryVoltage);

		ESP_LOGI(TAG, "Battery pecentage: %i %%", batteryPercentage);
		vTaskDelayUntil(&xLastWakeTime, xFrequency);
	}
}

void taskUI_Char(void *parameter) {
	
}