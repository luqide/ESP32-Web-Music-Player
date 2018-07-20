#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
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
#include "picojpeg.h"
#include "../lvgl/lvgl.h"

#include "i2s_dac.h"
#include "keypad_control.h"
#include "ui.h"

LV_IMG_DECLARE(default_cover);

int batteryVoltage = 0;
int batteryPercentage = 0;
int rawData = 0;
bool wifi_connected = false;
uint8_t menuID = 0;

#define HOME_LIST_NUM 4

static const char *TAG = "UI";
key_event_t key_event;
int selected = 0;
lv_obj_t *status_bar, *battery_icon, *battery_text, *volume, *wifi_icon, *playing_icon;
lv_obj_t *screen, *home_list, *home_list_btns[HOME_LIST_NUM];
lv_obj_t *img_cover, *info_obj, *now_playing, *author, *album, *sample_info, *time_text, *time_bar, *playmode;
lv_style_t status_bar_style, status_bar_icon_style, title_40, title_20;

typedef struct {
	size_t inBufSize, inBufOffset;
	FILE *fPtr;
	uint8_t *inBuf;
} JPEG_Decoder_t;

JPEG_Decoder_t *JPEG_Decoder;
unsigned char pjpeg_callback (
	unsigned char* pBuf, 
	unsigned char buf_size, 
	unsigned char *pBytes_actually_read, 
	void *pCallback_data) {

	unsigned int n;

	n = min(JPEG_Decoder->inBufSize - JPEG_Decoder->inBufOffset, buf_size);

	if(JPEG_Decoder->fPtr != NULL) {
		*pBytes_actually_read = fread(pBuf, sizeof(unsigned char), n, JPEG_Decoder->fPtr);
	}
	else {
		for(int i = 0; i < n; ++i)
			pBuf[i] = *(JPEG_Decoder->inBuf + JPEG_Decoder->inBufOffset + i);
		*pBytes_actually_read = (uint8_t)n;
	}

	JPEG_Decoder->inBufOffset += n;
	return 0;
}

