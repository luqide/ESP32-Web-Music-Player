#include "i2s_dac.h"

static const char *TAG = "I2S_DAC";
headerState_t state = HEADER_RIFF;
wavProperties_t wavProps;
playlist_t playlist = {
  .priv = NULL
};

playerState_t playerState = {
  .paused = true,
  .totalTime = 0,
  .currentTime = 0,
  .nowPlaying = "",
  .playMode = PLAYMODE_PLAYLIST,
  .volume = 50,
  .volumeMultiplier = pow(10, -25 / 20.0),
  .musicType = NONE
};


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
  if(wavFile != NULL) {
    int fSize;
    size_t n;
    long count;
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
//          ESP_LOGI(TAG, "RAM left %d", esp_get_free_heap_size());
          if(playerState.paused == true) {
            ESP_LOGI(TAG, "Paused.");
            dac_mute(true);
          }
          while(playerState.paused == true);
          dac_mute(false);

          int bytes = wavProps.bitsPerSample / 8 * 2 * 768;
          int16_t *data = malloc(bytes);
          n = readNBytes(wavFile, data, bytes);
          for(int i = 0; i < bytes / 2; i ++) {
            data[i] *= playerState.volumeMultiplier;
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
  playerState.volume = vol;
  playerState.volumeMultiplier = pow(10, (MIN_VOL_OFFSET + vol / 2) / 20.0);
}

esp_err_t i2s_init() {
  gpio_set_direction(PIN_PD, GPIO_MODE_OUTPUT);
  i2s_driver_install((i2s_port_t)i2s_num, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)i2s_num, &pin_config);
  REG_WRITE(PIN_CTRL, 0xFFFFFFF0);
  PIN_FUNC_SELECT(GPIO_PIN_REG_0, 1);
  memset(playerState.nowPlaying, 0, sizeof(playerState.nowPlaying));
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

void parseMusicType() {
  char typeName[8];
  int len = strlen(playerState.nowPlaying);
  if(len < 5) {
    playerState.musicType = NONE;
    return;
  }
  memset(typeName, 0, sizeof(typeName));
  for(int i = 0; i < 4; ++i)
    typeName[i] = playerState.nowPlaying[len  - (4 - i)];

//  printf("%s\n", typeName);
  if((!strcmp(typeName, ".wav")) | (!strcmp(typeName, ".WAV")))
    playerState.musicType = WAV;
  else if((!strcmp(typeName, ".mp3")) | (!strcmp(typeName, ".MP3")))
    playerState.musicType = MP3;
  else if((!strcmp(typeName, ".ape")) | (!strcmp(typeName, ".APE")))
    playerState.musicType = APE;
  else if((!strcmp(typeName, "flac")) | (!strcmp(typeName, ".flac")))
    playerState.musicType = FLAC;
  else playerState.musicType = NONE;
}

void setNowPlaying(char *str) {
  strcpy(playerState.nowPlaying, str);
}

int getMusicType() {
  return playerState.musicType;
}

bool isPaused() {
  return playerState.paused;
}

FILE* musicFileOpen() {
  return fopen(playerState.nowPlaying, "rb");
}