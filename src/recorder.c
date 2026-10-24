/* Gravação e reprodução de WAV (Windows) */
#include "recorder.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#define RING (1 << 20)          /* ~21 s a 48 kHz */
#define MAXITEMS 500

static float ringWet[RING], ringDry[RING];
static volatile LONG64 wIdx = 0, rIdx = 0;
static volatile LONG recording = 0, overflow = 0;
static volatile LONG64 pushed = 0;
static int recDI = 0; static unsigned recRate = 48000;
static FILE *fWet = NULL, *fDry = NULL;
static unsigned long long bytesWet = 0;
static DWORD lastHeader = 0;
static CRITICAL_SECTION cs;
static HANDLE worker = NULL;
static volatile LONG quit = 0;
static wchar_t folder[MAX_PATH];
static char lastName[260] = "";

typedef struct { wchar_t path[MAX_PATH]; char name[260]; double secs; int di; FILETIME t; } Item;
static Item items[MAXITEMS];
static int nItems = 0;

/* reprodução */
static float* playBuf = NULL;
static volatile LONG playLen = 0, playing = 0;
static volatile double playPos = 0, playStep = 1, seekReq = -1;
static volatile float playVol = 0.8f;
static unsigned playRate = 48000;
static wchar_t playPath[MAX_PATH] = L"";
static volatile LONG cbCount = 0;

static void toUtf8(const wchar_t* w, char* out, int n) { WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL); }

static void writeHeader(FILE* f, unsigned rate, unsigned long long dataBytes)
{
    unsigned d = dataBytes > 0xFFFFFFD0ull ? 0xFFFFFFD0u : (unsigned)dataBytes;
    unsigned char h[44];
    memcpy(h, "RIFF", 4); unsigned riff = 36 + d; memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    unsigned fmtLen = 16; memcpy(h + 16, &fmtLen, 4);
    unsigned short fmt = 1, ch = 1, align = 3, bits = 24;
    memcpy(h + 20, &fmt, 2); memcpy(h + 22, &ch, 2); memcpy(h + 24, &rate, 4);
    unsigned br = rate * 3; memcpy(h + 28, &br, 4); memcpy(h + 32, &align, 2); memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4); memcpy(h + 40, &d, 4);
    fwrite(h, 1, 44, f);
}

static void patchHeaders(void)
{
    if (fWet) { long p = ftell(fWet); fseek(fWet, 0, SEEK_SET); writeHeader(fWet, recRate, bytesWet); fseek(fWet, p, SEEK_SET); fflush(fWet); }
    if (fDry) { long p = ftell(fDry); fseek(fDry, 0, SEEK_SET); writeHeader(fDry, recRate, bytesWet); fseek(fDry, p, SEEK_SET); fflush(fDry); }
}

static void drain(void)
{
    static unsigned char bw[3 * 8192], bd[3 * 8192];
    for (;;) {
        LONG64 w = wIdx, r = rIdx;
        LONG64 avail = w - r;
        if (avail <= 0) break;
        int n = avail > 8192 ? 8192 : (int)avail;
        for (int i = 0; i < n; i++) {
            int idx = (int)((r + i) & (RING - 1));
            float a = ringWet[idx], b = ringDry[idx];
            if (a > 1) a = 1; else if (a < -1) a = -1;
            if (b > 1) b = 1; else if (b < -1) b = -1;
            int va = (int)lrintf(a * 8388607.0f), vb = (int)lrintf(b * 8388607.0f);
            bw[i * 3] = (unsigned char)va; bw[i * 3 + 1] = (unsigned char)(va >> 8); bw[i * 3 + 2] = (unsigned char)(va >> 16);
            bd[i * 3] = (unsigned char)vb; bd[i * 3 + 1] = (unsigned char)(vb >> 8); bd[i * 3 + 2] = (unsigned char)(vb >> 16);
        }
        MemoryBarrier();
        InterlockedExchange64(&rIdx, r + n);
        if (fWet) fwrite(bw, 3, n, fWet);
        if (fDry) fwrite(bd, 3, n, fDry);
        bytesWet += (unsigned long long)n * 3;
    }
    DWORD now = GetTickCount();
    if (now - lastHeader > 2000) { patchHeaders(); lastHeader = now; }
}