static int lv_img_set_src_jpg(lv_obj_t *img_obj, char *fname, uint8_t *buf, int size) {
	if(img_obj == NULL) return 0;
	pjpeg_image_info_t image_info;
	JPEG_Decoder = heap_caps_malloc(sizeof(JPEG_Decoder_t), MALLOC_CAP_SPIRAM);
	memset(JPEG_Decoder, 0, sizeof(JPEG_Decoder_t));

	if(fname != NULL) {
		JPEG_Decoder->fPtr = fopen(fname, "rb");
		if(JPEG_Decoder->fPtr == NULL) {
			ESP_LOGE(TAG, "Unable to open jpeg file.");
			free(JPEG_Decoder);
			return 0;
		}
		fseek(JPEG_Decoder->fPtr, 0, SEEK_END);
		JPEG_Decoder->inBufSize = ftell(JPEG_Decoder->fPtr);
		rewind(JPEG_Decoder->fPtr);
	} 
	else {
		if(buf == NULL) {
			free(JPEG_Decoder);
			return 0;
		}
		JPEG_Decoder->inBufSize = size;
		JPEG_Decoder->inBuf = buf;
	}

	unsigned char status = pjpeg_decode_init(&image_info, pjpeg_callback, NULL, 0);
	if(status) {
		free(JPEG_Decoder);
		return 0;
	}

	ESP_LOGI(TAG, "Image size: %ix%i", image_info.m_width, image_info.m_height);
	ESP_LOGI(TAG, "MCU size: %ix%i", image_info.m_MCUWidth, image_info.m_MCUHeight);

	

	status = 0;
	int pos = 0;
	uint16_t pixel;
	// for(int mY = 0; mY < image_info.m_MCUSPerRow; ++mY) {
	// 	if(status) {
	// 		if(status == PJPG_NO_MORE_BLOCKS) break;
	// 		else return 0;
	// 	} 
	// 	for(int mX = 0; mX < image_info.m_MCUSPerCol; ++mX) {
	// 		status = pjpeg_decode_mcu();
	// 		if(status) {
	// 			if(status == PJPG_NO_MORE_BLOCKS) break;
	// 			else return 0;
	// 		} 
	// 		if(image_info.m_scanType == PJPG_GRAYSCALE) {
	// 			for(int i = 0; i < image_info.m_MCUHeight; ++i) {
	// 				for(int j = 0; j < image_info.m_MCUWidth; ++j) {
	// 					pixel = (image_info.m_pMCUBufR[i * (image_info.m_MCUWidth) + j] & 0xF8) << 8
	// 							| (image_info.m_pMCUBufR[i * (image_info.m_MCUWidth) + j] & 0xFC) << 3
	// 							| (image_info.m_pMCUBufR[i * (image_info.m_MCUWidth) + j] & 0xF8) >> 3;
	// 					img_pixels[pos] = (uint8_t)(pixel >> 8);
	// 					img_pixels[pos + 1] = (uint8_t)(pixel & 0xff);
	// 				}
	// 			}
	// 		}
	// 		else {
	// 			for(int i = 0; i < image_info.m_MCUHeight; ++i) {
	// 				for(int j = 0; j < image_info.m_MCUWidth; ++j) {
	// 					pixel = (image_info.m_pMCUBufR[i * (image_info.m_MCUWidth) + j] & 0xF8) << 8
	// 							| (image_info.m_pMCUBufG[i * (image_info.m_MCUWidth) + j] & 0xFC) << 3
	// 							| (image_info.m_pMCUBufB[i * (image_info.m_MCUWidth) + j] & 0xF8) >> 3;
	// 					img_pixels[pos] = (uint8_t)(pixel >> 8);
	// 					img_pixels[pos + 1] = (uint8_t)(pixel & 0xff);
	// 				}
	// 			}
	// 		}
	// 		pos += 2;
	// 	}
	// }
	
	free(JPEG_Decoder);
	return 1;
}

static void style_init() {
	lv_style_copy(&status_bar_style, &lv_style_scr);
	status_bar_style.body.main_color = LV_COLOR_BLACK;
	status_bar_style.body.grad_color = LV_COLOR_BLACK;
	status_bar_style.text.color = LV_COLOR_BLACK;
	status_bar_style.image.color = LV_COLOR_BLACK;
	status_bar_style.line.color = LV_COLOR_BLACK;

	lv_style_copy(&status_bar_icon_style, &lv_style_plain);
	status_bar_icon_style.text.color = LV_COLOR_HEX(0xFFFFFF);

	lv_style_copy(&title_40, &lv_style_plain);
	title_40.text.color = LV_COLOR_WHITE;
	title_40.text.font = &lv_font_dejavu_40;

	lv_style_copy(&title_20, &lv_style_plain);
	title_20.text.color = LV_COLOR_WHITE;
	title_20.text.font = &lv_font_dejavu_20;
}

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
 	const TickType_t xFrequency = 15*1000 / portTICK_RATE_MS;
 	xLastWakeTime = xTaskGetTickCount();
	while(1) {
		getBatteryPecentage();
		ESP_LOGI(TAG, "Battery voltage: %i mV", batteryVoltage);

		ESP_LOGI(TAG, "Battery pecentage: %i %%", batteryPercentage);
		vTaskDelayUntil(&xLastWakeTime, xFrequency);
	}
}
void clear_screen() {
	lv_obj_clean(screen);
}

