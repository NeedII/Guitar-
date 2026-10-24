/* Motor de áudio do Guitarra no PC — WASAPI via miniaudio */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#include "miniaudio.h"
#include "audio.h"
#include "dsp.h"
#include "recorder.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAXDEV 64

volatile int pChan = 0;

static ma_context mctx;
static int ctxOk = 0;
static ma_device dev;
static int devOk = 0, exclOk = 0;

static ma_device_id capIds[MAXDEV], pbIds[MAXDEV];
static char capNames[MAXDEV][256], pbNames[MAXDEV][256];
static int capDef[MAXDEV], pbDef[MAXDEV];
static int nCap = 0, nPb = 0;

static volatile float gPeakIn = 0.0f, gPeakOut = 0.0f;
static volatile int   gStopped = 0;
static float monoIn[2048], monoOut[2048];

static void dataCallback(ma_device* d, void* output, const void* input, ma_uint32 frames)
{
    const float* in = (const float*)input;
    float* out = (float*)output;
    ma_uint32 inCh = d->capture.channels, outCh = d->playback.channels;
    int ch = pChan; if (ch >= (int)inCh) ch = (int)inCh - 1; if (ch < 0) ch = 0;
    float pi = gPeakIn, po = gPeakOut;
    ma_uint32 done = 0;
    while (done < frames) {
        ma_uint32 n = frames - done; if (n > 2048) n = 2048;
        for (ma_uint32 i = 0; i < n; i++) monoIn[i] = in ? in[(done + i) * inCh + ch] : 0.0f;
        dsp_process(monoIn, monoOut, (int)n, &pi, &po);
        rec_push(monoOut, monoIn, (int)n);
        rec_play_mix(monoOut, (int)n);
        for (ma_uint32 i = 0; i < n; i++) {
            float v = monoOut[i];
            if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
            for (ma_uint32 c = 0; c < outCh; c++) out[(done + i) * outCh + c] = v;
        }
        done += n;
    }
    gPeakIn = pi; gPeakOut = po;
}

static void notifyCallback(const ma_device_notification* n)
{
    if (n->type == ma_device_notification_type_stopped) gStopped = 1;
}

int audio_init(void)
{
    ma_backend backends[] = { ma_backend_wasapi };
    ctxOk = ma_context_init(backends, 1, NULL, &mctx) == MA_SUCCESS;
    if (ctxOk) audio_enumerate();
    return ctxOk;
}

void audio_shutdown(void)
{
    audio_stop();
    if (ctxOk) ma_context_uninit(&mctx);
    ctxOk = 0;
}

void audio_enumerate(void)
{
    ma_device_info *pb, *cap; ma_uint32 npb, ncap;
    nCap = nPb = 0;
    if (!ctxOk || ma_context_get_devices(&mctx, &pb, &npb, &cap, &ncap) != MA_SUCCESS) return;
    for (ma_uint32 i = 0; i < ncap && nCap < MAXDEV; i++) {
        capIds[nCap] = cap[i].id; strncpy(capNames[nCap], cap[i].name, 255); capNames[nCap][255] = 0;
        capDef[nCap] = cap[i].isDefault; nCap++;
    }
    for (ma_uint32 i = 0; i < npb && nPb < MAXDEV; i++) {
        pbIds[nPb] = pb[i].id; strncpy(pbNames[nPb], pb[i].name, 255); pbNames[nPb][255] = 0;
        pbDef[nPb] = pb[i].isDefault; nPb++;
    }
}

int audio_capture_count(void) { return nCap; }
const char* audio_capture_name(int i) { return (i >= 0 && i < nCap) ? capNames[i] : ""; }
int audio_capture_is_default(int i) { return (i >= 0 && i < nCap) ? capDef[i] : 0; }
int audio_playback_count(void) { return nPb; }
const char* audio_playback_name(int i) { return (i >= 0 && i < nPb) ? pbNames[i] : ""; }
int audio_playback_is_default(int i) { return (i >= 0 && i < nPb) ? pbDef[i] : 0; }

int audio_find_capture(const char* name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < nCap; i++) if (!strcmp(capNames[i], name)) return i;
    return -1;
}

