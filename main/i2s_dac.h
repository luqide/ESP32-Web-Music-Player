#ifndef _I2S_DAC_H_
#define _I2S_DAC_H_

#define MAINBUF_SIZE    1940
#define MIN_VOL_OFFSET -50

#define CCCC(c1, c2, c3, c4)    ((c4 << 24) | (c3 << 16) | (c2 << 8) | c1)
#define PIN_PD 4

#define PLAYMODE_REPEAT 0
#define PLAYMODE_REPEAT_PLAYLIST 1
#define PLAYMODE_PLAYLIST 2
#define PLAYMODE_RANDOM 3

#define MUSICDB_FN_LEN 128
#define MUSICDB_TITLE_LEN 128
typedef enum {
    NONE = 0, WAV, MP3, APE, FLAC
} musicType_t;

extern int playlist_len, nowplay_offset, list_offset;
/* these are data structures to process wav file */
typedef enum {
    HEADER_RIFF, HEADER_FMT, HEADER_DATA, DATA
} headerState_t;

typedef struct {
    bool paused, started;
    uint16_t totalTime;
    uint16_t currentTime;
    int sampleRate;
    int bitsPerSample;
    char title[128];
    char author[128];
    char album[128];
    char fileName[128];
    FILE *filePtr;
    int playMode;
    int volume; //0 - 100%
    double volumeMultiplier;
    musicType_t musicType;
    bool musicChanged;
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

extern playerState_t playerState;

size_t readNBytes(FILE *file, void *data, int count);
size_t read4bytes(FILE *file, uint32_t *chunkId);
size_t readRiff(FILE *file, wavRiff_t *wavRiff);
size_t readProps(FILE *file, wavProperties_t *wavProps);
esp_err_t wavPlay(FILE *wavFile);
void mp3Play(FILE *mp3File);
void setVolume(int vol);
int getVolumePercentage();
esp_err_t i2s_init();
esp_err_t i2s_deinit();
void dac_mute(bool m);
void player_pause(bool p);
void parseMusicType();
int getMusicType();
void setNowPlaying(char *str);
bool isPaused();
FILE* musicFileOpen();
void taskPlay(void *parameter);

int scan_music_file(const char *basePath, int dep_cur, const int dep, FILE *db);
void parse_mp3_info(FILE *mp3File, char *title, char *author, char *album);
void utf16_to_utf8(char *data, size_t length);
void utf16be_to_utf8(char *data, size_t length);
#endif