void drawHomeScreen() {
	selected = 0;
	home_list = lv_list_create(screen, NULL);
	lv_obj_set_size(home_list, 170, 216);
	home_list_btns[0] = lv_list_add(home_list, SYMBOL_AUDIO, "Library", NULL);
	home_list_btns[1] = lv_list_add(home_list, SYMBOL_IMAGE, "Gallery", NULL);
	home_list_btns[2] = lv_list_add(home_list, SYMBOL_SETTINGS, "Settings", NULL);
	home_list_btns[3] = lv_list_add(home_list, SYMBOL_PLAY, "Now Playing", NULL);
	lv_btn_set_state(home_list_btns[selected], LV_BTN_STATE_PR);
}

void drawPlaying() {
	img_cover = lv_img_create(screen, NULL);
	lv_img_set_src(img_cover, &default_cover);
	lv_obj_set_pos(img_cover, 15, 15);
	lv_obj_set_size(img_cover, 128, 128);

	info_obj = lv_obj_create(screen, screen);
	lv_obj_set_pos(info_obj, 160, 20);
	lv_obj_set_size(info_obj, 150, 128);

	now_playing = lv_label_create(info_obj, NULL);
	lv_label_set_style(now_playing, &title_20);
	lv_obj_set_pos(now_playing, 0, 0);
	lv_label_set_long_mode(now_playing, LV_LABEL_LONG_SCROLL);
	lv_label_set_text(now_playing, playerState.fileName);

	author = lv_label_create(info_obj, NULL);
	lv_label_set_style(author, &title_20);
	lv_obj_set_pos(author, 0, 30);
	lv_label_set_text(author, playerState.author);
	lv_label_set_long_mode(author, LV_LABEL_LONG_SCROLL);

	album = lv_label_create(info_obj, author);
	lv_obj_set_pos(album, 0, 60);
	lv_label_set_text(album, playerState.album);
	lv_label_set_long_mode(album, LV_LABEL_LONG_SCROLL);

	sample_info = lv_label_create(info_obj, author);
	lv_obj_set_pos(sample_info, 0, 90);
	lv_label_set_text(sample_info, "44100Hz 16-bit");

	time_bar = lv_bar_create(screen, NULL);
	lv_obj_set_size(time_bar, 290, 10);
	lv_obj_set_pos(time_bar, 15, 187);
	lv_bar_set_value(time_bar, 25);

	time_text = lv_label_create(screen, author);
	lv_obj_set_pos(time_text, 15, 158);
	lv_label_set_text(time_text, "0:00 / 0:00");
}

