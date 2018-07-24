#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "dirent.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

esp_err_t sdmmc_mount(sdmmc_card_t *card);
esp_err_t sdmmc_unmount();
bool sdmmc_is_valid();

#endif
