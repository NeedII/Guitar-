#ifndef GNP_RECORDER_H
#define GNP_RECORDER_H

int  rec_init(void);
void rec_shutdown(void);

/* gravação */
int    rec_start(int withDI, unsigned sampleRate, char* err, int errLen);
void   rec_stop(void);
int    rec_active(void);
double rec_seconds(void);
int    rec_overflowed(void);
const char* rec_last_name(void);
void   rec_push(const float* wet, const float* dry, int n);   /* chamado pela thread de áudio */

/* reprodução */
int    rec_play(int item, unsigned deviceRate, char* err, int errLen);
void   rec_play_stop(int audioRunning);
int    rec_playing(void);
double rec_play_pos(void);
double rec_play_len(void);
int    rec_play_item(void);
void   rec_play_seek(double secs);
void   rec_set_play_vol(float v);
void   rec_play_mix(float* out, int n);                        /* chamado pela thread de áudio */

/* lista de arquivos */
int    rec_list(void);
int    rec_count(void);
const char* rec_item_name(int i);
double rec_item_secs(int i);
int    rec_item_has_di(int i);
int    rec_trash(int i);
void   rec_open_folder(void);

#endif