int audio_find_playback(const char* name)
{
    if (!name || !name[0]) return -1;
    for (int i = 0; i < nPb; i++) if (!strcmp(pbNames[i], name)) return i;
    return -1;
}

static ma_result tryInit(int inIdx, int outIdx, ma_share_mode mode)
{
    ma_device_config c = ma_device_config_init(ma_device_type_duplex);
    c.capture.pDeviceID  = (inIdx >= 0 && inIdx < nCap) ? &capIds[inIdx] : NULL;
    c.capture.format     = ma_format_f32;
    c.capture.channels   = 0;
    c.capture.shareMode  = mode;
    c.playback.pDeviceID = (outIdx >= 0 && outIdx < nPb) ? &pbIds[outIdx] : NULL;
    c.playback.format    = ma_format_f32;
    c.playback.channels  = 2;
    c.playback.shareMode = mode;
    c.sampleRate         = 0;
    c.performanceProfile = ma_performance_profile_low_latency;
    if (mode == ma_share_mode_exclusive) c.periodSizeInMilliseconds = 3;
    c.dataCallback = dataCallback;
    c.notificationCallback = notifyCallback;
    ma_result r = ma_device_init(&mctx, &c, &dev);
    if (r != MA_SUCCESS) return r;
    dsp_init((float)dev.sampleRate);
    r = ma_device_start(&dev);
    if (r != MA_SUCCESS) { ma_device_uninit(&dev); return r; }
    devOk = 1;
    return MA_SUCCESS;
}

int audio_start(int inIdx, int outIdx, int exclusive, char* err, int errLen)
{
    audio_stop();
    if (err && errLen) err[0] = 0;
    if (!ctxOk) { if (err) snprintf(err, errLen, "Não foi possível acessar o sistema de áudio do Windows."); return 0; }
    gStopped = 0; exclOk = 0;

    ma_result r = tryInit(inIdx, outIdx, exclusive ? ma_share_mode_exclusive : ma_share_mode_shared);
    if (r == MA_SUCCESS && exclusive) exclOk = 1;
    if (r != MA_SUCCESS && exclusive) r = tryInit(inIdx, outIdx, ma_share_mode_shared);

    if (r != MA_SUCCESS) {
        if (!err) return 0;
        if (r == MA_ACCESS_DENIED)
            snprintf(err, errLen, "O Windows bloqueou o microfone. Ative em Configurações > Privacidade > Microfone, inclusive para aplicativos da área de trabalho.");
        else if (r == MA_BUSY || r == MA_DEVICE_NOT_STARTED)
            snprintf(err, errLen, "Outro programa está usando o aparelho de áudio. Feche-o e ligue de novo.");
        else
            snprintf(err, errLen, "Não foi possível ligar o áudio (%s). Escolha outros aparelhos na aba Áudio.", ma_result_description(r));
        return 0;
    }
    return 1;
}

void audio_stop(void)
{
    if (devOk) { ma_device_uninit(&dev); devOk = 0; }
    exclOk = 0; gPeakIn = 0; gPeakOut = 0;
}

int audio_running(void) { return devOk; }
int audio_exclusive_active(void) { return devOk && exclOk; }
int audio_lost(void) { return devOk && gStopped; }

void audio_peaks(float* in, float* out)
{
    *in = gPeakIn; *out = gPeakOut;
    gPeakIn = 0; gPeakOut = 0;
}

void audio_info(unsigned* rate, float* bufferMs, int* inChannels)
{
    *rate = 0; *bufferMs = 0; *inChannels = 0;
    if (!devOk) return;
    float ms = 0;
    if (dev.capture.internalSampleRate)
        ms += 1000.0f * dev.capture.internalPeriodSizeInFrames * dev.capture.internalPeriods / dev.capture.internalSampleRate;
    if (dev.playback.internalSampleRate)
        ms += 1000.0f * dev.playback.internalPeriodSizeInFrames * dev.playback.internalPeriods / dev.playback.internalSampleRate;
    *rate = dev.sampleRate; *bufferMs = ms; *inChannels = (int)dev.capture.channels;
}

void app_dir(char* buf, int len)
{
    GetModuleFileNameA(NULL, buf, (DWORD)len);
    char* slash = strrchr(buf, '\\');
    if (slash) slash[1] = 0; else buf[0] = 0;
}
