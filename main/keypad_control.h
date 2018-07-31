#ifndef _KEYPAD_CONTROL_
#define _KEYPAD_CONTROL_

#define KEY_NUM 5

#define KEY_PRESSED 1
#define KEY_RELEASED 0

#define KEY_MID 0x70

typedef struct {
	uint32_t key_name;
	lv_indev_state_t state;
} key_event_t;

extern TickType_t key_last_tick;
extern lv_indev_t *keypad_indev;
extern QueueHandle_t Queue_Key;
void taskScanKey(void *parameter);
esp_err_t keyQueueCreate();
bool keypad_read(lv_indev_data_t *data);
#endif
