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
LV_FONT_DECLARE(hansans_20_cn);
LV_FONT_DECLARE(hansans_20_jp);

int batteryVoltage = 0;
int batteryPercentage = 0;
int rawData = 0;
bool wifi_connected = false;
uint8_t menuID = 0;

static const char *TAG = "UI";
lv_theme_t *th;
key_event_t key_event;
int selected = 0;
lv_obj_t *status_bar, *battery_icon, *battery_text, *volume, *wifi_icon, *playing_icon;
lv_obj_t *screen, *home_list, *library_list;
lv_obj_t *img_cover, *info_obj, *now_playing, *author, *album, *sample_info, *time_text, *time_bar, *playmode;
lv_style_t status_bar_style, status_bar_icon_style, title_20, style_focused;
lv_group_t *group;

static lv_res_t onclick_homelist(lv_obj_t * list_btn);
static lv_res_t onclick_library(lv_obj_t * list_btn);
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

	lv_font_add(&hansans_20_cn, &lv_font_dejavu_20);
	lv_font_add(&hansans_20_jp, &lv_font_dejavu_20);


	lv_style_copy(&title_20, &lv_style_plain);
	title_20.text.color = LV_COLOR_WHITE;
	title_20.text.font = &lv_font_dejavu_20;
}

lv_group_style_mod_func_t style_mod_cb(lv_style_t *style) {
	return &lv_style_transp;
}

int getBatteryPecentage() {
	adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_0,ADC_ATTEN_DB_11);
  rawData = adc1_get_raw(ADC1_CHANNEL_0);

	ESP_LOGI(TAG, "ADC rawData: %i", rawData);
  return  (int)((double)rawData / 4096.0 * 3578.0 * 175.0 / 100.0);
}

void taskBattery(void *parameter) {
	TickType_t xLastWakeTime;
 	const TickType_t xFrequency = 10*1000 / portTICK_RATE_MS;
 	xLastWakeTime = xTaskGetTickCount();
	int data = 0;
	while(1) {
		data = 0;
		for(int i = 0; i < 5; ++i) {
				data += getBatteryPecentage();
				vTaskDelay(1000 / portTICK_RATE_MS);
		}
		batteryVoltage = data / 5;
		ESP_LOGI(TAG, "Battery voltage: %i mV", batteryVoltage);
		batteryPercentage = ((double)batteryVoltage - 3700) / 500.0 * 100;
		ESP_LOGI(TAG, "Battery pecentage: %i %%", batteryPercentage);
		vTaskDelayUntil(&xLastWakeTime, xFrequency);
	}
}
void clear_screen() {
	lv_group_del(group);
	lv_obj_clean(screen);
	group = lv_group_create();
	lv_indev_set_group(keypad_indev, group);
	lv_group_set_style_mod_cb(group, style_mod_cb);
}

void drawHomeScreen() {
	home_list = lv_list_create(screen, NULL);
	lv_obj_set_size(home_list, 320, 216);
	lv_list_add(home_list, SYMBOL_AUDIO, "Library", onclick_homelist);
	lv_list_add(home_list, SYMBOL_IMAGE, "Gallery", onclick_homelist);
	lv_list_add(home_list, SYMBOL_SETTINGS, "Settings", onclick_homelist);
	lv_list_add(home_list, SYMBOL_PLAY, "Now Playing", onclick_homelist);
	lv_group_add_obj(group, home_list);
	lv_group_focus_obj(home_list);
}

void drawLibrary() {
	library_list = lv_list_create(screen, NULL);
	lv_obj_set_size(library_list, 320, 216);
	lv_group_add_obj(group, library_list);
	lv_list_set_anim_time(library_list, 0);
	for(int i = 0; i < playlist_len; ++i) {
		lv_list_add(library_list, SYMBOL_AUDIO, playlist_array[i].title, onclick_library);
	}
	lv_group_focus_obj(library_list);
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
	lv_label_set_text(now_playing, playerState.title);

	author = lv_label_create(info_obj, NULL);
	lv_label_set_style(author, &title_20);
	lv_obj_set_pos(author, 0, 30);
	lv_label_set_text(author, playerState.author);
	lv_label_set_long_mode(author, LV_LABEL_LONG_SCROLL);

	album = lv_label_create(info_obj, author);
	lv_obj_set_pos(album, 0, 60);
	lv_label_set_text(author, playerState.album);
	lv_label_set_long_mode(album, LV_LABEL_LONG_SCROLL);

	sample_info = lv_label_create(info_obj, author);
	lv_obj_set_pos(sample_info, 0, 90);
	lv_label_set_text(sample_info, "44100Hz 16-bit");

	time_bar = lv_bar_create(screen, NULL);
	lv_obj_set_size(time_bar, 290, 10);
	lv_obj_set_pos(time_bar, 15, 187);
	lv_bar_set_value(time_bar, 0);

	time_text = lv_label_create(screen, author);
	lv_obj_set_pos(time_text, 15, 158);
	lv_label_set_text(time_text, "0:00 / 0:00");
}

