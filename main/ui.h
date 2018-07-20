#ifndef _UI_H_
#define _UI_H_

#ifndef min
  #define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

void lockSpi(bool lock);
void getBatteryPecentage();
void taskBattery(void *parameter);
void taskUI_Char(void *parameter);
void wifi_set_stat(bool c);
// void drawStatusBar();
// void drawList(int x, int y, int w, int h, int count);
#endif