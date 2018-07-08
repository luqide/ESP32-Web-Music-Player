#ifndef _I2S_DAC_H_
#define _I2S_DAC_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "stdio.h"
#include "driver/i2s.h"
#include "soc/io_mux_reg.h"
#include "esp_log.h"
#include "math.h"

#define MIN_VOL_OFFSET -50

#define CCCC(c1, c2, c3, c4)    ((c4 << 24) | (c3 << 16) | (c2 << 8) | c1)
#define PIN_PD 4

/* these are data structures to process wav file */
typedef enum {
    HEADER_RIFF, HEADER_FMT, HEADER_DATA, DATA
} headerState_t;

typedef struct {
    bool paused;
    uint16_t totalTime;
    uint16_t currentTime;
} playerState_t;

typedef struct {
    uint32_t chunkID;
    uint32_t chunkSize;
    uint32_t format;
} wavRiff_t;

typedef struct {
    uint32_t chunkID;
    uint32_t chunkSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
} wavProperties_t;
/* variables hold file, state of process wav file and wav file properties */

//i2s configuration
static i2s_config_t i2s_config = {
     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
     .sample_rate = 44100,
     .bits_per_sample = 16,
     .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
     .communication_format = I2S_COMM_FORMAT_I2S,
     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // high interrupt priority
     .dma_buf_count = 8,
     .dma_buf_len = 64,   //Interrupt level 1
     .use_apll = true,
//     .fixed_mclk = 11289600
};

static i2s_pin_config_t pin_config = {
    .bck_io_num = 26, //this is BCK pin
    .ws_io_num = 25, // this is LRCK pin
    .data_out_num = 22, // this is DATA output pin
    .data_in_num = -1   //Not used
};

static int i2s_num = 0; // i2s port number

size_t readNBytes(FILE *file, uint8_t *data, int count);
size_t read4bytes(FILE *file, uint32_t *chunkId);
size_t readRiff(FILE *file, wavRiff_t *wavRiff);
size_t readProps(FILE *file, wavProperties_t *wavProps);
esp_err_t wavPlay(FILE *wavFile);
void setVolume(int vol);
esp_err_t i2s_init();
esp_err_t i2s_deinit();
void dac_mute(bool m);
void player_pause(bool p);
#endif
