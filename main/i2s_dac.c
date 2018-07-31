#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include "sys/stat.h"
#include "driver/i2s.h"
#include "soc/io_mux_reg.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include "mp3dec.h"
#include "../lvgl/lvgl.h"
#include "sd_card.h"

#include "ui.h"
#include "i2s_dac.h"


static const char *TAG = "CODEC";
headerState_t state = HEADER_RIFF;
wavProperties_t wavProps;
int playlist_len, nowplay_offset;
playlist_node_t *playlist_array;

//i2s configuration
i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100,
    .bits_per_sample = 16,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, // high interrupt priority
    .dma_buf_count = 64,
    .dma_buf_len = 64,
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
  .started = false,
  .totalTime = 0,
  .currentTime = 0,
  .title = "",
  .author = "",
  .album = "",
  .playMode = PLAYMODE_REPEAT_PLAYLIST,
  .volume = 50,
  .volumeMultiplier = pow(10, -25 / 20.0),
  .musicType = NONE,
  .musicChanged = true
};


size_t read4bytes(FILE *file, uint32_t *chunkId){
  size_t n = fread((uint8_t *)chunkId, sizeof(uint8_t), 4, file);
  return n;
}

size_t readNBytes(FILE *file, void *data, int count){
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
    size_t n, fSize, dOffset = 0;
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
          dOffset = ftell(wavFile);
          playerState.totalTime = (fSize - dOffset) / wavProps.byteRate;
          //set sample rates of i2s to sample rate of wav file
          i2s_set_sample_rates((i2s_port_t)i2s_num, wavProps.sampleRate);
          playerState.sampleRate = wavProps.sampleRate;
          playerState.bitsPerSample = wavProps.bitsPerSample;
        }
        break;
        /* after processing wav file, it is time to process music data */
        case DATA: {
          if(playerState.paused == true) {
            ESP_LOGI(TAG, "Paused.");
            i2s_zero_dma_buffer(0);
            // dac_mute(true);
            while(playerState.paused == true)vTaskDelay(100 / portTICK_RATE_MS);
            ESP_LOGI(TAG, "Continued.");
          }
          if(playerState.started == false) {
            i2s_zero_dma_buffer(0);
            fclose(wavFile);
            return ESP_FAIL;
          }
          playerState.currentTime = (ftell(wavFile) - dOffset) / wavProps.byteRate;

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
  if(vol > 100 ) playerState.volume = 100;
  else if(vol < 0) playerState.volume = 0;
  else playerState.volume = vol;
  playerState.volumeMultiplier = pow(10, (MIN_VOL_OFFSET + vol / 2) / 20.0);
}

esp_err_t i2s_init() {
  gpio_set_direction(PIN_PD, GPIO_MODE_OUTPUT);
  i2s_driver_install((i2s_port_t)i2s_num, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)i2s_num, &pin_config);
  REG_WRITE(PIN_CTRL, 0xFFFFFFF0);
  PIN_FUNC_SELECT(GPIO_PIN_REG_0, 1);
  memset(playerState.fileName, 0, sizeof(playerState.fileName));
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
  int len = strlen(playerState.fileName);
  if(len < 5) {
    playerState.musicType = NONE;
    return;
  }
  memset(typeName, 0, sizeof(typeName));
  for(int i = 0; i < 4; ++i)
    typeName[i] = playerState.fileName[len  - (4 - i)];

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
  strcpy(playerState.fileName, str);
}

int getMusicType() {
  return playerState.musicType;
}

bool isPaused() {
  return playerState.paused;
}

