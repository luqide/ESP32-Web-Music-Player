#ifndef _UI_H_
#define _UI_H_

void lockSpi(bool lock);
void getBatteryPecentage();
void taskBattery(void *parameter);
void taskUI_Char(void *parameter);

// void drawStatusBar();
// void drawList(int x, int y, int w, int h, int count);
#endif