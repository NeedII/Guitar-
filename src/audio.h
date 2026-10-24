#ifndef GNP_AUDIO_H
#define GNP_AUDIO_H

extern volatile int pChan;   /* canal da entrada (0 = primeiro) */

int  audio_init(void);
void audio_shutdown(void);

void audio_enumerate(void);
int  audio_capture_count(void);
const char* audio_capture_name(int i);
int  audio_capture_is_default(int i);
int  audio_playback_count(void);
const char* audio_playback_name(int i);
int  audio_playback_is_default(int i);
int  audio_find_capture(const char* name);
int  audio_find_playback(const char* name);

/* inIdx/outIdx = -1 usa o padrão do Windows. Retorna 1 se ligou. */
int  audio_start(int inIdx, int outIdx, int exclusive, char* err, int errLen);
void audio_stop(void);
int  audio_running(void);
int  audio_exclusive_active(void);
int  audio_lost(void);
void audio_peaks(float* in, float* out);
void audio_info(unsigned* rate, float* bufferMs, int* inChannels);

void app_dir(char* buf, int len);

#endif