FILE* musicFileOpen() {
  return fopen(playerState.fileName, "rb");
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
    fseek(mp3File, 0, SEEK_END);
    size_t fileSize = ftell(mp3File);
    rewind(mp3File);

    playerState.currentTime = 0;

    int samplerate = 0;
    i2s_zero_dma_buffer(0);
    char tag[10];
    int tag_len = 0;
    int read_bytes = fread(tag, 1, 10, mp3File);
    if(read_bytes == 10) {
      if (memcmp(tag,"ID3",3) == 0) {
        tag_len = ((tag[6] & 0x7F)<< 21)|((tag[7] & 0x7F) << 14) | ((tag[8] & 0x7F) << 7) | (tag[9] & 0x7F);
          // ESP_LOGI(TAG,"tag_len: %d %x %x %x %x", tag_len,tag[6],tag[7],tag[8],tag[9]);
       }
      else {
        ESP_LOGE(TAG, "Not an mp3 file.");
        return;
      }
     }
     fseek(mp3File, tag_len, SEEK_SET);
     int bytesLeft = 0;
     unsigned char* readPtr = readBuf;
     while (1) {
        if(playerState.paused == true) {
          ESP_LOGI(TAG, "Paused.");
          i2s_zero_dma_buffer(0);
          while(playerState.paused == true) vTaskDelay(100 / portTICK_RATE_MS);
          ESP_LOGI(TAG, "Continued.");
        }
        if(playerState.started == false) {
          i2s_zero_dma_buffer(0);
          fclose(mp3File);
          return;
        }
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
          playerState.currentTime = (ftell(mp3File) - tag_len) * 8 / mp3FrameInfo.bitrate;
          if(samplerate!=mp3FrameInfo.samprate)
          {
              samplerate=mp3FrameInfo.samprate;
              i2s_set_clk(0,samplerate,16,mp3FrameInfo.nChans);
              playerState.sampleRate = mp3FrameInfo.samprate;
              playerState.bitsPerSample = 16;
              playerState.totalTime = (fileSize - tag_len) * 8 / mp3FrameInfo.bitrate;
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

void taskPlay(void *parameter) {
  playlist_len = 0;
  nowplay_offset = 0;
  playlist_array = heap_caps_malloc(sizeof(playlist_node_t) * 256, MALLOC_CAP_SPIRAM);
  memset(playlist_array, 0, sizeof(playlist_node_t) * 256);
  scan_music_file("/sdcard/", 0, 3);
  ESP_LOGI(TAG, "Music scanning completed.");
  while(1) {
    playerState.musicChanged = true;
    setNowPlaying(playlist_array[nowplay_offset].filePath);
    strcpy(playerState.title, playlist_array[nowplay_offset].title);
    playerState.filePtr = fopen(playerState.fileName, "rb");
    memset(playerState.author, 0, sizeof(playerState.author));
    memset(playerState.album, 0, sizeof(playerState.album));
    parse_mp3_info(playerState.filePtr, NULL, playerState.author, playerState.album);
    if(playerState.filePtr != NULL) {
      parseMusicType();
      switch(playerState.musicType) {
        case WAV:
          wavPlay(playerState.filePtr);
        break;
        case MP3:
          mp3Play(playerState.filePtr);
        break;
        default:break;
      }
      fclose(playerState.filePtr);
    }
    playerState.totalTime = 0;
    playerState.currentTime = 0;
    if(playerState.started != false) {
      switch(playerState.playMode) {
        case PLAYMODE_RANDOM:
          srand(time(NULL));
          nowplay_offset = rand() % (playlist_len + 1);
        break;
        case PLAYMODE_REPEAT_PLAYLIST:
          nowplay_offset++;
          if(nowplay_offset > playlist_len) nowplay_offset = 0;
        break;
        case PLAYMODE_REPEAT:break;
        default:break;
      }
    } else {
      playerState.started = true;
    }
    vTaskDelay(800 / portTICK_RATE_MS);
  }
}

static int check_music_file(char* filename) {
  char l[5];
  memset(l, 0, sizeof(l));
  int len = strlen(filename);
  for(int i = 0; i < 4; ++i) {
    l[i] = filename[len - (4 - i)];
  }
  if(strcmp(l,".mp3") == 0 || strcmp(l, ".MP3") == 0)
    return 1;
  else if(strcmp(l, ".wav") == 0 || strcmp(l, ".WAV") == 0) return 2;

  return 0;
}

int scan_music_file(const char *basePath, int dep_cur, const int dep) {
  if(dep == dep_cur) return 0;
  DIR *dir_p;
  struct dirent *dirent_p;
  FILE *file_p;
  char path[256];
  int type = 0;
  memset(path, 0, sizeof(path));
  strcpy(path, basePath);

  dir_p = opendir(basePath);
  if(dir_p == NULL) {
    ESP_LOGE(TAG, "Failed to opendir: %s", basePath);
    return -1;
  }
  dirent_p = readdir(dir_p);
  while(dirent_p != NULL) {
    if(strcmp(dirent_p->d_name, ".") == 0 || strcmp(dirent_p->d_name, "..") == 0)
      continue;

    switch(dirent_p->d_type) {
      case 1://file
        strcat(path, dirent_p->d_name);
        type = check_music_file(path);
        if(type != 0) {
          playlist_len++;
          sprintf(playlist_array[playlist_len - 1].filePath, "%s/%s", basePath, dirent_p->d_name);
          file_p = fopen(playlist_array[playlist_len - 1].filePath, "rb");
          if(type == 1)
            parse_mp3_info(file_p, playlist_array[playlist_len - 1].title, NULL, NULL);
          else if(type == 2)
            memcpy(playlist_array[playlist_len - 1].title, playlist_array[playlist_len - 1].filePath, 256);
          fclose(file_p);
        }

        break;
      case 2:
        strcat(path, dirent_p->d_name);
        //strcat(path, "/");
        scan_music_file(path, dep_cur + 1, dep);
        break;
      default:break;
    }
    dirent_p = readdir(dir_p);
    memset(path, 0, sizeof(path));
    strcpy(path, basePath);
  }
  return 0;
}

void parse_mp3_info(FILE *mp3File, char *title, char *author, char *album) {
    if(mp3File == NULL) return;
    rewind(mp3File);
    int tag_len = 0;
    char tag[10];
    int read_bytes = fread(tag, 1, 10, mp3File);
    if(read_bytes == 10) {
      if (memcmp(tag,"ID3",3) == 0) {
        tag_len = ((tag[6] & 0x7F)<< 21)|((tag[7] & 0x7F) << 14) | ((tag[8] & 0x7F) << 7) | (tag[9] & 0x7F);
        while(ftell(mp3File) <= tag_len) {
          read_bytes = fread(tag, 1, 10, mp3File);
          int frame_len = 0;
          if(read_bytes == 10) {
            frame_len = (tag[4] << 24)|(tag[5] << 16)|(tag[6] << 8)|tag[7];
            char FrameID[5] = "", *FrameData = NULL;
            for(int i = 0; i < 4; ++i) FrameID[i] = tag[i];
            if(!strcmp(FrameID, "TIT2")) {
              FrameData = title;
            } else if(!strcmp(FrameID, "TPE1")) {
              FrameData = author;
            } else if(!strcmp(FrameID, "TALB")) {
              FrameData = album;
            } else {
              fseek(mp3File, frame_len, SEEK_CUR);
              continue;
            }
            if(FrameData == NULL) {
              fseek(mp3File, frame_len, SEEK_CUR);
              continue;
            }
            uint8_t code = 0x0;
            fread(&code, 1, 1, mp3File);
            fread(FrameData, 1, frame_len - 1, mp3File);
            switch(code) {
              case 0x00:break;//iso-8859-1
              case 0x01:utf16_to_utf8(FrameData, frame_len - 1);break;
              case 0x02:utf16be_to_utf8(FrameData, frame_len - 1);break;
              case 0x03:break;
              default:break;
            }
          }
        }
      }
      else {
        return;
      }
    }
}

void utf16_to_utf8(char *data, size_t length) {
  if(data[0] != 0xFF || data[1] != 0xFE) {
    ESP_LOGE(TAG, "Not utf16");
    return;
  }
  int len = length,oOffset = 0;
  uint16_t tmp[256];
  uint32_t unicode = 0;
  memset(tmp, 0, sizeof(tmp));
  for(int i = 2; i < len; i += 2) {
    tmp[(i - 2) / 2] = ((uint16_t)data[i + 1] << 8) | data[i];
  }
  len = (len - 2) / 2;
  memset(data, 0, sizeof(char) * length);
  for(int i = 0; i < len; ++i) {
    if(tmp[i] < 0xD800 || tmp[i] > 0xDFFF) unicode = tmp[i];
    else if(tmp[i] >= 0xD800 && tmp[i] <= 0xDBFF
            && i != len - 1 && tmp[i+1] >= 0xDC00
            && tmp[i+1] <= 0xDFFF) {
      unicode = ((((tmp[i] - 0xD800) & 0x3FF) << 10) | ((tmp[i+1] - 0xDC00) & 0x3FF)) + 0x10000;
      i++;
    } else {
      continue;
    }

    if(unicode <= 0x7F) {
      data[oOffset] = unicode & 0x7F;
      oOffset++;
    } else if(unicode <= 0x7FF) {
      data[oOffset] = ((unicode & 0x7C0) >> 6) | 0xC0;
      data[oOffset + 1] = (unicode & 0x3F) | 0x80;
      oOffset += 2;
    } else if(unicode <= 0xFFFF) {
      data[oOffset] = ((unicode & 0xF000) >> 12) | 0xE0;
      data[oOffset + 1] = ((unicode & 0xFC0) >> 6) | 0x80;
      data[oOffset + 2] = (unicode & 0x3F) | 0x80;
      oOffset += 3;
    } else {
      data[oOffset] = ((unicode & 0x1C0000) >> 18) | 0xF0;
      data[oOffset + 1] = ((unicode & 0x3F000) >> 12) | 0x80;
      data[oOffset + 2] = ((unicode & 0xFC0) >> 6) | 0x80;
      data[oOffset + 3] = (unicode & 0x3F) | 0x80;
      oOffset += 4;
    }

  }
}

void utf16be_to_utf8(char *data, size_t length) {
  if(data[0] != 0xFE || data[1] != 0xFF) {
    ESP_LOGE(TAG, "Not utf16be");
    return;
  }
  int len = length,oOffset = 0;
  uint16_t tmp[256];
  uint32_t unicode = 0;
  memset(tmp, 0, sizeof(tmp));
  for(int i = 2; i < len; i += 2) {
    tmp[(i - 2) / 2] = ((uint16_t)data[i] << 8) | data[i + 1];
  }
  len = (len - 2) / 2;
  memset(data, 0, sizeof(char) * length);
  for(int i = 0; i < len; ++i) {
    if(tmp[i] < 0xD800 || tmp[i] > 0xDFFF) unicode = tmp[i];
    else if(tmp[i] >= 0xD800 && tmp[i] <= 0xDBFF
            && i != len - 1 && tmp[i+1] >= 0xDC00
            && tmp[i+1] <= 0xDFFF) {
      unicode = ((((tmp[i] - 0xD800) & 0x3FF) << 10) | ((tmp[i+1] - 0xDC00) & 0x3FF)) + 0x10000;
      i++;
    } else {
      continue;
    }

    if(unicode <= 0x7F) {
      data[oOffset] = unicode & 0x7F;
      oOffset++;
    } else if(unicode <= 0x7FF) {
      data[oOffset] = ((unicode & 0x7C0) >> 6) | 0xC0;
      data[oOffset + 1] = (unicode & 0x3F) | 0x80;
      oOffset += 2;
    } else if(unicode <= 0xFFFF) {
      data[oOffset] = ((unicode & 0xF000) >> 12) | 0xE0;
      data[oOffset + 1] = ((unicode & 0xFC0) >> 6) | 0x80;
      data[oOffset + 2] = (unicode & 0x3F) | 0x80;
      oOffset += 3;
    } else {
      data[oOffset] = ((unicode & 0x1C0000) >> 18) | 0xF0;
      data[oOffset + 1] = ((unicode & 0x3F000) >> 12) | 0x80;
      data[oOffset + 2] = ((unicode & 0xFC0) >> 6) | 0x80;
      data[oOffset + 3] = (unicode & 0x3F) | 0x80;
      oOffset += 4;
    }

  }
}
