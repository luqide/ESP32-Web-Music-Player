#ifndef _LEDC_H_
#define _LEDC_H_

#define LEDC_GPIO (21)
#define LEDC_TIMER          LEDC_TIMER_1
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0

extern uint8_t backlight_duty;
extern TickType_t backlight_timeout;
void ledc_init(void);
void taskBacklight(void *parameter);

#endif
