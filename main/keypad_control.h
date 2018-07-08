#ifndef _KEYPAD_CONTROL_
#define _KEYPAD_CONTROL_

#define KEY_NUM 5

#define KEY_PRESSED 0
#define KEY_RELEASED 1

enum key_name {
	KEY_NAME_UP = 0,
	KEY_NAME_DOWN,
	KEY_NAME_LEFT,
	KEY_NAME_RIGHT,
	KEY_NAME_MID
};

typedef struct {
	int key_name;
	bool pressed;
} key_event_t;

extern QueueHandle_t Queue_Key;
void keyPinInit();
void taskScanKey(void *parameter);
esp_err_t keyQueueCreate();
#endif