void drawStatusBar() {
	status_bar = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_size(status_bar, 320, 24);
	lv_obj_set_style(status_bar, &status_bar_style);

	battery_icon = lv_label_create(status_bar, NULL);
	lv_label_set_style(battery_icon, &status_bar_icon_style);
	lv_obj_set_pos(battery_icon, 5, 2);

	// battery_text = lv_label_create(status_bar, NULL);
	// lv_label_set_style(battery_text, &status_bar_icon_style);
	// lv_obj_set_pos(battery_text, 35, 3);
	// lv_label_set_text(battery_text, "100%");

	playing_icon = lv_label_create(status_bar, NULL);
	lv_label_set_style(playing_icon, &status_bar_icon_style);
	lv_obj_set_pos(playing_icon, 295, 2);
	lv_label_set_text(playing_icon, SYMBOL_STOP);

	volume =lv_label_create(status_bar, NULL);
	lv_label_set_style(volume, &status_bar_icon_style);
	lv_obj_set_pos(volume, 40, 2);

	wifi_icon = lv_label_create(status_bar, NULL);
	lv_label_set_style(wifi_icon, &status_bar_icon_style);
	lv_obj_set_pos(wifi_icon, 265, 2);
}

void taskUI_Char(void *parameter) {
	th = lv_theme_material_init(210, NULL);
	lv_theme_set_current(th);

	style_init();
	drawStatusBar();

	screen = lv_obj_create(lv_scr_act(), NULL);
	lv_obj_set_size(screen, 320, 216);
	lv_obj_set_pos(screen, 0, 24);
	lv_obj_set_style(screen, &status_bar_style);

	group = lv_group_create();
	lv_indev_set_group(keypad_indev, group);
	lv_group_set_style_mod_cb(group, style_mod_cb);
	drawHomeScreen();

	lv_indev_state_t l_state = LV_INDEV_STATE_REL;
	while(1) {
		if(batteryPercentage == 0) lv_label_set_text(battery_icon, SYMBOL_BATTERY_EMPTY);
		else if(batteryPercentage <=25) lv_label_set_text(battery_icon, SYMBOL_BATTERY_1);
		else if(batteryPercentage > 25 && batteryPercentage <= 50) lv_label_set_text(battery_icon, SYMBOL_BATTERY_2);
		else if(batteryPercentage > 50 && batteryPercentage <= 75) lv_label_set_text(battery_icon, SYMBOL_BATTERY_3);
		else lv_label_set_text(battery_icon, SYMBOL_BATTERY_FULL);

		char tmp_str[32];
		// memset(tmp_str, 0, sizeof(tmp_str));
		// sprintf(tmp_str, "%i%%", batteryPercentage);
		// lv_label_set_text(battery_text, tmp_str);

		memset(tmp_str, 0, sizeof(tmp_str));
		int v = getVolumePercentage();
		if(v == 0) sprintf(tmp_str, "%s0%%", SYMBOL_MUTE);
		else sprintf(tmp_str, "%s%i%%", SYMBOL_VOLUME_MAX, v);
		lv_label_set_text(volume, tmp_str);

		if(playerState.started == false) lv_label_set_text(playing_icon, SYMBOL_STOP);
		else lv_label_set_text(playing_icon, isPaused() ? SYMBOL_PAUSE : SYMBOL_PLAY);
		lv_label_set_text(wifi_icon, wifi_connected ? SYMBOL_WIFI : " ");

		lv_indev_data_t i_data;
		lv_indev_read(keypad_indev, &i_data);
		switch(menuID) {
			case 0: // home screen
			break;

			case 1: //Library
				if(l_state == LV_INDEV_STATE_PR && i_data.state == LV_INDEV_STATE_REL) {
					switch (i_data.key) {
						case LV_GROUP_KEY_ESC:
							menuID = 0;
							clear_screen();
							drawHomeScreen();
						break;
						case LV_GROUP_KEY_RIGHT:
							for(int i = 0; i < 3; ++i)
								lv_group_send_data(group, LV_GROUP_KEY_RIGHT);
						break;
						case LV_GROUP_KEY_LEFT:
							for(int i = 0; i < 3; ++i)
								lv_group_send_data(group, LV_GROUP_KEY_LEFT);
						break;
					}
				}
			break;

			case 2:

			break;

			case 3:

			break;

			case 4: //now playing
				memset(tmp_str, 0, sizeof(tmp_str));
				sprintf(tmp_str, "%i:%02i / %i:%02i", playerState.currentTime / 60
												, playerState.currentTime % 60
												, playerState.totalTime / 60
												, playerState.totalTime & 60);
				lv_label_set_text(time_text, tmp_str);
				if(playerState.totalTime != 0)
					lv_bar_set_value(time_bar, (int)((double)playerState.currentTime / (double)playerState.totalTime * 100));
				else lv_bar_set_value(time_bar, 0);

				memset(tmp_str, 0, sizeof(tmp_str));
				sprintf(tmp_str, "%iHz %i-Bit", playerState.sampleRate, playerState.bitsPerSample);
				lv_label_set_text(sample_info, tmp_str);

				if(playerState.musicChanged == true) {
					lv_label_set_text(now_playing, playerState.title);
					lv_label_set_text(author, playerState.author);
					lv_label_set_text(album, playerState.album);
					playerState.musicChanged = false;
				}

				if(l_state == LV_INDEV_STATE_PR && i_data.state == LV_INDEV_STATE_REL) {
					switch (i_data.key) {
						case LV_GROUP_KEY_UP:
							if(getVolumePercentage() >= 90) setVolume(100);
							else setVolume(getVolumePercentage() + 10);
						break;
						case LV_GROUP_KEY_DOWN:
							if(getVolumePercentage() <= 10) setVolume(0);
							else setVolume(getVolumePercentage() - 10);
						break;
						case LV_GROUP_KEY_LEFT:
							playerState.started = false;
							nowplay_offset--;
							if(nowplay_offset < 0) nowplay_offset = playlist_len;
						break;
						case LV_GROUP_KEY_RIGHT:
							playerState.started = false;
							nowplay_offset++;
							if(nowplay_offset > playlist_len) nowplay_offset = 0;
						break;
						case LV_GROUP_KEY_ESC:
							menuID = 0;
							clear_screen();
							drawHomeScreen();
						break;
					}
   			}
			break;
			default:break;
		}
		l_state = i_data.state;
		vTaskDelay(50 / portTICK_RATE_MS);
	}
}

