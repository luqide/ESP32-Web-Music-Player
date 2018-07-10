#define CONFIG_AUDIO_HELIX

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "stdio.h"
#include "stdlib.h"
#include "time.h"
#include "sys/stat.h"
#include "driver/i2s.h"
#include "soc/io_mux_reg.h"
#include "esp_log.h"
#include "math.h"
#include "ui.h"
#include "string.h"

#include "mp3dec.h"

#include "i2s_dac.h"

static const char *TAG = "CODEC";
headerState_t state = HEADER_RIFF;
wavProperties_t wavProps;

//i2s configuration
i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = 16,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // high interrupt priority
    .dma_buf_count = 128,
    .dma_buf_len = 64,   //Interrupt level 1
    .use_apll = true,
//     .fixed_mclk = 11289600
};

i2s_pin_config_t pin_config = {
    .bck_io_num = 26, //this is BCK pin
    .ws_io_num = 25, // this is LRCK pin
    .data_out_num = 22, // this is DATA output pin
    .data_in_num = -1   //Not used
};

int i2s_num = 0; // i2s port number

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
    size_t n, fSize;
    long count;
    fseek(wavFile , 0 , SEEK_END);
    fSize = ftell (wavFile);
    ESP_LOGI(TAG, "Wav play");
    ESP_LOGI(TAG, "File size: %.2f MBytes\n", (double)fSize / 1024.0 / 1024.0);
    state = HEADER_RIFF;
    rewind(wavFile);
    while(ftell(wavFile) != fSize) {
      switch(state){
        case HEADER_RIFF: {
          wavRiff_t wavRiff;
          n = readRiff(wavFile, &wavRiff);
            if(n == 12 && wavRiff.chunkID == CCCC('R', 'I', 'F', 'F') 
                && wavRiff.format == CCCC('W', 'A', 'V', 'E'))
              state = HEADER_FMT;
        }
        break;
        case HEADER_FMT: {
          n = readProps(wavFile, &wavProps);
          if(n == 24)
            state = HEADER_DATA;
          ESP_LOGI(TAG, "SampleRate: %i ByteRate: %i BitsPerSample: %i ", 
            (int)wavProps.sampleRate, 
            (int)wavProps.byteRate, 
            (int)wavProps.bitsPerSample);
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
          if(playerState.paused == true) {
            ESP_LOGI(TAG, "Paused.");
            i2s_zero_dma_buffer(0);
            // dac_mute(true);
            while(playerState.paused == true);
            ESP_LOGI(TAG, "Continued.");
          }
          // dac_mute(false);
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
  fclose(wavFile);
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

int getVolumePercentage() {
  return playerState.volume;
}


void mp3Play(FILE *mp3File)
{
    ESP_LOGI(TAG,"MP3 start decoding");
    HMP3Decoder hMP3Decoder;
    MP3FrameInfo mp3FrameInfo;
    unsigned char *readBuf=malloc(MAINBUF_SIZE);
    if(readBuf==NULL){
      ESP_LOGE(TAG,"ReadBuf malloc failed");
      return;
    }
    int16_t *output=malloc(1153*4);
    if(output==NULL){
      free(readBuf);
      ESP_LOGE(TAG,"OutBuf malloc failed");
    }
    hMP3Decoder = MP3InitDecoder();
    if (hMP3Decoder == 0){
      free(readBuf);
      free(output);
      ESP_LOGE(TAG,"Memory not enough");
    }

    int samplerate = 0;
    i2s_zero_dma_buffer(0);
    char tag[10];
    int tag_len = 0;
    int read_bytes = fread(tag, 1, 10, mp3File);
    if(read_bytes == 10) 
       {
        if (memcmp(tag,"ID3",3) == 0) 
         {
          tag_len = ((tag[6] & 0x7F)<< 21)|((tag[7] & 0x7F) << 14) | ((tag[8] & 0x7F) << 7) | (tag[9] & 0x7F);
            // ESP_LOGI(TAG,"tag_len: %d %x %x %x %x", tag_len,tag[6],tag[7],tag[8],tag[9]);
          fseek(mp3File, tag_len - 10, SEEK_SET);
         }
        else 
         {
            fseek(mp3File, 0, SEEK_SET);
         }
       }
       unsigned char* input = &readBuf[0];
       int bytesLeft = 0;
       int outOfData = 0;
       unsigned char* readPtr = readBuf;
       while (1) {    
         if(playerState.paused == true) {
            ESP_LOGI(TAG, "Paused.");
            //dac_mute(true);
            i2s_zero_dma_buffer(0);
            while(playerState.paused == true);
            ESP_LOGI(TAG, "Continued.");
          }
          //dac_mute(false);
          if (bytesLeft < MAINBUF_SIZE)
          {
              memmove(readBuf, readPtr, bytesLeft);
              int br = fread(readBuf + bytesLeft, 1, MAINBUF_SIZE - bytesLeft, mp3File);
              if ((br == 0)&&(bytesLeft==0)) break;

              bytesLeft = bytesLeft + br;
              readPtr = readBuf;
          }
          int offset = MP3FindSyncWord(readPtr, bytesLeft);
          if (offset < 0)
          {  
               ESP_LOGE(TAG,"MP3FindSyncWord not find");
               bytesLeft=0;
               continue;
          }
          else
          {
            readPtr += offset;                         //data start point
            bytesLeft -= offset;                 //in buffer
            int errs = MP3Decode(hMP3Decoder, &readPtr, &bytesLeft, (short*)output, 0);
            if (errs != 0)
            {
                ESP_LOGE(TAG,"MP3Decode failed ,code is %d ",errs);
                break;
            }
            MP3GetLastFrameInfo(hMP3Decoder, &mp3FrameInfo);   
            if(samplerate!=mp3FrameInfo.samprate)
            {
                samplerate=mp3FrameInfo.samprate;
                //hal_i2s_init(0,samplerate,16,mp3FrameInfo.nChans);
                i2s_set_clk(0,samplerate,16,mp3FrameInfo.nChans);
                //wm8978_samplerate_set(samplerate);
                ESP_LOGI(TAG,"mp3file info---bitrate=%d,layer=%d,nChans=%d,samprate=%d,outputSamps=%d",mp3FrameInfo.bitrate,mp3FrameInfo.layer,mp3FrameInfo.nChans,mp3FrameInfo.samprate,mp3FrameInfo.outputSamps);
            } 
            for(int i = 0; i < mp3FrameInfo.outputSamps; ++i)
              output[i] *= playerState.volumeMultiplier;

            i2s_write(i2s_num,(const char*)output,mp3FrameInfo.outputSamps*2, (size_t*)(&read_bytes), 1000 / portTICK_RATE_MS);
          }
      
      }
    i2s_zero_dma_buffer(0);
    //i2s_driver_uninstall(0);
    MP3FreeDecoder(hMP3Decoder);
    free(readBuf);
    free(output);  
    fclose(mp3File);
 
    ESP_LOGI(TAG,"end mp3 decode ..");
}