static DWORD WINAPI workerMain(LPVOID p)
{
    (void)p;
    while (!quit) {
        Sleep(15);
        EnterCriticalSection(&cs);
        if (fWet) drain();
        LeaveCriticalSection(&cs);
    }
    return 0;
}

static int ensureFolder(const wchar_t* base)
{
    if (CreateDirectoryW(base, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        wchar_t test[MAX_PATH]; _snwprintf(test, MAX_PATH, L"%ls\\.teste", base);
        FILE* f = _wfopen(test, L"wb");
        if (f) { fclose(f); DeleteFileW(test); return 1; }
    }
    return 0;
}

int rec_init(void)
{
    InitializeCriticalSection(&cs);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t* sl = wcsrchr(exe, L'\\'); if (sl) *sl = 0;
    _snwprintf(folder, MAX_PATH, L"%ls\\Gravações", exe);
    if (!ensureFolder(folder)) {
        wchar_t docs[MAX_PATH];
        if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs) == S_OK) {
            wchar_t app[MAX_PATH]; _snwprintf(app, MAX_PATH, L"%ls\\Guitarra no PC", docs);
            CreateDirectoryW(app, NULL);
            _snwprintf(folder, MAX_PATH, L"%ls\\Gravações", app);
            ensureFolder(folder);
        }
    }
    worker = CreateThread(NULL, 0, workerMain, NULL, 0, NULL);
    rec_list();
    return worker != NULL;
}

void rec_shutdown(void)
{
    rec_stop();
    quit = 1;
    if (worker) { WaitForSingleObject(worker, 1000); CloseHandle(worker); worker = NULL; }
    rec_play_stop(0);
}