void taskUI_Char(void *parameter) {
	lv_theme_t *th = lv_theme_material_init(210, NULL);
	lv_theme_set_current(th);

	style_init();
	status_bar = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_size(status_bar, 320, 24);
	lv_obj_set_style(status_bar, &status_bar_style);

	battery_icon = lv_label_create(status_bar, NULL);
	lv_label_set_style(battery_icon, &status_bar_icon_style);
	lv_obj_set_pos(battery_icon, 5, 2);

	battery_text = lv_label_create(status_bar, NULL);
	lv_label_set_style(battery_text, &status_bar_icon_style);
	lv_obj_set_pos(battery_text, 35, 3);
	lv_label_set_text(battery_text, "100%");

	playing_icon = lv_label_create(status_bar, NULL);
	lv_label_set_style(playing_icon, &status_bar_icon_style);
	lv_obj_set_pos(playing_icon, 295, 2);
	lv_label_set_text(playing_icon, SYMBOL_PLAY);

	volume =lv_label_create(status_bar, NULL);
	lv_label_set_style(volume, &status_bar_icon_style);
	lv_obj_set_pos(volume, 85, 2);

	wifi_icon = lv_label_create(status_bar, NULL);
	lv_label_set_style(wifi_icon, &status_bar_icon_style);
	lv_obj_set_pos(wifi_icon, 265, 2);

	screen = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_size(screen, 320, 216);
	lv_obj_set_pos(screen, 0, 24);
	lv_obj_set_style(screen, &status_bar_style);

	drawHomeScreen();
	
	while(1) {
		if(batteryPercentage == 0) lv_label_set_text(battery_icon, SYMBOL_BATTERY_EMPTY);
		else if(batteryPercentage <=25) lv_label_set_text(battery_icon, SYMBOL_BATTERY_1);
		else if(batteryPercentage > 25 && batteryPercentage <= 50) lv_label_set_text(battery_icon, SYMBOL_BATTERY_2);
		else if(batteryPercentage > 50 && batteryPercentage <= 75) lv_label_set_text(battery_icon, SYMBOL_BATTERY_3);
		else lv_label_set_text(battery_icon, SYMBOL_BATTERY_FULL);

		char tmp_str[16];
		memset(tmp_str, 0, sizeof(tmp_str));
		sprintf(tmp_str, "%i%%", batteryPercentage);
		lv_label_set_text(battery_text, tmp_str);

		memset(tmp_str, 0, sizeof(tmp_str));
		int v = getVolumePercentage();
		if(v == 0) sprintf(tmp_str, "%s0%%", SYMBOL_MUTE);
		else sprintf(tmp_str, "%s%i%%", SYMBOL_VOLUME_MAX, v);
		lv_label_set_text(volume, tmp_str);

		lv_label_set_text(playing_icon, isPaused() ? SYMBOL_PAUSE : SYMBOL_PLAY);
		lv_label_set_text(wifi_icon, wifi_connected ? SYMBOL_WIFI : " ");

		switch(menuID) {
			case 0: // home screen
				if(xQueueReceive(Queue_Key, &key_event, 100) == pdPASS) {
				    if(key_event.pressed == false) {
					    switch(key_event.key_name) {
					    	case KEY_NAME_UP:
					    		lv_btn_set_state(home_list_btns[selected], LV_BTN_STATE_REL);
					    		selected--;
					    		if(selected < 0) selected = HOME_LIST_NUM - 1;
					    		lv_btn_set_state(home_list_btns[selected], LV_BTN_STATE_PR);
					    		lv_list_focus(home_list_btns[selected],true);
					    		break;
					        case KEY_NAME_DOWN:
					        	lv_btn_set_state(home_list_btns[selected], LV_BTN_STATE_REL);
					        	selected++;
					        	if(selected == HOME_LIST_NUM) selected = 0;
					        	lv_btn_set_state(home_list_btns[selected], LV_BTN_STATE_PR);
					        	lv_list_focus(home_list_btns[selected],true);
					        	break;
					   		case KEY_NAME_MENU:
					   			switch(selected) {
					   				case 0:menuID = 1;clear_screen();break;
					   				case 1:menuID = 2;clear_screen();break;
					   				case 2:menuID = 3;clear_screen();break;
					   				case 3:menuID = 4;clear_screen();drawPlaying();break;
					   				default:selected = 0;break;
					   			}
					   			break;
					     	case KEY_NAME_MID:
					        	player_pause(!isPaused());break;
					     	default:break;
					    }
					}
				}
			break;

			case 1:

			break;

			case 2:

			break;

			case 3:

			break;

			case 4:
				if(xQueueReceive(Queue_Key, &key_event, 100) == pdPASS) {
				    if(key_event.pressed == false) {
					    switch(key_event.key_name) {
					    	case KEY_NAME_UP:
					    		setVolume(getVolumePercentage() + 10);
					    		break;
					        case KEY_NAME_DOWN:
					        	setVolume(getVolumePercentage() - 10);
					        	break;
					   		case KEY_NAME_MENU:

					   			break;
					     	case KEY_NAME_MID:
					        	player_pause(!isPaused());
					        break;
					        case KEY_NAME_BACK:
					        	clear_screen();
					        	drawHomeScreen();
					        	menuID = 0;
					        	break;
					     	default:break;
					    }
					}
				}
			break;
			default:break;
		}
		vTaskDelay(50 / portTICK_RATE_MS);
	}
}

void wifi_set_stat(bool c) {
	wifi_connected = c;
}