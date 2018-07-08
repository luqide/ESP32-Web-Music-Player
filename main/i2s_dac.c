#include "i2s_dac.h"

static const char *TAG = "I2S_DAC";
headerState_t state = HEADER_RIFF;
wavProperties_t wavProps;
playerState_t playerState = {
  .paused = true,
  .totalTime = 0,
  .currentTime = 0,
};
int volume = -25;


size_t read4bytes(FILE *file, uint32_t *chunkId){
  size_t n = fread((uint8_t *)chunkId, sizeof(uint8_t), 4, file);
  return n;
}

size_t readNBytes(FILE *file, uint8_t *data, int count){
  size_t n = fread((uint8_t *)data, sizeof(uint8_t), count, file);
  return n;
}

/* these are function to process wav file */
size_t readRiff(FILE *file, wavRiff_t *wavRiff){
  size_t n = fread((uint8_t *)wavRiff, sizeof(uint8_t), 12, file);
  return n;
}
size_t readProps(FILE *file, wavProperties_t *wavProps){
  size_t n = fread((uint8_t *)wavProps, sizeof(uint8_t), 24, file);
  return n;
}

esp_err_t wavPlay(FILE *wavFile) {
  double multiplier = pow(10, volume / 20.0);

  if(wavFile != NULL) {
    int fSize;
    size_t n;
    fseek(wavFile , 0 , SEEK_END);
    fSize = ftell (wavFile);
    printf("File size: %.2f MBytes\n", (double)fSize / 1024.0 / 1024.0);
    state = HEADER_RIFF;
    rewind(wavFile);
    while(ftell(wavFile) != fSize) {
      switch(state){
        case HEADER_RIFF: {
          wavRiff_t wavRiff;
          n = readRiff(wavFile, &wavRiff);
          if(n == 12){
            if(wavRiff.chunkID == CCCC('R', 'I', 'F', 'F') && wavRiff.format == CCCC('W', 'A', 'V', 'E')){
              state = HEADER_FMT;
              ESP_LOGI(TAG, "HEADER_RIFF");
            }
          }
        }
        break;
        case HEADER_FMT: {
          n = readProps(wavFile, &wavProps);
          if(n == 24){
            state = HEADER_DATA;
          }
          printf("SampleRate: %i\nByteRate: %i\nBitsPerSample: %i\n", (int)wavProps.sampleRate, (int)wavProps.byteRate, (int)wavProps.bitsPerSample);

        }
        break;
        case HEADER_DATA: {
          uint32_t chunkId, chunkSize;
          uint8_t byte;
          while(ftell(wavFile) != fSize) {
            readNBytes(wavFile, &byte, 1);
            if(byte == 'd') {
              fseek(wavFile, -1, SEEK_CUR);
              n = read4bytes(wavFile, &chunkId);
              if(n == 4){
                if(chunkId == CCCC('d', 'a', 't', 'a')){
                  ESP_LOGI(TAG, "HEADER_DATA");
                  break;
                }
              }
            }

          }
          n = read4bytes(wavFile, &chunkSize);
          if(n == 4){
            ESP_LOGI(TAG, "MUSIC DATA");
            state = DATA;
          }
          //set sample rates of i2s to sample rate of wav file
          i2s_set_sample_rates((i2s_port_t)i2s_num, wavProps.sampleRate);
        }
        break;
        /* after processing wav file, it is time to process music data */
        case DATA: {
          if(playerState.paused == true) ESP_LOGI(TAG, "Paused.");
          while(playerState.paused == true);
          int bytes = wavProps.bitsPerSample / 8 * 2 * 100;
          int16_t *data = malloc(bytes);
          n = readNBytes(wavFile, data, bytes);
          for(int i = 0; i < bytes / 2; i ++) {
            data[i] *= multiplier;
          }
          i2s_write(i2s_num, data, bytes, &n, 100);
          free(data);
        }
        break;
      }
    }
  }
  else {
    ESP_LOGE(TAG, "Failed to read wav file.");
    return ESP_FAIL;
  }
  return ESP_OK;
}

void setVolume(int vol) {
  volume = vol;
}

esp_err_t i2s_init() {
  gpio_set_direction(PIN_PD, GPIO_MODE_OUTPUT);
  i2s_driver_install((i2s_port_t)i2s_num, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)i2s_num, &pin_config);
  REG_WRITE(PIN_CTRL, 0xFFFFFFF0);
  PIN_FUNC_SELECT(GPIO_PIN_REG_0, 1);
  return ESP_OK;
}

esp_err_t i2s_deinit() {
  return i2s_driver_uninstall((i2s_port_t)i2s_num);
}

void dac_mute(bool m) {
  if(m == true) {
    gpio_set_level(PIN_PD, 0);
    vTaskDelay(210 / portTICK_RATE_MS);
  }
  else {
    gpio_set_level(PIN_PD, 1);
    vTaskDelay(10 / portTICK_RATE_MS);
  }
}

void player_pause(bool p) {
  playerState.paused = p;
}