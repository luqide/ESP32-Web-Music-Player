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
#include "../lvgl/lv_core/lv_group.h"
#include "../lvgl/lv_core/lv_indev.h"

#include "i2s_dac.h"

#include "keypad_control.h"

lv_indev_t *keypad_indev;
static uint32_t last_key;
static lv_indev_state_t state;
QueueHandle_t Queue_Key;
//bool keyStats[KEY_NUM];
static const char* TAG = "KEYPAD";
key_event_t keyEvent;
TickType_t key_last_tick;

void taskScanKey(void *patameter) {
	int data;
	while(1) {
		adc1_config_width(ADC_WIDTH_BIT_12);
		adc1_config_channel_atten(ADC1_CHANNEL_3,ADC_ATTEN_DB_6);
		data = adc1_get_raw(ADC1_CHANNEL_3);
		data = (int)((double)data / 4096.0 * 2200);

		if(data > 0 && data < 225) keyEvent.key_name = LV_GROUP_KEY_RIGHT;
		else if(data >= 225 && data < 476) keyEvent.key_name = LV_GROUP_KEY_UP;
		else if(data >= 476 && data < 700) keyEvent.key_name = LV_GROUP_KEY_LEFT;
		else if(data >= 700 && data < 952) {
			player_pause(!isPaused());
			while(adc1_get_raw(ADC1_CHANNEL_3) != 0) vTaskDelay(10 / portTICK_RATE_MS);
			key_last_tick = xTaskGetTickCount();
			continue;
		}
		else if(data >= 952 && data < 1296) keyEvent.key_name = LV_GROUP_KEY_DOWN;
		else if(data >= 1296 && data < 1673) keyEvent.key_name = LV_GROUP_KEY_ENTER;
		else if(data >= 1673) keyEvent.key_name = LV_GROUP_KEY_ESC;
		else {
			vTaskDelay(10 / portTICK_RATE_MS);
			continue;
		}

		keyEvent.state = LV_INDEV_STATE_PR;
		//ESP_LOGI(TAG, "key %i pressed.", keyEvent.key_name);
		last_key = keyEvent.key_name;
		state = LV_INDEV_STATE_PR;
		xQueueSend(Queue_Key, (void*)(&keyEvent), (TickType_t) 10);
		key_last_tick = xTaskGetTickCount();
		while(adc1_get_raw(ADC1_CHANNEL_3) != 0) vTaskDelay(10 / portTICK_RATE_MS);
		//ESP_LOGI(TAG, "key %i released.", keyEvent.key_name);
		keyEvent.state = KEY_RELEASED;
		state = LV_INDEV_STATE_REL;
		xQueueSend(Queue_Key, (void*)(&keyEvent), (TickType_t) 10);
		key_last_tick = xTaskGetTickCount();
		vTaskDelay(10 / portTICK_RATE_MS);
	}

}

esp_err_t keyQueueCreate() {
	key_last_tick = xTaskGetTickCount();
	Queue_Key = xQueueCreate(16, sizeof(key_event_t));
	if(Queue_Key == 0) return ESP_FAIL;

	return ESP_OK;
}

bool keypad_read(lv_indev_data_t *data) {
	key_event_t key_event;
	if(xQueueReceive(Queue_Key, &key_event, 10) == pdPASS) {
		data->state = key_event.state;
		data->key = key_event.key_name;
	} else {
		data->state = state;
		data->key = last_key;
	}

	return false;
}