int rec_start(int withDI, unsigned sampleRate, char* err, int errLen)
{
    if (recording) return 1;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t base[MAX_PATH], pw[MAX_PATH], pd[MAX_PATH];
    _snwprintf(base, MAX_PATH, L"Gravação %02d-%02d-%04d %02dh%02dm%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);
    _snwprintf(pw, MAX_PATH, L"%ls\\%ls.wav", folder, base);
    _snwprintf(pd, MAX_PATH, L"%ls\\%ls (limpo).wav", folder, base);
    EnterCriticalSection(&cs);
    fWet = _wfopen(pw, L"wb");
    if (!fWet) {
        LeaveCriticalSection(&cs);
        if (err) snprintf(err, errLen, "Não foi possível criar o arquivo da gravação. Confira se a pasta Gravações pode ser alterada.");
        return 0;
    }
    fDry = withDI ? _wfopen(pd, L"wb") : NULL;
    recRate = sampleRate ? sampleRate : 48000; recDI = withDI;
    bytesWet = 0; lastHeader = GetTickCount();
    writeHeader(fWet, recRate, 0);
    if (fDry) writeHeader(fDry, recRate, 0);
    toUtf8(base, lastName, sizeof lastName);
    wIdx = rIdx = 0; pushed = 0; overflow = 0;
    MemoryBarrier();
    recording = 1;
    LeaveCriticalSection(&cs);
    return 1;
}

void rec_stop(void)
{
    if (!recording && !fWet) return;
    recording = 0;
    Sleep(30);
    EnterCriticalSection(&cs);
    if (fWet) {
        drain();
        patchHeaders();
        fclose(fWet); fWet = NULL;
        if (fDry) { fclose(fDry); fDry = NULL; }
    }
    LeaveCriticalSection(&cs);
    rec_list();
}

int rec_active(void) { return recording != 0; }
double rec_seconds(void) { return recRate ? (double)pushed / recRate : 0; }
int rec_overflowed(void) { return overflow != 0; }
const char* rec_last_name(void) { return lastName; }

void rec_push(const float* wet, const float* dry, int n)
{
    if (!recording) return;
    LONG64 w = wIdx, r = rIdx;
    if (RING - (w - r) < n) { overflow = 1; return; }
    for (int i = 0; i < n; i++) {
        int idx = (int)((w + i) & (RING - 1));
        ringWet[idx] = wet[i]; ringDry[idx] = dry[i];
    }
    MemoryBarrier();
    InterlockedExchange64(&wIdx, w + n);
    pushed += n;
}

/* ---------- leitura de WAV ---------- */
static int readWav(const wchar_t* path, float** outBuf, int* outLen, unsigned* outRate, double* secsOnly)
{
    FILE* f = _wfopen(path, L"rb"); if (!f) return 0;
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return 0; }
    unsigned short fmt = 0, ch = 0, bits = 0; unsigned rate = 0; unsigned dataLen = 0; long dataPos = 0;
    for (;;) {
        unsigned char c[8];
        if (fread(c, 1, 8, f) != 8) break;
        unsigned len; memcpy(&len, c + 4, 4);
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char b[40] = { 0 };
            unsigned rd = len < 40 ? len : 40;
            if (fread(b, 1, rd, f) != rd) break;
            memcpy(&fmt, b, 2); memcpy(&ch, b + 2, 2); memcpy(&rate, b + 4, 4); memcpy(&bits, b + 14, 2);
            if (fmt == 0xFFFE && rd >= 26) memcpy(&fmt, b + 24, 2);
            fseek(f, (long)(len - rd + (len & 1)), SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) {
            dataLen = len; dataPos = ftell(f);
            break;
        } else fseek(f, (long)(len + (len & 1)), SEEK_CUR);
    }
    if (!rate || !ch || !bits || !dataPos || (fmt != 1 && fmt != 3)) { fclose(f); return 0; }
    int bps = bits / 8, frameBytes = bps * ch;
    if (dataLen == 0 || dataLen == 0xFFFFFFFFu || dataLen == 0xFFFFFFD0u) {
        fseek(f, 0, SEEK_END); dataLen = (unsigned)(ftell(f) - dataPos);
    }
    int frames = (int)(dataLen / frameBytes);
    if (secsOnly) { *secsOnly = (double)frames / rate; fclose(f); return 1; }
    if (frames > rate * 60 * 30) frames = rate * 60 * 30;
    float* buf = (float*)malloc(sizeof(float) * (frames + 2));
    unsigned char* raw = (unsigned char*)malloc((size_t)frameBytes * 4096);
    if (!buf || !raw) { free(buf); free(raw); fclose(f); return 0; }
    fseek(f, dataPos, SEEK_SET);
    int done = 0;
    while (done < frames) {
        int n = frames - done; if (n > 4096) n = 4096;
        int got = (int)fread(raw, frameBytes, n, f);
        if (got <= 0) break;
        for (int i = 0; i < got; i++) {
            float s = 0;
            for (int c = 0; c < ch; c++) {
                const unsigned char* p = raw + i * frameBytes + c * bps;
                float v = 0;
                if (fmt == 3 && bits == 32) memcpy(&v, p, 4);
                else if (bits == 16) v = (short)(p[0] | (p[1] << 8)) / 32768.0f;
                else if (bits == 24) { int x = p[0] | (p[1] << 8) | (p[2] << 16); if (x & 0x800000) x |= ~0xFFFFFF; v = x / 8388608.0f; }
                else if (bits == 32) { int x; memcpy(&x, p, 4); v = x / 2147483648.0f; }
                else if (bits == 8) v = (p[0] - 128) / 128.0f;
                s += v;
            }
            buf[done + i] = s / ch;
        }
        done += got;
    }
    free(raw); fclose(f);
    buf[done] = 0; buf[done + 1] = 0;
    *outBuf = buf; *outLen = done; *outRate = rate;
    return 1;
}

/* ---------- reprodução ---------- */
int rec_play(int item, unsigned deviceRate, char* err, int errLen)
{
    if (item < 0 || item >= nItems) return 0;
    rec_play_stop(1);
    float* b = NULL; int len = 0; unsigned rate = 0;
    if (!readWav(items[item].path, &b, &len, &rate, NULL) || len < 2) {
        if (err) snprintf(err, errLen, "Não foi possível abrir essa gravação.");
        free(b); return 0;
    }
    playBuf = b; playLen = len; playRate = rate;
    playStep = (double)rate / (deviceRate ? deviceRate : 48000);
    playPos = 0; seekReq = -1;
    wcscpy(playPath, items[item].path);
    MemoryBarrier();
    playing = 1;
    return 1;
}

void rec_play_stop(int audioRunning)
{
    playing = 0;
    MemoryBarrier();
    if (playBuf) {
        if (audioRunning) {
            LONG c0 = cbCount; DWORD t0 = GetTickCount();
            while (cbCount - c0 < 2 && GetTickCount() - t0 < 300) Sleep(5);
        }
        free(playBuf); playBuf = NULL; playLen = 0;
    }
    playPath[0] = 0;
}

