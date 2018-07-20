#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"

#include "i2s_dac.h"

#include "keypad_control.h"

QueueHandle_t Queue_Key;
//bool keyStats[KEY_NUM];
static const char* TAG = "KEYPAD";
key_event_t keyEvent;

void taskScanKey(void *patameter) {
	int data;
	while(1) {
		adc1_config_width(ADC_WIDTH_BIT_12);
		adc1_config_channel_atten(ADC1_CHANNEL_3,ADC_ATTEN_DB_6);
		data = adc1_get_raw(ADC1_CHANNEL_3);
		data = (int)((double)data / 4096.0 * 2200);

		if(data > 0 && data < 225) keyEvent.key_name = KEY_NAME_RIGHT;
		else if(data >= 225 && data < 476) keyEvent.key_name = KEY_NAME_UP;
		else if(data >= 476 && data < 700) keyEvent.key_name = KEY_NAME_LEFT;
		else if(data >= 700 && data < 952) keyEvent.key_name = KEY_NAME_MID;
		else if(data >= 952 && data < 1296) keyEvent.key_name = KEY_NAME_DOWN;
		else if(data >= 1296 && data < 1673) keyEvent.key_name = KEY_NAME_MENU;
		else if(data >= 1673) keyEvent.key_name = KEY_NAME_BACK;
		else {
			vTaskDelay(20 / portTICK_RATE_MS);
			continue;
		}

		keyEvent.pressed = KEY_PRESSED;
		//ESP_LOGI(TAG, "key %i pressed.", keyEvent.key_name);
		xQueueSend(Queue_Key, (void*)(&keyEvent), (TickType_t) 10);
		while(adc1_get_raw(ADC1_CHANNEL_3) != 0) vTaskDelay(10 / portTICK_RATE_MS);
		//ESP_LOGI(TAG, "key %i released.", keyEvent.key_name);
		keyEvent.pressed = KEY_RELEASED;
		xQueueSend(Queue_Key, (void*)(&keyEvent), (TickType_t) 10);
		vTaskDelay(20 / portTICK_RATE_MS);
	}

}

esp_err_t keyQueueCreate() {
	Queue_Key = xQueueCreate(16, sizeof(key_event_t));
	if(Queue_Key == 0) return ESP_FAIL;

	return ESP_OK;
}
