#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_log.h"

#include "keypad_control.h"

QueueHandle_t Queue_Key;
bool keyStats[KEY_NUM];
const char* TAG = "KEYPAD";
key_event_t keyEvent;

const int key_pins[KEY_NUM] = {
	35, 34, 5, 27, 18
};

void keyPinInit() {
	for(int i = 0; i < 5; ++i) {
		gpio_pad_select_gpio(key_pins[i]);
		gpio_set_direction(key_pins[i], GPIO_MODE_INPUT);
		gpio_set_pull_mode(key_pins[i], GPIO_PULLUP_ONLY);
	}
}

void taskScanKey(void *patameter) {
	while(1) {
//		ESP_LOGI(TAG, "task stack: %d", uxTaskGetStackHighWaterMark(NULL));
		for(int i = 0; i < 5; ++i) {
			if(keyStats[i] != gpio_get_level(key_pins[i])) {
				vTaskDelay(20 / portTICK_RATE_MS);
				if(keyStats[i] != gpio_get_level(key_pins[i])) {
					keyStats[i] = !keyStats[i];
					keyEvent.key_name = i;
					keyEvent.pressed = !keyStats[i];

					xQueueSend(Queue_Key, (void*)(&keyEvent), (TickType_t) 10);
				}
			}
		}
		vTaskDelay(20 / portTICK_RATE_MS);
	}
}

esp_err_t keyQueueCreate() {
	Queue_Key = xQueueCreate(16, sizeof(key_event_t));
	if(Queue_Key == 0) return ESP_FAIL;

	return ESP_OK;
}
