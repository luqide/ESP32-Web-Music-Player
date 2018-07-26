#ifndef _UI_H_
#define _UI_H_

#ifndef min
  #define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

extern lv_obj_t *img_cover, *info_obj, *now_playing, *author, *album, *sample_info, *time_text, *time_bar, *playmode;

int getBatteryPecentage();
void taskBattery(void *parameter);
void taskUI_Char(void *parameter);
void wifi_set_stat(bool c);
// void drawStatusBar();
// void drawList(int x, int y, int w, int h, int count);
#endif
