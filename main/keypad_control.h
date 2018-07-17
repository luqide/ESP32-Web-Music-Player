#ifndef _KEYPAD_CONTROL_
#define _KEYPAD_CONTROL_

#define KEY_NUM 5

#define KEY_PRESSED 1
#define KEY_RELEASED 0

enum key_name {
	KEY_NAME_UP = 0,
	KEY_NAME_DOWN,
	KEY_NAME_LEFT,
	KEY_NAME_RIGHT,
	KEY_NAME_MID,
	KEY_NAME_MENU,
	KEY_NAME_BACK
};

typedef struct {
	int key_name;
	bool pressed;
} key_event_t;

extern QueueHandle_t Queue_Key;
void taskScanKey(void *parameter);
esp_err_t keyQueueCreate();
#endif