int rec_playing(void) { return playing != 0; }
double rec_play_pos(void) { return playRate ? playPos / playRate : 0; }
double rec_play_len(void) { return playRate ? (double)playLen / playRate : 0; }
void rec_play_seek(double secs) { seekReq = secs * playRate; }
void rec_set_play_vol(float v) { playVol = v; }

int rec_play_item(void)
{
    if (!playPath[0]) return -1;
    for (int i = 0; i < nItems; i++) if (!wcscmp(items[i].path, playPath)) return i;
    return -1;
}

void rec_play_mix(float* out, int n)
{
    InterlockedIncrement(&cbCount);
    if (!playing || !playBuf) return;
    double pos = playPos, step = playStep;
    if (seekReq >= 0) { pos = seekReq; seekReq = -1; if (pos > playLen - 2) pos = playLen - 2; if (pos < 0) pos = 0; }
    float v = playVol * playVol;
    int len = playLen;
    for (int i = 0; i < n; i++) {
        int idx = (int)pos;
        if (idx + 1 >= len) { playing = 0; break; }
        float f = (float)(pos - idx);
        out[i] += (playBuf[idx] + (playBuf[idx + 1] - playBuf[idx]) * f) * v;
        pos += step;
    }
    playPos = pos;
}

/* ---------- lista ---------- */
static int cmpItems(const void* a, const void* b)
{
    const Item* x = (const Item*)a; const Item* y = (const Item*)b;
    return CompareFileTime(&y->t, &x->t);
}

int rec_list(void)
{
    wchar_t pat[MAX_PATH]; _snwprintf(pat, MAX_PATH, L"%ls\\*.wav", folder);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    nItems = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        size_t L = wcslen(fd.cFileName);
        if (L > 12 && !_wcsicmp(fd.cFileName + L - 12, L" (limpo).wav")) continue;
        if (nItems >= MAXITEMS) break;
        Item* it = &items[nItems];
        _snwprintf(it->path, MAX_PATH, L"%ls\\%ls", folder, fd.cFileName);
        wchar_t nm[MAX_PATH]; wcsncpy(nm, fd.cFileName, MAX_PATH - 1); nm[MAX_PATH - 1] = 0;
        size_t nl = wcslen(nm); if (nl > 4) nm[nl - 4] = 0;
        toUtf8(nm, it->name, sizeof it->name);
        it->t = fd.ftLastWriteTime;
        it->secs = 0;
        readWav(it->path, NULL, NULL, NULL, &it->secs);
        wchar_t di[MAX_PATH]; _snwprintf(di, MAX_PATH, L"%ls\\%ls (limpo).wav", folder, nm);
        it->di = GetFileAttributesW(di) != INVALID_FILE_ATTRIBUTES;
        nItems++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(items, nItems, sizeof(Item), cmpItems);
    return nItems;
}

int rec_count(void) { return nItems; }
const char* rec_item_name(int i) { return (i >= 0 && i < nItems) ? items[i].name : ""; }
double rec_item_secs(int i) { return (i >= 0 && i < nItems) ? items[i].secs : 0; }
int rec_item_has_di(int i) { return (i >= 0 && i < nItems) ? items[i].di : 0; }

static int trashPath(const wchar_t* p)
{
    wchar_t from[MAX_PATH + 2]; memset(from, 0, sizeof from);
    wcsncpy(from, p, MAX_PATH);
    SHFILEOPSTRUCTW op; memset(&op, 0, sizeof op);
    op.wFunc = FO_DELETE; op.pFrom = from;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0;
}

int rec_trash(int i)
{
    if (i < 0 || i >= nItems) return 0;
    if (!wcscmp(items[i].path, playPath)) rec_play_stop(1);
    int ok = trashPath(items[i].path);
    if (items[i].di) {
        wchar_t di[MAX_PATH]; wcsncpy(di, items[i].path, MAX_PATH - 1); di[MAX_PATH - 1] = 0;
        size_t L = wcslen(di); if (L > 4) { di[L - 4] = 0; wcscat(di, L" (limpo).wav"); trashPath(di); }
    }
    rec_list();
    return ok;
}

void rec_open_folder(void) { ShellExecuteW(NULL, L"open", folder, NULL, NULL, SW_SHOWNORMAL); }