void wifi_set_stat(bool c) {
	wifi_connected = c;
}

static lv_res_t onclick_homelist(lv_obj_t * list_btn) {
	char *text = lv_list_get_btn_text(list_btn);
	if(strcmp(text, "Library") == 0) {
		clear_screen();
		drawLibrary();
		menuID = 1;
	} else if(strcmp(text, "Now Playing") == 0) {
		clear_screen();
		drawPlaying();
		menuID = 4;
	} else if(strcmp(text, "Settings") == 0) {
		clear_screen();
		drawHomeScreen();
	} else if(strcmp(text, "Gallery") == 0) {
		clear_screen();
		drawHomeScreen();
	}
  return LV_RES_OK; /*Return OK because the list is not deleted*/
}

static lv_res_t onclick_library(lv_obj_t * list_btn) {
	char *fn = lv_list_get_btn_text(list_btn);
	ESP_LOGI(TAG, "Now playing: %s", fn);
	for(int i = 0; i < playlist_len; ++i) {
		if(strcmp(playlist_array[i].title, fn) == 0){
			ESP_LOGI(TAG, "nowplay_offset: %i", nowplay_offset);
			nowplay_offset = i;
			break;
		}
	}
	player_pause(false);
	playerState.started = false;

	return LV_RES_OK;
}
