/* Guitarra no PC — janela gráfica (raylib) */
#include "raylib.h"
#include "rlgl.h"
#include "audio.h"
#include "dsp.h"
#include "rigio.h"
#include "recorder.h"
#include "fonts.h"
#include "icon_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define VW 1200
#define VH 760
#define SS 2
#define SEL_AMP NSLOT

/* ---------- paleta ---------- */
static const Color C_BG     = { 17, 18, 21, 255 };
static const Color C_BAR    = { 26, 27, 31, 255 };
static const Color C_PANEL  = { 30, 32, 36, 255 };
static const Color C_FIELD  = { 22, 23, 27, 255 };
static const Color C_LINE   = { 50, 53, 59, 255 };
static const Color C_HOVER  = { 41, 43, 49, 255 };
static const Color C_TEXT   = { 228, 230, 234, 255 };
static const Color C_MUTED  = { 138, 143, 152, 255 };
static const Color C_ACC    = { 242, 162, 52, 255 };
static const Color C_ACCDK  = { 66, 48, 24, 255 };
static const Color C_OK     = { 98, 196, 108, 255 };
static const Color C_WARN   = { 236, 196, 72, 255 };
static const Color C_BAD    = { 232, 86, 74, 255 };
static const Color C_CREAM  = { 238, 228, 204, 255 };

/* ---------- visual de cada amplificador ---------- */
typedef struct {
    int tweed;            /* corpo em tweed */
    Color grille, panel, ink, piping, logo;
    int blackKnobs, sansLogo;
} AmpLook;
static const AmpLook LOOKS[NAMP] = {
    /* Americano */ { 0, { 214, 220, 232, 255 }, { 70, 70, 76, 255 },  { 232, 232, 236, 255 }, { 206, 208, 214, 255 }, { 236, 238, 244, 255 }, 1, 0 },
    /* Tweed     */ { 1, { 196, 118, 92, 255 },  { 206, 206, 212, 255 }, { 40, 30, 22, 255 },   { 236, 220, 186, 255 }, { 246, 232, 200, 255 }, 0, 0 },
    /* Britânico */ { 0, { 238, 210, 182, 255 }, { 222, 184, 112, 255 }, { 54, 40, 20, 255 },   { 238, 228, 204, 255 }, { 238, 228, 204, 255 }, 0, 0 },
    /* Hot-Rod   */ { 0, { 96, 94, 96, 255 },    { 222, 184, 112, 255 }, { 54, 40, 20, 255 },   { 214, 178, 104, 255 }, { 232, 196, 120, 255 }, 0, 0 },
    /* Moderno   */ { 0, { 70, 72, 78, 255 },    { 74, 74, 82, 255 },    { 242, 162, 52, 255 },  { 90, 92, 100, 255 },   { 210, 214, 222, 255 }, 1, 1 },
};

/* ---------- recursos ---------- */
static Font fUi, fBold, fScript;
static Texture2D tTolex, tTweed, tGrille, tBrush;
static RenderTexture2D rt;

/* ---------- mouse em coordenadas lógicas ---------- */
static Vector2 M;
static int mDown, mPressed, mReleased;
static float wheel;
static int cursorWant;
static int typing = 0;

/* ---------- estado ---------- */
typedef struct { char name[64]; Rig rig; int builtin; } Preset;
#define MAXPRESETS 128
static Preset presets[MAXPRESETS];
static int nPresets = 0, curPreset = 0;

static float* activeV = NULL;
static float dragY, dragV;
static float* lastClickV = NULL; static double lastClickT = 0;

static int tab = 0;                     /* 0 presets, 1 editar, 2 áudio */
static int selBlock = SEL_AMP;
static int selIn = -1, selOut = -1, wantExcl = 0;
static float scrollPresets = 0, scrollIn = 0, scrollOut = 0;
static char nameBuf[64]; static int nameLen = 0;
static int confirmDelete = 0; static double confirmUntil = 0;
static float trimNorm = 0.333f;         /* nível da entrada, 0..1 => -12..+24 dB */
static int recDI = 0; static float playVolN = 0.8f;
static int confirmTrash = -1; static double confirmTrashUntil = 0; static float scrollRec = 0;

static char banner[320]; static Color bannerCol; static double bannerUntil = 0; static int bannerIsError = 0;
static char iniPath[600], presetPath[600];
static int dirty = 0; static double dirtyAt = 0;

static float lvIn = -60, lvOut = -60, pkIn = -60, pkOut = -60; static double pkInT = 0, pkOutT = 0, clipUntil = 0;

/* ---------- utilidades ---------- */
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static Color withA(Color c, float a) { c.a = (unsigned char)clampf(c.a * a, 0, 255); return c; }
static Color mixc(Color a, Color b, float t) {
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}
static Color hexc(unsigned v) { return (Color){ (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, 255 }; }
static float lum(Color c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }
static int hover(Rectangle r) { return CheckCollisionPointRec(M, r); }
static int clicked(Rectangle r) { if (hover(r)) { if (cursorWant == 0) cursorWant = 1; return mPressed && !activeV; } return 0; }

static void txt(Font f, const char* s, float x, float y, float size, float sp, Color c) { DrawTextEx(f, s, (Vector2){ x, y }, size, sp, c); }
static float tw(Font f, const char* s, float size, float sp) { return MeasureTextEx(f, s, size, sp).x; }
static void txtC(Font f, const char* s, float cx, float y, float size, float sp, Color c) { txt(f, s, cx - tw(f, s, size, sp) / 2, y, size, sp, c); }
static void txtR(Font f, const char* s, float rx, float y, float size, float sp, Color c) { txt(f, s, rx - tw(f, s, size, sp), y, size, sp, c); }

static void fitText(char* out, int outLen, Font f, const char* s, float size, float maxw)
{
    snprintf(out, outLen, "%s", s);
    if (tw(f, out, size, 0) <= maxw) return;
    char base[300]; snprintf(base, sizeof base, "%s", s);
    int n = (int)strlen(base);
    while (n > 0) {
        n--; while (n > 0 && (base[n] & 0xC0) == 0x80) n--;
        base[n] = 0;
        char tmp[320]; snprintf(tmp, sizeof tmp, "%s…", base);
        if (tw(f, tmp, size, 0) <= maxw) { snprintf(out, outLen, "%s", tmp); return; }
    }
    out[0] = 0;
}

/* quebra o texto em linhas; devolve a altura usada */
static float wrapText(Font f, const char* s, float x, float y, float maxw, float size, float lh, Color c)
{
    char line[400] = ""; char word[120];
    float yy = y; const char* p = s;
    while (*p) {
        int n = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && n < 118) word[n++] = *p++;
        word[n] = 0;
        if (!n) break;
        char test[520];
        snprintf(test, sizeof test, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && tw(f, test, size, 0) > maxw) {
            txt(f, line, x, yy, size, 0, c); yy += lh;
            snprintf(line, sizeof line, "%s", word);
        } else snprintf(line, sizeof line, "%s", test);
    }
    if (line[0]) { txt(f, line, x, yy, size, 0, c); yy += lh; }
    return yy - y;
}

static void clipBegin(Rectangle r) { BeginScissorMode((int)(r.x * SS), (int)(r.y * SS), (int)(r.width * SS), (int)(r.height * SS)); }

static void radial(float cx, float cy, float r, Color in, Color out)
{
    const int seg = 56;
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < seg; i++) {
        float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg;
        rlColor4ub(in.r, in.g, in.b, in.a);    rlVertex2f(cx, cy);
        rlColor4ub(out.r, out.g, out.b, out.a); rlVertex2f(cx + cosf(a1) * r, cy + sinf(a1) * r);
        rlColor4ub(out.r, out.g, out.b, out.a); rlVertex2f(cx + cosf(a0) * r, cy + sinf(a0) * r);
    }
    rlEnd();
}

static void tri(Vector2 a, Vector2 b, Vector2 c, Color col) { DrawTriangle(a, b, c, col); DrawTriangle(a, c, b, col); }

static Vector2 bez(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float t)
{
    float u = 1 - t;
    return (Vector2){ u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
                      u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y };
}
static void cable(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float thick, Color col)
{
    Vector2 prev = p0;
    for (int i = 1; i <= 40; i++) {
        Vector2 q = bez(p0, p1, p2, p3, i / 40.0f);
        DrawLineEx(prev, q, thick, col);
        DrawCircleV(q, thick / 2, col);
        prev = q;
    }
}

static float trimDb(void) { return -12.0f + trimNorm * 36.0f; }

/* ---------- avisos ---------- */
static void setBanner(const char* s, Color c, float secs, int isError)
{
    snprintf(banner, sizeof banner, "%s", s);
    bannerCol = c; bannerIsError = isError;
    bannerUntil = secs > 0 ? GetTime() + secs : 0;
}
static void clearBanner(void) { banner[0] = 0; }

/* ---------- arquivos ---------- */
static void markDirty(void) { dirty = 1; dirtyAt = GetTime(); }
static void trimNl(char* s) { size_t l = strlen(s); while (l && (s[l-1] == '\n' || s[l-1] == '\r')) s[--l] = 0; }

static void saveIni(void)
{
    FILE* f = fopen(iniPath, "w"); if (!f) return;
    char rs[1200]; rig_serialize(&rig, rs, sizeof rs);
    fprintf(f, "gravarlimpo=%d\nvolrepro=%.3f\n", recDI, playVolN);
    fprintf(f, "entrada=%s\nsaida=%s\ncanal=%d\nexclusivo=%d\nnivel=%.3f\npreset=%s\nrig=%s\n",
        selIn >= 0 ? audio_capture_name(selIn) : "", selOut >= 0 ? audio_playback_name(selOut) : "",
        pChan, wantExcl, trimNorm, presets[curPreset].name, rs);
    fclose(f);
    dirty = 0;
}

static int loadIni(char* inName, char* outName, char* presetName)
{
    FILE* f = fopen(iniPath, "r"); if (!f) return 0;
    char line[1400]; int haveRig = 0;
    while (fgets(line, sizeof line, f)) {
        trimNl(line);
        if (!strncmp(line, "entrada=", 8)) snprintf(inName, 256, "%s", line + 8);
        else if (!strncmp(line, "saida=", 6)) snprintf(outName, 256, "%s", line + 6);
        else if (!strncmp(line, "canal=", 6)) pChan = atoi(line + 6);
        else if (!strncmp(line, "exclusivo=", 10)) wantExcl = atoi(line + 10) ? 1 : 0;
        else if (!strncmp(line, "nivel=", 6)) trimNorm = clampf((float)atof(line + 6), 0, 1);
        else if (!strncmp(line, "preset=", 7)) snprintf(presetName, 64, "%s", line + 7);
        else if (!strncmp(line, "rig=", 4)) { rig_parse(&rig, line + 4); haveRig = 1; }
        else if (!strncmp(line, "gravarlimpo=", 12)) recDI = atoi(line + 12) ? 1 : 0;
        else if (!strncmp(line, "volrepro=", 9)) playVolN = clampf((float)atof(line + 9), 0, 1);
    }
    fclose(f);
    if (pChan < 0) pChan = 0;
    return haveRig;
}

static void savePresets(void)
{
    FILE* f = fopen(presetPath, "w"); if (!f) { setBanner("Não foi possível salvar os presets nesta pasta.", C_BAD, 6, 0); return; }
    for (int i = 0; i < nPresets; i++) if (!presets[i].builtin) {
        char rs[1200]; rig_serialize(&presets[i].rig, rs, sizeof rs);
        fprintf(f, "%s|%s\n", presets[i].name, rs);
    }
    fclose(f);
}

static void loadPresets(void)
{
    nPresets = 0;
    for (int i = 0; i < NFACTORY && nPresets < MAXPRESETS; i++) {
        Preset* p = &presets[nPresets++];
        snprintf(p->name, sizeof p->name, "%s", FACTORY[i].name);
        rig_parse(&p->rig, FACTORY[i].rig);
        p->builtin = 1;
    }
    FILE* f = fopen(presetPath, "r"); if (!f) return;
    char line[1400];
    while (fgets(line, sizeof line, f) && nPresets < MAXPRESETS) {
        trimNl(line);
        char* bar = strchr(line, '|'); if (!bar) continue;
        *bar = 0;
        if (!line[0] || !strstr(bar + 1, "amp=")) continue;   /* ignora presets da versão antiga */
        Preset* p = &presets[nPresets++];
        memset(p, 0, sizeof *p);
        snprintf(p->name, sizeof p->name, "%s", line);
        rig_parse(&p->rig, bar + 1);
    }
    fclose(f);
}

static int presetModified(void) { return !rig_equal(&rig, &presets[curPreset].rig); }

static void applyPreset(int i)
{
    if (i < 0 || i >= nPresets) return;
    curPreset = i;
    rig = presets[i].rig;
    confirmDelete = 0;
    markDirty();
}

static void startTyping(void)
{
    typing = 1; tab = 0;
    if (presets[curPreset].builtin) snprintf(nameBuf, sizeof nameBuf, "%s (meu)", presets[curPreset].name);
    else snprintf(nameBuf, sizeof nameBuf, "%s 2", presets[curPreset].name);
    nameLen = (int)strlen(nameBuf);
    if (nameLen > 47) { nameLen = 47; while (nameLen > 0 && (nameBuf[nameLen] & 0xC0) == 0x80) nameLen--; nameBuf[nameLen] = 0; }
}

static void saveCurrent(void)
{
    if (presets[curPreset].builtin) { startTyping(); return; }
    presets[curPreset].rig = rig;
    savePresets(); markDirty();
    setBanner("Preset salvo.", C_OK, 2.5f, 0);
}

static void confirmNewPreset(void)
{
    char name[64]; int j = 0;
    for (int i = 0; nameBuf[i] && j < 63; i++) if (nameBuf[i] != '|') name[j++] = nameBuf[i];
    name[j] = 0;
    while (j > 0 && name[j-1] == ' ') name[--j] = 0;
    if (!name[0]) { setBanner("Dê um nome ao preset antes de salvar.", C_WARN, 3, 0); return; }
    int found = -1;
    for (int i = 0; i < nPresets; i++) if (!strcmp(presets[i].name, name)) found = i;
    if (found >= 0 && presets[found].builtin) { setBanner("Esse nome é de um preset de fábrica. Escolha outro.", C_WARN, 3.5f, 0); return; }
    if (found < 0) {
        if (nPresets >= MAXPRESETS) { setBanner("Limite de presets atingido. Exclua algum antes.", C_WARN, 3.5f, 0); return; }
        found = nPresets++;
        memset(&presets[found], 0, sizeof(Preset));
        snprintf(presets[found].name, sizeof presets[found].name, "%s", name);
    }
    presets[found].rig = rig;
    curPreset = found;
    savePresets(); markDirty();
    typing = 0;
    setBanner("Preset salvo.", C_OK, 2.5f, 0);
}

static void deleteCurrent(void)
{
    if (presets[curPreset].builtin) return;
    for (int i = curPreset; i < nPresets - 1; i++) presets[i] = presets[i + 1];
    nPresets--;
    applyPreset(0);
    savePresets();
    confirmDelete = 0;
    setBanner("Preset excluído.", C_MUTED, 2.5f, 0);
}

/* ---------- áudio ---------- */
static void fmtTime(double secs, char* b, int n)
{
    int t = (int)secs; if (t < 0) t = 0;
    if (t >= 3600) snprintf(b, n, "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    else snprintf(b, n, "%d:%02d", t / 60, t % 60);
}

static void finishRecording(const char* prefix)
{
    if (!rec_active()) return;
    rec_stop();
    char b[320]; snprintf(b, sizeof b, "%s %s", prefix, rec_last_name());
    setBanner(b, rec_overflowed() ? C_WARN : C_OK, 6, 0);
}

static void startAudio(void)
{
    char err[300];
    if (rec_active()) finishRecording("O áudio foi reiniciado, então a gravação foi encerrada e salva:");
    if (rec_playing()) rec_play_stop(audio_running());
    if (!audio_start(selIn, selOut, wantExcl, err, sizeof err)) { setBanner(err, C_BAD, 0, 1); return; }
    if (bannerIsError) clearBanner();
    if (wantExcl && !audio_exclusive_active()) {
        wantExcl = 0;
        setBanner("Este aparelho não aceitou o modo de baixa latência. Voltei para o modo normal.", C_WARN, 6, 0);
    }
    markDirty();
}
static void togglePower(void)
{
    if (audio_running()) {
        finishRecording("Gravação salva:");
        rec_play_stop(1);
        audio_stop();
    } else startAudio();
}

static void toggleRecord(void)
{
    if (rec_active()) { finishRecording("Gravação salva:"); return; }
    if (!audio_running()) startAudio();
    if (!audio_running()) return;
    unsigned rate; float buf; int ch; audio_info(&rate, &buf, &ch);
    char err[300];
    if (!rec_start(recDI, rate, err, sizeof err)) setBanner(err, C_BAD, 6, 0);
}

static void playItem(int i)
{
    if (rec_playing() && rec_play_item() == i) { rec_play_stop(audio_running()); return; }
    if (!audio_running()) startAudio();
    if (!audio_running()) return;
    unsigned rate; float buf; int ch; audio_info(&rate, &buf, &ch);
    char err[300];
    if (!rec_play(i, rate, err, sizeof err)) setBanner(err, C_BAD, 5, 0);
}
static void restartIfRunning(void) { if (audio_running()) startAudio(); markDirty(); }

/* ---------- texturas ---------- */
static unsigned rngState = 12345u;
static int rnd(int lo, int hi) { rngState = rngState * 1664525u + 1013904223u; return lo + (int)((rngState >> 8) % (unsigned)(hi - lo + 1)); }
static unsigned char cl8(int v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

static Texture2D makeTex(Image img)
{
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

static void makeTextures(void)
{
    {   /* tolex */
        const int W = 512, H = 512;
        float* v = (float*)malloc(sizeof(float) * W * H);
        float* w = (float*)malloc(sizeof(float) * W * H);
        for (int i = 0; i < W * H; i++) v[i] = (float)rnd(-100, 100);
        for (int pass = 0; pass < 2; pass++) {
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                float s = 0;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
                    s += v[((y + dy + H) % H) * W + ((x + dx + W) % W)];
                w[y * W + x] = s / 9.0f;
            }
            float* t = v; v = w; w = t;
        }
        Image img = GenImageColor(W, H, BLACK);
        Color* px = (Color*)img.data;
        for (int i = 0; i < W * H; i++) {
            int base = 27 + (int)(v[i] * 0.22f) + rnd(-2, 2);
            px[i] = (Color){ cl8(base + 1), cl8(base), cl8(base - 1), 255 };
        }
        free(v); free(w);
        tTolex = makeTex(img);
    }
    {   /* tweed: sarja diagonal envernizada */
        const int W = 512, H = 512;
        Image img = GenImageColor(W, H, BLACK);
        Color* px = (Color*)img.data;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            int d = (x + y) & 7, d2 = (x - y + 4096) & 15;
            float b = d < 4 ? 1.0f : 0.80f;
            if (d2 < 2) b *= 0.86f;
            int n = rnd(-8, 8);
            px[y * W + x] = (Color){ cl8((int)(196 * b) + n), cl8((int)(156 * b) + n), cl8((int)(98 * b) + n / 2), 255 };
        }
        tTweed = makeTex(img);
    }
    {   /* tela do alto-falante, em cinza (recebe a cor de cada amp) */
        const int W = 504, H = 504, C = 6;
        Image img = GenImageColor(W, H, BLACK);
        Color* px = (Color*)img.data;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            int cell = ((x / C) + (y / C)) & 1;
            float b = cell ? sinf(PI * ((x % C) + 0.5f) / C) : sinf(PI * ((y % C) + 0.5f) / C);
            b = 0.40f + 0.60f * b;
            int v = (int)(118 * b) + rnd(-7, 7);
            px[y * W + x] = (Color){ cl8(v), cl8(v), cl8(v), 255 };
        }
        tGrille = makeTex(img);
    }
    {   /* metal escovado */
        const int W = 512, H = 128;
        Image img = GenImageColor(W, H, BLACK);
        Color* px = (Color*)img.data;
        for (int y = 0; y < H; y++) {
            int row = rnd(-12, 12);
            float ph = (float)rnd(0, 628) / 100.0f; int k = rnd(1, 4);
            for (int x = 0; x < W; x++) {
                int vv = 228 + row + (int)(5 * sinf(2 * PI * k * x / W + ph)) + rnd(-4, 4);
                px[y * W + x] = (Color){ cl8(vv), cl8(vv), cl8(vv), 255 };
            }
        }
        tBrush = makeTex(img);
    }
}

static void drawTiled(Texture2D t, Rectangle r, Color tint)
{
    DrawTexturePro(t, (Rectangle){ 0, 0, r.width * SS, r.height * SS }, r, (Vector2){ 0, 0 }, 0, tint);
}

/* ---------- controles ---------- */
static int button(Rectangle r, const char* label, int style)
{
    int h = hover(r);
    Color bg, fg, bd;
    if (style == 1) { bg = h ? (Color){ 255, 180, 76, 255 } : C_ACC; fg = (Color){ 30, 20, 6, 255 }; bd = bg; }
    else if (style == 2) { bg = h ? (Color){ 120, 36, 30, 255 } : (Color){ 92, 30, 26, 255 }; fg = (Color){ 255, 214, 208, 255 }; bd = C_BAD; }
    else { bg = h ? C_HOVER : C_FIELD; fg = C_TEXT; bd = C_LINE; }
    DrawRectangleRounded(r, 0.22f, 8, bg);
    DrawRectangleRoundedLinesEx(r, 0.22f, 8, 1, bd);
    txtC(fUi, label, r.x + r.width / 2, r.y + (r.height - 15) / 2 - 1, 15, 0, fg);
    return clicked(r);
}

static int segmented(Rectangle r, const char** labels, int n, int sel, int disabledMask)
{
    int res = sel;
    DrawRectangleRounded(r, 0.22f, 8, C_FIELD);
    DrawRectangleRoundedLinesEx(r, 0.22f, 8, 1, C_LINE);
    float w = r.width / n;
    for (int i = 0; i < n; i++) {
        Rectangle s = { r.x + i * w + 3, r.y + 3, w - 6, r.height - 6 };
        int dis = (disabledMask >> i) & 1;
        if (i == sel) DrawRectangleRounded(s, 0.22f, 8, C_ACCDK);
        else if (!dis && hover(s)) DrawRectangleRounded(s, 0.22f, 8, C_HOVER);
        Color fg = dis ? withA(C_MUTED, 0.45f) : (i == sel ? C_ACC : C_TEXT);
        txtC(fUi, labels[i], s.x + s.width / 2, s.y + (s.height - 14) / 2 - 1, 14, 0, fg);
        if (!dis && clicked(s)) res = i;
    }
    return res;
}

static void meter(Rectangle r, float db, float peakDb)
{
    const int N = 32; float gap = 2;
    float sw = (r.width - gap * (N - 1)) / N;
    float lit = (db + 60) / 60 * N, pk = (peakDb + 60) / 60 * N;
    for (int i = 0; i < N; i++) {
        float f = (float)i / N;
        Color on = f < 0.70f ? C_OK : (f < 0.88f ? C_WARN : C_BAD);
        Color off = mixc(C_FIELD, on, 0.12f);
        int isPeak = (int)pk - 1 == i && pk > 1;
        DrawRectangleRec((Rectangle){ r.x + i * (sw + gap), r.y, sw, r.height }, (i < lit || isPeak) ? on : off);
    }
}

/* interação comum a todos os knobs; devolve 1 se o mouse está sobre ele */
static int knobInput(float* v, float def, Rectangle hit)
{
    int h = hover(hit) && !typing;
    if (h && !activeV) cursorWant = 2;
    if (h && mPressed && !activeV) {
        double now = GetTime();
        if (lastClickV == v && now - lastClickT < 0.35) { *v = def; lastClickV = NULL; markDirty(); }
        else { activeV = v; dragY = M.y; dragV = *v; lastClickV = v; lastClickT = now; }
    }
    if (activeV == v && mDown) {
        float sens = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? 900.0f : 220.0f;
        *v = clampf(dragV + (dragY - M.y) / sens, 0, 1);
        markDirty(); cursorWant = 2;
    }
    if (h && wheel != 0 && !activeV) { *v = clampf(*v + wheel * 0.025f, 0, 1); markDirty(); }
    return h || activeV == v;
}

/* knob do painel do amplificador */
static void ampKnob(float* v, const ParamDef* pd, float cx, float cy, float r, const AmpLook* L)
{
    int h = knobInput(v, pd->def, (Rectangle){ cx - r - 12, cy - r - 12, 2 * r + 24, 2 * r + 24 });
    float t = *v;
    for (int i = 0; i <= 10; i++) {
        float a = (-135.0f + 270.0f * i / 10.0f - 90.0f) * DEG2RAD;
        float ca = cosf(a), sa = sinf(a);
        float r0 = r + 5, r1 = r + (i % 2 == 0 ? 10 : 8);
        DrawLineEx((Vector2){ cx + ca * r0, cy + sa * r0 }, (Vector2){ cx + ca * r1, cy + sa * r1 }, i % 2 == 0 ? 1.6f : 1.0f, withA(L->ink, 0.85f));
        if (i % 2 == 0) {
            char n[4]; snprintf(n, sizeof n, "%d", i);
            float rr = r + 17, fs = 10.5f;
            Vector2 sz = MeasureTextEx(fBold, n, fs, 0);
            txt(fBold, n, cx + ca * rr - sz.x / 2, cy + sa * rr - sz.y / 2, fs, 0, L->ink);
        }
    }
    DrawCircle((int)(cx + 2), (int)(cy + 4), r + 3, (Color){ 0, 0, 0, 90 });
    if (L->blackKnobs) {
        DrawCircleV((Vector2){ cx, cy }, r + 3, (Color){ 18, 18, 20, 255 });
        for (int i = 0; i < 40; i++) {
            float a = i * 2 * PI / 40;
            DrawLineEx((Vector2){ cx + cosf(a) * (r + 0.5f), cy + sinf(a) * (r + 0.5f) },
                       (Vector2){ cx + cosf(a) * (r + 3), cy + sinf(a) * (r + 3) }, 1.2f, (Color){ 52, 52, 56, 255 });
        }
        DrawCircleV((Vector2){ cx, cy }, r, (Color){ 30, 30, 33, 255 });
        radial(cx, cy, r * 0.62f, (Color){ 190, 192, 198, 255 }, (Color){ 120, 122, 128, 255 });
        radial(cx - r * 0.18f, cy - r * 0.2f, r * 0.3f, (Color){ 255, 255, 255, 170 }, (Color){ 255, 255, 255, 0 });
        float a = (-135.0f + 270.0f * t - 90.0f) * DEG2RAD;
        DrawLineEx((Vector2){ cx + cosf(a) * r * 0.62f, cy + sinf(a) * r * 0.62f },
                   (Vector2){ cx + cosf(a) * r * 0.95f, cy + sinf(a) * r * 0.95f }, 3.0f, (Color){ 240, 240, 244, 255 });
    } else {
        DrawCircleV((Vector2){ cx, cy }, r + 3, (Color){ 34, 30, 26, 255 });
        for (int i = 0; i < 40; i++) {
            float a = i * 2 * PI / 40;
            DrawLineEx((Vector2){ cx + cosf(a) * (r + 0.5f), cy + sinf(a) * (r + 0.5f) },
                       (Vector2){ cx + cosf(a) * (r + 3), cy + sinf(a) * (r + 3) }, 1.2f, (Color){ 72, 66, 58, 255 });
        }
        DrawCircleV((Vector2){ cx, cy }, r, (Color){ 196, 184, 156, 255 });
        radial(cx, cy, r * 0.92f, (Color){ 238, 230, 210, 255 }, (Color){ 196, 184, 156, 255 });
        radial(cx - r * 0.28f, cy - r * 0.32f, r * 0.55f, (Color){ 255, 252, 242, 200 }, (Color){ 255, 252, 242, 0 });
        DrawRing((Vector2){ cx, cy }, r - 1.2f, r, 0, 360, 56, (Color){ 120, 106, 82, 255 });
        float a = (-135.0f + 270.0f * t - 90.0f) * DEG2RAD;
        DrawLineEx((Vector2){ cx + cosf(a) * r * 0.22f, cy + sinf(a) * r * 0.22f },
                   (Vector2){ cx + cosf(a) * r * 0.86f, cy + sinf(a) * r * 0.86f }, 3.2f, (Color){ 36, 28, 18, 255 });
    }
    float ly = cy - r - 38;
    if (h) {
        char vb[32]; param_format(pd, *v, vb, sizeof vb);
        float w = tw(fUi, vb, 13, 0) + 16;
        DrawRectangleRounded((Rectangle){ cx - w / 2, ly - 3, w, 20 }, 0.5f, 8, (Color){ 24, 22, 20, 235 });
        txtC(fUi, vb, cx, ly, 13, 0, C_CREAM);
    } else txtC(fBold, pd->label, cx, ly, 13.5f, 1.3f, L->ink);
}

/* knob do editor de pedais */
static void editKnob(float* v, const ParamDef* pd, float cx, float cy, float r, Color accent)
{
    int h = knobInput(v, pd->def, (Rectangle){ cx - 44, cy - r - 24, 88, 2 * r + 48 });
    txtC(fBold, pd->label, cx, cy - r - 22, 13, 1.2f, h ? C_TEXT : C_MUTED);
    float a0 = -225, a1 = 45;
    DrawRing((Vector2){ cx, cy }, r + 4, r + 7, a0, a1, 48, (Color){ 46, 48, 54, 255 });
    if (*v > 0.002f) DrawRing((Vector2){ cx, cy }, r + 4, r + 7, a0, a0 + (a1 - a0) * *v, 48, accent);
    DrawCircle((int)(cx + 1), (int)(cy + 3), r, (Color){ 0, 0, 0, 110 });
    DrawCircleV((Vector2){ cx, cy }, r, (Color){ 52, 54, 60, 255 });
    radial(cx, cy - r * 0.2f, r * 0.9f, (Color){ 84, 87, 95, 255 }, (Color){ 52, 54, 60, 255 });
    float a = (-135.0f + 270.0f * *v - 90.0f) * DEG2RAD;
    DrawLineEx((Vector2){ cx + cosf(a) * r * 0.3f, cy + sinf(a) * r * 0.3f },
               (Vector2){ cx + cosf(a) * r * 0.85f, cy + sinf(a) * r * 0.85f }, 2.6f, C_TEXT);
    char vb[32]; param_format(pd, *v, vb, sizeof vb);
    txtC(fUi, vb, cx, cy + r + 10, 13, 0, h ? C_ACC : C_TEXT);
}

/* ---------- amplificador ---------- */
static const Rectangle AMP_R = { 44, 92, 792, 336 };

static void ampGeometry(Rectangle* F, Rectangle* G, Rectangle* P)
{
    Rectangle A = AMP_R;
    *F = (Rectangle){ A.x + 32, A.y + 28, A.width - 64, A.height - 56 };
    float gh = F->height * 0.52f;
    *G = (Rectangle){ F->x, F->y, F->width, gh };
    *P = (Rectangle){ F->x, F->y + gh, F->width, F->height - gh };
}

static void drawPowerSwitch(float x, float cy, Color ink)
{
    int on = audio_running();
    Rectangle bz = { x - 19, cy - 28, 38, 52 };
    if (clicked(bz) && !typing) togglePower();
    if (on) radial(x, cy - 2, 58, (Color){ 255, 70, 40, 80 }, (Color){ 255, 70, 40, 0 });
    DrawRectangleRounded((Rectangle){ bz.x + 1, bz.y + 3, bz.width, bz.height }, 0.25f, 8, (Color){ 0, 0, 0, 90 });
    DrawRectangleRounded(bz, 0.25f, 8, (Color){ 24, 20, 17, 255 });
    Rectangle rk = { x - 13, cy - 22, 26, 40 };
    Color top = on ? (Color){ 255, 104, 76, 255 } : (Color){ 104, 28, 22, 255 };
    Color bot = on ? (Color){ 200, 24, 16, 255 } : (Color){ 62, 14, 10, 255 };
    DrawRectangleGradientV((int)rk.x, (int)rk.y, (int)rk.width, (int)rk.height, top, bot);
    if (on) DrawRectangle((int)rk.x, (int)(rk.y + rk.height - 6), (int)rk.width, 6, (Color){ 120, 10, 6, 255 });
    else DrawRectangle((int)rk.x, (int)rk.y, (int)rk.width, 6, (Color){ 150, 44, 36, 255 });
    DrawRectangleLinesEx(rk, 1, (Color){ 0, 0, 0, 120 });
    txtC(fBold, "POWER", x, cy + 30, 13, 1.4f, ink);
}

static void drawJack(float jx, float jy, Color ink)
{
    txtC(fBold, "ENTRADA", jx, jy - 40, 13, 1.4f, ink);
    DrawCircle((int)(jx + 1), (int)(jy + 3), 19, (Color){ 0, 0, 0, 90 });
    DrawPoly((Vector2){ jx, jy }, 6, 19, 30, (Color){ 150, 150, 158, 255 });
    DrawPolyLinesEx((Vector2){ jx, jy }, 6, 19, 30, 1.2f, (Color){ 90, 90, 98, 255 });
    radial(jx - 4, jy - 5, 10, (Color){ 250, 250, 252, 160 }, (Color){ 250, 250, 252, 0 });
    DrawCircle((int)jx, (int)jy, 12, (Color){ 186, 186, 194, 255 });
    radial(jx - 3, jy - 4, 7, (Color){ 255, 255, 255, 200 }, (Color){ 255, 255, 255, 0 });
    DrawCircle((int)jx, (int)jy, 8, (Color){ 26, 26, 28, 255 });
    DrawRing((Vector2){ jx, jy }, 7, 8, 0, 360, 32, (Color){ 70, 70, 76, 255 });
}

static void drawAmp(void)
{
    Rectangle A = AMP_R;
    const AmpLook* L = &LOOKS[rig.amp];
    for (int i = 0; i < 5; i++)
        DrawEllipse((int)(A.x + A.width / 2), (int)(A.y + A.height + 4), A.width * (0.50f - i * 0.025f), 14.0f - i * 2.2f, (Color){ 0, 0, 0, 34 });

    Rectangle H = { A.x + A.width / 2 - 120, A.y - 26, 240, 40 };
    DrawRectangleRounded(H, 0.55f, 12, (Color){ 14, 13, 12, 255 });
    DrawRectangleRounded((Rectangle){ H.x + 22, H.y + 6, H.width - 44, 10 }, 0.9f, 10, (Color){ 44, 40, 36, 255 });
    for (int s = 0; s < 2; s++) {
        Rectangle c = { s ? H.x + H.width - 24 : H.x - 4, A.y - 14, 28, 22 };
        DrawRectangleRounded(c, 0.3f, 6, (Color){ 112, 112, 118, 255 });
        DrawRectangleGradientV((int)c.x + 3, (int)c.y + 2, (int)c.width - 6, 8, (Color){ 220, 220, 226, 160 }, (Color){ 220, 220, 226, 0 });
    }

    DrawRectangleRounded(A, 0.06f, 16, L->tweed ? (Color){ 150, 116, 70, 255 } : (Color){ 22, 21, 20, 255 });
    Rectangle T = { A.x + 8, A.y + 8, A.width - 16, A.height - 16 };
    drawTiled(L->tweed ? tTweed : tTolex, T, WHITE);
    if (L->tweed) {
        DrawRectangleGradientV((int)T.x, (int)T.y, (int)T.width, (int)T.height, (Color){ 255, 220, 150, 30 }, (Color){ 60, 30, 0, 60 });
        DrawRectangleRoundedLinesEx(A, 0.06f, 16, 3, (Color){ 120, 86, 44, 255 });
    } else {
        DrawRectangleGradientV((int)T.x, (int)T.y, (int)T.width, 26, (Color){ 255, 255, 255, 16 }, (Color){ 255, 255, 255, 0 });
        DrawRectangleRoundedLinesEx(A, 0.06f, 16, 1.5f, (Color){ 64, 60, 56, 255 });
    }
    if (!L->tweed) for (int i = 0; i < 4; i++) {
        float cx = (i & 1) ? A.x + A.width - 30 : A.x - 2, cy = (i & 2) ? A.y + A.height - 30 : A.y - 2;
        Rectangle c = { cx, cy, 32, 32 };
        DrawRectangleRounded(c, 0.35f, 8, (Color){ 52, 52, 56, 255 });
        DrawRectangleGradientV((int)c.x + 4, (int)c.y + 3, 24, 10, (Color){ 200, 200, 210, 60 }, (Color){ 200, 200, 210, 0 });
        DrawCircle((int)(c.x + 16), (int)(c.y + 16), 3, (Color){ 128, 128, 136, 255 });
    }

    Rectangle F, G, P; ampGeometry(&F, &G, &P);
    DrawRectangleRounded((Rectangle){ F.x - 5, F.y - 5, F.width + 10, F.height + 10 }, 0.04f, 10, (Color){ 6, 6, 6, 220 });
    drawTiled(tGrille, G, L->grille);
    DrawRectangleGradientV((int)G.x, (int)G.y, (int)G.width, 40, (Color){ 0, 0, 0, 130 }, (Color){ 0, 0, 0, 0 });
    DrawRectangleGradientV((int)G.x, (int)(G.y + G.height - 30), (int)G.width, 30, (Color){ 0, 0, 0, 0 }, (Color){ 0, 0, 0, 120 });

    const char* logo = AMPS[rig.amp].logo;
    if (L->sansLogo) {
        char up[32]; int j = 0; for (const char* p = logo; *p && j < 30; p++) up[j++] = (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p); up[j] = 0;
        float ls = 46, sp = 10;
        Vector2 sz = MeasureTextEx(fBold, up, ls, sp);
        float lx = G.x + (G.width - sz.x) / 2, ly = G.y + (G.height - sz.y) / 2;
        Rectangle plate = { lx - 26, ly - 8, sz.x + 52, sz.y + 14 };
        DrawRectangleRounded(plate, 0.18f, 8, (Color){ 20, 20, 22, 255 });
        DrawRectangleRoundedLinesEx(plate, 0.18f, 8, 2, (Color){ 140, 144, 152, 255 });
        txt(fBold, up, lx, ly, ls, sp, L->logo);
        DrawRectangle((int)plate.x + 10, (int)(plate.y + plate.height - 6), (int)plate.width - 20, 2, C_ACC);
    } else {
        float ls = 76;
        Vector2 lsz = MeasureTextEx(fScript, logo, ls, 0);
        float lx = G.x + (G.width - lsz.x) / 2, ly = G.y + (G.height - lsz.y) / 2 + 2;
        txt(fScript, logo, lx + 3, ly + 4, ls, 0, (Color){ 0, 0, 0, 170 });
        txt(fScript, logo, lx, ly, ls, 0, L->logo);
    }

    drawTiled(tBrush, P, L->panel);
    DrawRectangleGradientV((int)P.x, (int)P.y, (int)P.width, (int)P.height, (Color){ 255, 244, 214, 40 }, (Color){ 0, 0, 0, 70 });
    DrawRectangle((int)P.x, (int)P.y, (int)P.width, 2, (Color){ 0, 0, 0, 120 });
    DrawRectangle((int)P.x, (int)P.y + 2, (int)P.width, 1, (Color){ 255, 255, 255, 70 });
    DrawRectangleRoundedLinesEx(F, 0.03f, 10, 3, L->piping);

    float cy = P.y + P.height * 0.57f;
    drawPowerSwitch(P.x + 48, cy, L->ink);
    float k0 = P.x + 150, k1 = P.x + P.width - 128;
    for (int i = 0; i < 6; i++) ampKnob(&rig.ak[i], &AMPK[i], k0 + (k1 - k0) * i / 5.0f, cy, 22, L);
    drawJack(P.x + P.width - 52, cy, L->ink);

    /* clique no logo abre o editor do amplificador */
    if (clicked(G) && !typing) { selBlock = SEL_AMP; tab = 1; }
}

static void drawAmpCable(void)
{
    Rectangle F, G, P; ampGeometry(&F, &G, &P);
    Rectangle A = AMP_R;
    float jx = P.x + P.width - 52, jy = P.y + P.height * 0.57f;
    Vector2 p0 = { jx, jy }, p1 = { jx + 44, jy + 14 }, p2 = { A.x + A.width + 44, A.y + A.height - 10 }, p3 = { A.x + A.width + 22, VH };
    cable((Vector2){ p0.x + 2, p0.y + 5 }, (Vector2){ p1.x + 2, p1.y + 5 }, (Vector2){ p2.x + 2, p2.y + 5 }, (Vector2){ p3.x + 2, p3.y + 5 }, 10, (Color){ 0, 0, 0, 90 });
    cable(p0, p1, p2, p3, 10, (Color){ 20, 20, 22, 255 });
    cable((Vector2){ p0.x - 1.5f, p0.y - 2 }, (Vector2){ p1.x - 1.5f, p1.y - 2 }, (Vector2){ p2.x - 2.5f, p2.y - 1 }, (Vector2){ p3.x - 2.5f, p3.y }, 2, (Color){ 74, 74, 80, 255 });
    DrawCircle((int)jx, (int)jy, 12, (Color){ 186, 186, 194, 255 });
    radial(jx - 3, jy - 4, 7, (Color){ 255, 255, 255, 200 }, (Color){ 255, 255, 255, 0 });
    DrawCircle((int)jx, (int)jy, 8, (Color){ 26, 26, 28, 255 });
    DrawRing((Vector2){ jx, jy }, 7, 8, 0, 360, 32, (Color){ 70, 70, 76, 255 });
}

/* ---------- pedaleira ---------- */
static const Rectangle BOARD = { 36, 444, 808, 260 };
#define PW 74.0f
#define PH 132.0f
#define PY 490.0f

static Rectangle itemRect(int item)
{
    float gap = (BOARD.width - 32 - 9 * PW) / 8.0f;
    return (Rectangle){ BOARD.x + 16 + item * (PW + gap), PY, PW, PH };
}
static int itemOfSlot(int s) { return s < AMP_POS ? s : s + 1; }

static void miniKnob(float cx, float cy, float r, float v, Color body)
{
    int dark = lum(body) < 120;
    DrawCircleV((Vector2){ cx + 0.5f, cy + 1.5f }, r, (Color){ 0, 0, 0, 90 });
    DrawCircleV((Vector2){ cx, cy }, r, dark ? (Color){ 220, 222, 226, 255 } : (Color){ 24, 24, 26, 255 });
    float a = (-135.0f + 270.0f * v - 90.0f) * DEG2RAD;
    DrawLineEx((Vector2){ cx, cy }, (Vector2){ cx + cosf(a) * r * 0.9f, cy + sinf(a) * r * 0.9f }, 1.8f, dark ? (Color){ 20, 20, 22, 255 } : (Color){ 240, 240, 244, 255 });
}

static void drawPedal(int slot)
{
    Rectangle r = itemRect(itemOfSlot(slot));
    SlotState* st = &rig.s[slot];
    const FxType* t = &SLOTS[slot].types[st->type];
    int sel = selBlock == slot, on = st->on;
    Color body = hexc(t->color);
    if (!on) body = mixc(body, (Color){ 38, 40, 44, 255 }, 0.7f);
    int lightBody = lum(body) > 150;
    Color ink = lightBody ? (Color){ 30, 30, 34, 255 } : (Color){ 240, 240, 242, 255 };

    Rectangle foot = { r.x + r.width / 2 - 16, r.y + r.height - 44, 32, 32 };
    int hf = hover(foot), hb = hover(r);

    if (sel) DrawRectangleRoundedLinesEx((Rectangle){ r.x - 5, r.y - 5, r.width + 10, r.height + 10 }, 0.16f, 10, 2, C_ACC);
    DrawRectangleRounded((Rectangle){ r.x + 2, r.y + 5, r.width, r.height }, 0.12f, 10, (Color){ 0, 0, 0, 120 });
    DrawRectangleRounded(r, 0.12f, 10, body);
    DrawRectangleGradientV((int)r.x + 4, (int)r.y + 3, (int)r.width - 8, 22, (Color){ 255, 255, 255, 40 }, (Color){ 255, 255, 255, 0 });
    if (hb && !hf) DrawRectangleRoundedLinesEx(r, 0.12f, 10, 1.5f, (Color){ 255, 255, 255, 70 });

    int nk = t->nk < 3 ? t->nk : 3;
    for (int i = 0; i < nk; i++) {
        float cx = r.x + r.width * (i + 1) / (nk + 1);
        miniKnob(cx, r.y + 20, nk == 3 ? 8.5f : 10, st->k[i], body);
    }

    char nm[40]; float fs = 13.5f;
    while (fs > 9 && tw(fBold, t->shortName, fs, 0.6f) > r.width - 8) fs -= 0.5f;
    snprintf(nm, sizeof nm, "%s", t->shortName);
    txtC(fBold, nm, r.x + r.width / 2, r.y + 40, fs, 0.6f, ink);

    Vector2 led = { r.x + r.width / 2, r.y + 68 };
    if (on) radial(led.x, led.y, 14, (Color){ 255, 60, 40, 140 }, (Color){ 255, 60, 40, 0 });
    DrawCircleV(led, 4, on ? (Color){ 255, 80, 60, 255 } : (Color){ 70, 24, 22, 255 });

    DrawCircleV((Vector2){ foot.x + 16, foot.y + 17 }, 15, (Color){ 0, 0, 0, 100 });
    DrawCircleV((Vector2){ foot.x + 16, foot.y + 16 }, 15, (Color){ 120, 122, 130, 255 });
    radial(foot.x + 16, foot.y + 16, 13, hf ? (Color){ 250, 250, 252, 255 } : (Color){ 220, 222, 228, 255 }, (Color){ 140, 142, 150, 255 });
    DrawRing((Vector2){ foot.x + 16, foot.y + 16 }, 9, 10, 0, 360, 32, (Color){ 110, 112, 120, 255 });

    Color cat = sel ? C_ACC : C_MUTED;
    txtC(fBold, SLOTS[slot].category, r.x + r.width / 2, r.y + r.height + 10, 12, 1.2f, cat);

    if (!typing && mPressed && !activeV) {
        if (hf) { st->on = !st->on; selBlock = slot; markDirty(); }
        else if (hb) { selBlock = slot; tab = 1; }
    }
    if (hb) cursorWant = 1;
}

static void drawAmpTile(void)
{
    Rectangle r = itemRect(AMP_POS);
    const AmpLook* L = &LOOKS[rig.amp];
    int sel = selBlock == SEL_AMP, hb = hover(r);
    if (sel) DrawRectangleRoundedLinesEx((Rectangle){ r.x - 5, r.y - 5, r.width + 10, r.height + 10 }, 0.16f, 10, 2, C_ACC);
    DrawRectangleRounded((Rectangle){ r.x + 2, r.y + 5, r.width, r.height }, 0.12f, 10, (Color){ 0, 0, 0, 120 });
    DrawRectangleRounded(r, 0.12f, 10, (Color){ 36, 38, 44, 255 });
    if (hb) DrawRectangleRoundedLinesEx(r, 0.12f, 10, 1.5f, (Color){ 255, 255, 255, 70 });
    /* mini cabeçote */
    Rectangle h = { r.x + 8, r.y + 26, r.width - 16, 46 };
    DrawRectangleRounded(h, 0.15f, 6, L->tweed ? (Color){ 190, 150, 94, 255 } : (Color){ 20, 20, 22, 255 });
    Rectangle g = { h.x + 5, h.y + 5, h.width - 10, 20 };
    DrawRectangleRec(g, mixc((Color){ 40, 40, 42, 255 }, L->grille, 0.55f));
    Rectangle p = { h.x + 5, h.y + 27, h.width - 10, 13 };
    DrawRectangleRec(p, L->panel);
    for (int i = 0; i < 5; i++) DrawCircle((int)(p.x + 8 + i * (p.width - 16) / 4), (int)(p.y + 6.5f), 2.6f, L->blackKnobs ? (Color){ 20, 20, 22, 255 } : C_CREAM);
    DrawRectangleRoundedLinesEx(h, 0.15f, 6, 1, L->piping);
    DrawRectangle((int)(r.x + r.width / 2 - 10), (int)(r.y + 18), 20, 6, (Color){ 14, 14, 16, 255 });

    char nm[40]; fitText(nm, sizeof nm, fBold, AMPS[rig.amp].logo, 13, r.width - 8);
    txtC(fBold, nm, r.x + r.width / 2, r.y + 82, 13, 0.6f, C_TEXT);
    char cb[40]; fitText(cb, sizeof cb, fUi, CABS[rig.cab], 11, r.width - 6);
    txtC(fUi, cb, r.x + r.width / 2, r.y + 100, 11, 0, C_MUTED);
    txtC(fBold, "AMP", r.x + r.width / 2, r.y + r.height + 10, 12, 1.2f, sel ? C_ACC : C_MUTED);
    if (hb) { cursorWant = 1; if (mPressed && !typing && !activeV) { selBlock = SEL_AMP; tab = 1; } }
}

static void drawBoard(void)
{
    DrawRectangleRounded((Rectangle){ BOARD.x, BOARD.y + 4, BOARD.width, BOARD.height }, 0.04f, 10, (Color){ 0, 0, 0, 90 });
    DrawRectangleRounded(BOARD, 0.04f, 10, (Color){ 28, 29, 33, 255 });
    for (int i = 0; i < 4; i++) {
        float y = BOARD.y + 52 + i * 36;
        DrawRectangle((int)BOARD.x + 10, (int)y, (int)BOARD.width - 20, 14, (Color){ 22, 23, 26, 255 });
        DrawRectangle((int)BOARD.x + 10, (int)y, (int)BOARD.width - 20, 1, (Color){ 44, 46, 52, 255 });
    }
    DrawRectangleRoundedLinesEx(BOARD, 0.04f, 10, 1, C_LINE);
    txt(fBold, "PEDALEIRA", BOARD.x + 16, BOARD.y + 14, 14, 1.6f, C_MUTED);
    txtR(fUi, "Clique num pedal para editar. Pise no botão para ligar ou desligar.", BOARD.x + BOARD.width - 16, BOARD.y + 15, 12.5f, 0, withA(C_MUTED, 0.85f));

    /* cabos entre os pedais */
    for (int i = 0; i < 8; i++) {
        Rectangle a = itemRect(i), b = itemRect(i + 1);
        Vector2 p0 = { a.x + a.width - 2, a.y + 30 }, p3 = { b.x + 2, b.y + 30 };
        float mx = (p0.x + p3.x) / 2;
        int live = audio_running();
        Color cc = live ? (Color){ 64, 58, 50, 255 } : (Color){ 46, 46, 50, 255 };
        cable(p0, (Vector2){ mx - 2, p0.y + 16 }, (Vector2){ mx + 2, p3.y + 16 }, p3, 3.5f, cc);
    }
    for (int s = 0; s < NSLOT; s++) drawPedal(s);
    drawAmpTile();
}

static void drawBanner(void)
{
    if (!banner[0]) return;
    if (bannerUntil > 0 && GetTime() > bannerUntil) { banner[0] = 0; return; }
    char line[340]; fitText(line, sizeof line, fUi, banner, 14, 720);
    float w = tw(fUi, line, 14, 0) + 52;
    Rectangle r = { 440 - w / 2, 64, w, 34 };
    DrawRectangleRounded((Rectangle){ r.x, r.y + 3, r.width, r.height }, 0.3f, 8, (Color){ 0, 0, 0, 120 });
    DrawRectangleRounded(r, 0.3f, 8, mixc(C_BAR, bannerCol, 0.18f));
    DrawRectangleRoundedLinesEx(r, 0.3f, 8, 1, withA(bannerCol, 0.7f));
    DrawCircle((int)(r.x + 18), (int)(r.y + 17), 4, bannerCol);
    txt(fUi, line, r.x + 32, r.y + 9, 14, 0, C_TEXT);
}

/* ---------- painel lateral ---------- */
#define SX 896.0f
#define SW 288.0f

static void drawPresetsTab(void)
{
    Rectangle L = { SX, 112, SW, 400 };
    const float rowH = 34, headH = 28;
    int nUser = 0; for (int i = 0; i < nPresets; i++) if (!presets[i].builtin) nUser++;
    float contentH = headH + NFACTORY * rowH + headH + (nUser ? nUser * rowH : rowH);
    if (hover(L) && wheel != 0) scrollPresets -= wheel * 40;
    scrollPresets = clampf(scrollPresets, 0, fmaxf(0, contentH - L.height));
    int modified = presetModified();

    clipBegin(L);
    float y = L.y - scrollPresets;
    for (int pass = 0; pass < 2; pass++) {
        txt(fBold, pass == 0 ? "DE FÁBRICA" : "MEUS PRESETS", SX + 2, y + 8, 12.5f, 1.4f, withA(C_MUTED, 0.9f));
        y += headH;
        int any = 0;
        for (int i = 0; i < nPresets; i++) {
            if (presets[i].builtin != (pass == 0)) continue;
            any = 1;
            Rectangle r = { SX, y, SW, rowH - 3 };
            y += rowH;
            if (r.y + r.height < L.y || r.y > L.y + L.height) continue;
            int sel = i == curPreset, h = hover(r) && hover(L);
            if (sel) DrawRectangleRounded(r, 0.2f, 8, (Color){ 48, 42, 34, 255 });
            else if (h) DrawRectangleRounded(r, 0.2f, 8, C_HOVER);
            char nm[80]; fitText(nm, sizeof nm, fUi, presets[i].name, 15, SW - (sel && modified ? 92 : 28));
            txt(fUi, nm, r.x + 12, r.y + (r.height - 15) / 2 - 1, 15, 0, sel ? C_ACC : C_TEXT);
            if (sel && modified) txtR(fUi, "alterado", r.x + r.width - 12, r.y + (r.height - 12) / 2, 12, 0, C_ACC);
            if (h) { cursorWant = 1; if (mPressed && !typing && !activeV) applyPreset(i); }
        }
        if (!any) {
            txt(fUi, "Nenhum ainda. Ajuste o som e salve.", SX + 12, y + 8, 13, 0, C_MUTED);
            y += rowH;
        }
    }
    EndScissorMode();
    if (contentH > L.height) {
        float bh = L.height * L.height / contentH, by = L.y + (L.height - bh) * (scrollPresets / (contentH - L.height));
        DrawRectangleRounded((Rectangle){ SX + SW - 3, by, 3, bh }, 1, 4, C_LINE);
    }

    float by = 528;
    DrawLine((int)SX, (int)by - 8, (int)(SX + SW), (int)by - 8, C_LINE);
    if (!typing) {
        if (button((Rectangle){ SX, by, 138, 36 }, "Salvar", modified ? 1 : 0)) saveCurrent();
        if (button((Rectangle){ SX + 150, by, 138, 36 }, "Salvar como novo", 0)) startTyping();
        if (!presets[curPreset].builtin) {
            if (confirmDelete && GetTime() > confirmUntil) confirmDelete = 0;
            if (button((Rectangle){ SX, by + 46, SW, 36 }, confirmDelete ? "Clique de novo para excluir" : "Excluir preset", confirmDelete ? 2 : 0)) {
                if (confirmDelete) deleteCurrent(); else { confirmDelete = 1; confirmUntil = GetTime() + 4; }
            }
        }
    } else {
        txt(fBold, "NOME DO NOVO PRESET", SX, by - 2, 13, 1.4f, C_MUTED);
        Rectangle tb = { SX, by + 16, SW, 38 };
        DrawRectangleRounded(tb, 0.2f, 8, C_FIELD);
        DrawRectangleRoundedLinesEx(tb, 0.2f, 8, 1.5f, C_ACC);
        clipBegin((Rectangle){ tb.x + 8, tb.y, tb.width - 16, tb.height });
        float tx = tb.x + 12, textW = tw(fUi, nameBuf, 16, 0);
        if (textW > tb.width - 30) tx -= textW - (tb.width - 30);
        txt(fUi, nameBuf, tx, tb.y + 10, 16, 0, C_TEXT);
        if (fmod(GetTime(), 1.0) < 0.6) DrawRectangle((int)(tx + textW + 2), (int)tb.y + 9, 2, 20, C_ACC);
        EndScissorMode();
        if (button((Rectangle){ SX, by + 64, 138, 36 }, "Salvar", 1)) confirmNewPreset();
        if (button((Rectangle){ SX + 150, by + 64, 138, 36 }, "Cancelar", 0)) typing = 0;
    }
    txt(fUi, "Arraste um knob para cima ou para baixo.", SX, 656, 12.5f, 0, C_MUTED);
    txt(fUi, "Shift faz o ajuste fino. Duplo clique volta ao padrão.", SX, 674, 12.5f, 0, C_MUTED);
}

static int toggleSwitch(Rectangle r, int on)
{
    int h = hover(r);
    DrawRectangleRounded(r, 0.5f, 10, on ? C_ACCDK : C_FIELD);
    DrawRectangleRoundedLinesEx(r, 0.5f, 10, 1, on ? C_ACC : (h ? C_MUTED : C_LINE));
    float kx = on ? r.x + r.width - r.height / 2 : r.x + r.height / 2;
    DrawCircleV((Vector2){ kx, r.y + r.height / 2 }, r.height / 2 - 4, on ? C_ACC : C_MUTED);
    const char* l = on ? "LIGADO" : "DESLIGADO";
    float tx = on ? r.x + 12 : r.x + r.height + 2;
    txt(fBold, l, tx, r.y + (r.height - 12) / 2 - 1, 12, 1.2f, on ? C_ACC : C_MUTED);
    return clicked(r);
}

static void drawEditSlot(int slot)
{
    SlotState* st = &rig.s[slot];
    const SlotDef* sd = &SLOTS[slot];
    const FxType* t = &sd->types[st->type];
    Color acc = hexc(t->color);
    if (lum(acc) < 70) acc = mixc(acc, C_TEXT, 0.45f);

    txt(fBold, sd->category, SX, 112, 13, 1.4f, C_MUTED);
    txt(fBold, t->name, SX, 128, 26, 0.3f, C_TEXT);
    if (toggleSwitch((Rectangle){ SX + SW - 112, 124, 112, 30 }, st->on)) { st->on = !st->on; markDirty(); }
    float y = 166;
    y += wrapText(fUi, t->desc, SX, y, SW, 13, 18, C_MUTED) + 12;

    if (sd->ntypes > 1) {
        txt(fBold, "TIPO", SX, y, 12.5f, 1.4f, C_MUTED); y += 18;
        for (int i = 0; i < sd->ntypes; i++) {
            Rectangle b = { SX + (i % 2) * 148, y + (i / 2) * 38, 140, 32 };
            int sel = i == st->type, h = hover(b);
            DrawRectangleRounded(b, 0.25f, 8, sel ? C_ACCDK : (h ? C_HOVER : C_FIELD));
            DrawRectangleRoundedLinesEx(b, 0.25f, 8, 1, sel ? C_ACC : C_LINE);
            DrawCircle((int)(b.x + 14), (int)(b.y + 16), 5, hexc(sd->types[i].color));
            txt(fUi, sd->types[i].name, b.x + 26, b.y + 8, 14, 0, sel ? C_ACC : C_TEXT);
            if (clicked(b) && !sel) { int on = st->on; slot_defaults(st, slot, i); st->on = on ? on : 1; markDirty(); }
        }
        y += ((sd->ntypes + 1) / 2) * 38 + 8;
    }

    float ky = y + 44;
    for (int i = 0; i < t->nk; i++) {
        float cx = SX + 48 + (i % 3) * 96, cy = ky + (i / 3) * 96;
        editKnob(&st->k[i], &t->k[i], cx, cy, 20, acc);
    }

    if (button((Rectangle){ SX, 664, SW, 34 }, "Voltar este pedal ao padrão", 0)) {
        int on = st->on; slot_defaults(st, slot, st->type); st->on = on; markDirty();
    }
}

static void drawEditAmp(void)
{
    txt(fBold, "AMPLIFICADOR", SX, 112, 13, 1.4f, C_MUTED);
    txt(fBold, AMPS[rig.amp].name, SX, 128, 26, 0.3f, C_TEXT);
    float y = 166;
    y += wrapText(fUi, AMPS[rig.amp].desc, SX, y, SW, 13, 18, C_MUTED) + 12;

    txt(fBold, "MODELO", SX, y, 12.5f, 1.4f, C_MUTED); y += 18;
    for (int i = 0; i < NAMP; i++) {
        Rectangle r = { SX, y, SW, 30 };
        int sel = i == rig.amp, h = hover(r);
        if (sel) DrawRectangleRounded(r, 0.2f, 8, (Color){ 48, 42, 34, 255 });
        else if (h) DrawRectangleRounded(r, 0.2f, 8, C_HOVER);
        Rectangle sw = { r.x + 10, r.y + 8, 22, 14 };
        DrawRectangleRounded(sw, 0.25f, 4, LOOKS[i].tweed ? (Color){ 196, 156, 98, 255 } : (Color){ 20, 20, 22, 255 });
        DrawRectangle((int)sw.x + 3, (int)sw.y + 8, 16, 4, LOOKS[i].panel);
        txt(fUi, AMPS[i].name, r.x + 42, r.y + 7, 15, 0, sel ? C_ACC : C_TEXT);
        if (clicked(r)) { rig.amp = i; markDirty(); }
        y += 32;
    }
    y += 12;
    txt(fBold, "GABINETE", SX, y, 12.5f, 1.4f, C_MUTED); y += 18;
    const char* cl[NCAB] = { "Nenhum", "1x12", "2x12", "4x12" };
    int nc = segmented((Rectangle){ SX, y, SW, 34 }, cl, NCAB, rig.cab, 0);
    if (nc != rig.cab) { rig.cab = nc; markDirty(); }
    y += 42;
    txt(fUi, CABS[rig.cab], SX, y, 14, 0, C_TEXT); y += 20;
    wrapText(fUi, CAB_DESC[rig.cab], SX, y, SW, 13, 18, C_MUTED);

    txt(fUi, "GANHO, GRAVE, MÉDIO, AGUDO, PRESENÇA e MASTER", SX, 646, 12.5f, 0, C_MUTED);
    txt(fUi, "ficam no painel do amplificador.", SX, 664, 12.5f, 0, C_MUTED);
}

static int deviceList(Rectangle L, int isCapture, int sel, float* scroll)
{
    int count = isCapture ? audio_capture_count() : audio_playback_count();
    int rows = count + 1, res = -2;
    float rowH = 26, contentH = rows * rowH;
    DrawRectangleRounded(L, 0.05f, 8, C_FIELD);
    DrawRectangleRoundedLinesEx(L, 0.05f, 8, 1, C_LINE);
    if (hover(L) && wheel != 0) *scroll -= wheel * 26;
    *scroll = clampf(*scroll, 0, fmaxf(0, contentH - L.height + 4));
    Rectangle in = { L.x + 2, L.y + 2, L.width - 4, L.height - 4 };
    clipBegin(in);
    for (int r = 0; r < rows; r++) {
        int idx = r - 1;
        Rectangle row = { in.x, in.y + r * rowH - *scroll, in.width, rowH };
        if (row.y + row.height < in.y || row.y > in.y + in.height) continue;
        int h = hover(row) && hover(in), s = sel == idx;
        if (h) DrawRectangleRec(row, C_HOVER);
        Vector2 c = { row.x + 16, row.y + rowH / 2 };
        DrawRing(c, 5, 6.5f, 0, 360, 24, s ? C_ACC : C_MUTED);
        if (s) DrawCircleV(c, 3, C_ACC);
        const char* name = idx < 0 ? "Padrão do Windows" : (isCapture ? audio_capture_name(idx) : audio_playback_name(idx));
        char nm[300]; fitText(nm, sizeof nm, fUi, name, 14, row.width - 40);
        txt(fUi, nm, row.x + 30, row.y + (rowH - 14) / 2 - 1, 14, 0, s ? C_TEXT : withA(C_TEXT, 0.82f));
        if (h) { cursorWant = 1; if (mPressed) res = idx; }
    }
    EndScissorMode();
    return res;
}

static void hslider(Rectangle r, float* v, float def, float mark)
{
    Rectangle hit = { r.x - 6, r.y - 10, r.width + 12, r.height + 20 };
    int h = hover(hit) && !typing;
    if (h) cursorWant = 1;
    if (h && mPressed && !activeV) {
        double now = GetTime();
        if (lastClickV == v && now - lastClickT < 0.35) { *v = def; lastClickV = NULL; markDirty(); }
        else { activeV = v; lastClickV = v; lastClickT = now; }
    }
    if (activeV == v && mDown) { *v = clampf((M.x - r.x) / r.width, 0, 1); markDirty(); }
    if (h && wheel != 0 && !activeV) { *v = clampf(*v + wheel * 0.02f, 0, 1); markDirty(); }
    DrawRectangleRounded(r, 1, 8, C_FIELD);
    DrawRectangleRounded((Rectangle){ r.x, r.y, r.width * *v, r.height }, 1, 8, C_ACC);
    if (mark >= 0) DrawRectangle((int)(r.x + r.width * mark), (int)(r.y - 3), 1, (int)(r.height + 6), C_MUTED);
    DrawCircleV((Vector2){ r.x + r.width * *v, r.y + r.height / 2 }, 8, (h || activeV == v) ? C_TEXT : C_CREAM);
}

static void drawAudioTab(void)
{
    txt(fBold, "ENTRADA", SX, 112, 13, 1.4f, C_MUTED);
    int r = deviceList((Rectangle){ SX, 130, SW, 100 }, 1, selIn, &scrollIn);
    if (r != -2 && r != selIn) { selIn = r; restartIfRunning(); }

    txt(fBold, "SAÍDA", SX, 242, 13, 1.4f, C_MUTED);
    r = deviceList((Rectangle){ SX, 260, SW, 100 }, 0, selOut, &scrollOut);
    if (r != -2 && r != selOut) { selOut = r; restartIfRunning(); }

    txt(fBold, "NÍVEL DA ENTRADA", SX, 374, 13, 1.4f, C_MUTED);
    char tv[24]; snprintf(tv, sizeof tv, "%+.1f dB", trimDb());
    txtR(fUi, tv, SX + SW, 373, 13, 0, C_TEXT);
    hslider((Rectangle){ SX + 8, 398, SW - 16, 6 }, &trimNorm, 12.0f / 36.0f, 12.0f / 36.0f);
    inTrimDb = trimDb();

    unsigned rate; float buf; int inCh;
    audio_info(&rate, &buf, &inCh);

    txt(fBold, "CANAL DA ENTRADA", SX, 424, 13, 1.4f, C_MUTED);
    const char* ch[2] = { "1 / esquerdo", "2 / direito" };
    int dis = (audio_running() && inCh < 2) ? 2 : 0;
    int nc = segmented((Rectangle){ SX, 442, SW, 32 }, ch, 2, pChan > 1 ? 1 : pChan, dis);
    if (nc != pChan) { pChan = nc; markDirty(); }

    txt(fBold, "MODO", SX, 486, 13, 1.4f, C_MUTED);
    const char* md[2] = { "Normal", "Baixa latência" };
    int nm = segmented((Rectangle){ SX, 504, SW, 32 }, md, 2, wantExcl, 0);
    if (nm != wantExcl) { wantExcl = nm; restartIfRunning(); }
    txt(fUi, "Baixa latência deixa os outros sons do PC mudos", SX, 542, 12, 0, C_MUTED);
    txt(fUi, "enquanto o programa estiver ligado.", SX, 557, 12, 0, C_MUTED);

    Rectangle info = { SX, 580, SW, 62 };
    DrawRectangleRounded(info, 0.12f, 8, C_FIELD);
    char l1[64], l2[64];
    if (audio_running()) {
        snprintf(l1, sizeof l1, "%.1f kHz  ·  %d %s", rate / 1000.0f, inCh, inCh == 1 ? "canal de entrada" : "canais de entrada");
        snprintf(l2, sizeof l2, "Buffer de cerca de %.0f ms  ·  %s", buf, audio_exclusive_active() ? "baixa latência" : "modo normal");
    } else {
        snprintf(l1, sizeof l1, "Áudio desligado");
        snprintf(l2, sizeof l2, "Ligue no botão Power do amplificador.");
    }
    txt(fUi, l1, info.x + 12, info.y + 12, 14, 0, C_TEXT);
    txt(fUi, l2, info.x + 12, info.y + 34, 13, 0, C_MUTED);

    if (button((Rectangle){ SX, 664, SW, 34 }, "Procurar aparelhos de novo", 0)) {
        char inName[256] = "", outName[256] = "";
        if (selIn >= 0) snprintf(inName, sizeof inName, "%s", audio_capture_name(selIn));
        if (selOut >= 0) snprintf(outName, sizeof outName, "%s", audio_playback_name(selOut));
        audio_enumerate();
        selIn = audio_find_capture(inName); selOut = audio_find_playback(outName);
        setBanner("Lista de aparelhos atualizada.", C_MUTED, 2.5f, 0);
    }
}

static void trashIcon(float x, float y, Color c)
{
    DrawRectangle((int)x - 6, (int)y - 6, 12, 2, c);
    DrawRectangle((int)x - 2, (int)y - 8, 4, 2, c);
    DrawRectangleLinesEx((Rectangle){ x - 5, y - 3, 10, 11 }, 1.5f, c);
    DrawRectangle((int)x - 1, (int)y - 1, 1, 7, c);
    DrawRectangle((int)x + 1, (int)y - 1, 1, 7, c);
}

static void drawRecTab(void)
{
    txt(fBold, "GRAVAR", SX, 112, 13, 1.4f, C_MUTED);
    Rectangle rb = { SX, 130, SW, 46 };
    int on = rec_active(), h = hover(rb);
    DrawRectangleRounded(rb, 0.2f, 8, on ? (Color){ 128, 30, 26, 255 } : (h ? C_HOVER : C_FIELD));
    DrawRectangleRoundedLinesEx(rb, 0.2f, 8, 1, on ? C_BAD : C_LINE);
    if (on) {
        DrawRectangle((int)rb.x + 16, (int)rb.y + 17, 12, 12, C_TEXT);
        txt(fUi, "Parar e salvar", rb.x + 40, rb.y + 13, 17, 0, C_TEXT);
        char t[24]; fmtTime(rec_seconds(), t, sizeof t);
        float pulse = 0.55f + 0.45f * sinf((float)GetTime() * 5);
        DrawCircle((int)(rb.x + rb.width - 70), (int)(rb.y + 23), 5, withA((Color){ 255, 120, 110, 255 }, pulse));
        txtR(fUi, t, rb.x + rb.width - 14, rb.y + 13, 17, 0, C_TEXT);
    } else {
        DrawCircle((int)rb.x + 22, (int)rb.y + 23, 7, C_BAD);
        txt(fUi, "Começar a gravar", rb.x + 40, rb.y + 13, 17, 0, C_TEXT);
        txtR(fUi, "tecla R", rb.x + rb.width - 14, rb.y + 15, 13, 0, C_MUTED);
    }
    if (clicked(rb) && !typing) toggleRecord();

    Rectangle cb = { SX, 192, 20, 20 };
    Rectangle cbHit = { SX, 188, SW, 44 };
    int ch = hover(cbHit) && !on;
    DrawRectangleRounded(cb, 0.25f, 6, recDI ? C_ACC : C_FIELD);
    DrawRectangleRoundedLinesEx(cb, 0.25f, 6, 1, recDI ? C_ACC : (ch ? C_MUTED : C_LINE));
    if (recDI) {
        DrawLineEx((Vector2){ cb.x + 5, cb.y + 10 }, (Vector2){ cb.x + 9, cb.y + 14 }, 2.4f, (Color){ 30, 20, 6, 255 });
        DrawLineEx((Vector2){ cb.x + 9, cb.y + 14 }, (Vector2){ cb.x + 15, cb.y + 6 }, 2.4f, (Color){ 30, 20, 6, 255 });
    }
    txt(fUi, "Gravar também a guitarra limpa", SX + 30, 192, 14.5f, 0, on ? C_MUTED : C_TEXT);
    txt(fUi, "Arquivo extra sem efeitos, para trocar o timbre depois.", SX + 30, 212, 12, 0, C_MUTED);
    if (ch) { cursorWant = 1; if (mPressed && !activeV && !typing) { recDI = !recDI; markDirty(); } }
    txt(fUi, "Grava o som que você ouve. O que estiver tocando", SX, 244, 12, 0, C_MUTED);
    txt(fUi, "na reprodução não entra na gravação.", SX, 259, 12, 0, C_MUTED);

    txt(fBold, "SUAS GRAVAÇÕES", SX, 290, 13, 1.4f, C_MUTED);
    Rectangle ob = { SX + SW - 100, 284, 100, 26 };
    {
        int oh = hover(ob);
        DrawRectangleRounded(ob, 0.3f, 8, oh ? C_HOVER : C_FIELD);
        DrawRectangleRoundedLinesEx(ob, 0.3f, 8, 1, C_LINE);
        txtC(fUi, "Abrir pasta", ob.x + ob.width / 2, ob.y + 5, 13, 0, C_TEXT);
        if (clicked(ob)) rec_open_folder();
    }

    Rectangle L = { SX, 318, SW, 280 };
    int n = rec_count();
    const float rowH = 48;
    float contentH = n * rowH;
    if (hover(L) && wheel != 0) scrollRec -= wheel * 40;
    scrollRec = clampf(scrollRec, 0, fmaxf(0, contentH - L.height));
    if (confirmTrash >= 0 && GetTime() > confirmTrashUntil) confirmTrash = -1;
    int playingIt = rec_playing() ? rec_play_item() : -1;
    if (n == 0) {
        txt(fUi, "Nenhuma gravação ainda.", SX + 4, L.y + 10, 14, 0, C_MUTED);
        txt(fUi, "Clique em Começar a gravar ou aperte R.", SX + 4, L.y + 30, 13, 0, C_MUTED);
    }
    clipBegin(L);
    for (int i = 0; i < n; i++) {
        Rectangle r = { SX, L.y + i * rowH - scrollRec, SW, rowH - 4 };
        if (r.y + r.height < L.y || r.y > L.y + L.height) continue;
        int hr = hover(r) && hover(L);
        int pl = i == playingIt, conf = i == confirmTrash;
        DrawRectangleRounded(r, 0.18f, 8, conf ? (Color){ 70, 28, 26, 255 } : (pl ? (Color){ 48, 42, 34, 255 } : (hr ? C_HOVER : C_FIELD)));
        Vector2 pc = { r.x + 22, r.y + r.height / 2 };
        Rectangle pb = { pc.x - 14, pc.y - 14, 28, 28 };
        int hp = hover(pb) && hover(L);
        DrawCircleV(pc, 14, pl ? C_ACC : (hp ? (Color){ 80, 84, 92, 255 } : (Color){ 58, 61, 68, 255 }));
        if (pl) DrawRectangle((int)pc.x - 5, (int)pc.y - 5, 10, 10, (Color){ 30, 20, 6, 255 });
        else tri((Vector2){ pc.x + 6, pc.y }, (Vector2){ pc.x - 4, pc.y - 6 }, (Vector2){ pc.x - 4, pc.y + 6 }, C_TEXT);
        Rectangle tb = { r.x + r.width - 36, r.y + (r.height - 28) / 2, 28, 28 };
        int ht = hover(tb) && hover(L);
        if (conf) {
            txt(fUi, "Clique na lixeira de novo para", r.x + 44, r.y + 6, 13, 0, (Color){ 255, 214, 208, 255 });
            txt(fUi, "mandar para a Lixeira do Windows", r.x + 44, r.y + 23, 13, 0, (Color){ 255, 214, 208, 255 });
        } else {
            char nm[300]; fitText(nm, sizeof nm, fUi, rec_item_name(i), 14, r.width - 90);
            txt(fUi, nm, r.x + 44, r.y + 6, 14, 0, pl ? C_ACC : C_TEXT);
            char d[64], t[24]; fmtTime(rec_item_secs(i), t, sizeof t);
            snprintf(d, sizeof d, "%s%s", t, rec_item_has_di(i) ? "  ·  com versão limpa" : "");
            txt(fUi, d, r.x + 44, r.y + 24, 12, 0, C_MUTED);
        }
        if (hr || conf) {
            if (ht) DrawRectangleRounded(tb, 0.3f, 6, conf ? (Color){ 150, 40, 34, 255 } : (Color){ 60, 63, 70, 255 });
            trashIcon(tb.x + 14, tb.y + 14, conf ? (Color){ 255, 200, 194, 255 } : (ht ? C_TEXT : C_MUTED));
        }
        if (hr) cursorWant = 1;
        if (hr && mPressed && !activeV && !typing) {
            if (ht) {
                if (conf) {
                    if (!rec_trash(i)) setBanner("Não foi possível mandar a gravação para a Lixeira.", C_BAD, 5, 0);
                    else setBanner("Gravação enviada para a Lixeira.", C_MUTED, 3, 0);
                    confirmTrash = -1;
                } else { confirmTrash = i; confirmTrashUntil = GetTime() + 4; }
            } else if (!conf) playItem(i);
        }
    }
    EndScissorMode();
    if (contentH > L.height) {
        float bh = L.height * L.height / contentH, by = L.y + (L.height - bh) * (scrollRec / (contentH - L.height));
        DrawRectangleRounded((Rectangle){ SX + SW - 3, by, 3, bh }, 1, 4, C_LINE);
    }

    Rectangle P = { SX, 608, SW, 92 };
    DrawRectangleRounded(P, 0.12f, 8, C_FIELD);
    int pi = rec_playing() ? rec_play_item() : -1;
    if (pi >= 0) {
        char nm[300]; fitText(nm, sizeof nm, fUi, rec_item_name(pi), 13.5f, P.width - 24);
        txt(fUi, nm, P.x + 12, P.y + 9, 13.5f, 0, C_TEXT);
        double pos = rec_play_pos(), len = rec_play_len();
        float f = len > 0 ? (float)(pos / len) : 0;
        Rectangle bar = { P.x + 12, P.y + 34, P.width - 24, 6 };
        Rectangle barHit = { bar.x, bar.y - 8, bar.width, bar.height + 16 };
        DrawRectangleRounded(bar, 1, 8, (Color){ 46, 48, 54, 255 });
        DrawRectangleRounded((Rectangle){ bar.x, bar.y, bar.width * clampf(f, 0, 1), bar.height }, 1, 8, C_ACC);
        if (hover(barHit)) { cursorWant = 1; if (mPressed && !activeV) rec_play_seek(clampf((M.x - bar.x) / bar.width, 0, 1) * len); }
        char a[24], b[24]; fmtTime(pos, a, sizeof a); fmtTime(len, b, sizeof b);
        txt(fUi, a, P.x + 12, P.y + 44, 12, 0, C_MUTED);
        txtR(fUi, b, P.x + P.width - 12, P.y + 44, 12, 0, C_MUTED);
    } else {
        txt(fUi, "Nada tocando. Clique no play de uma gravação", P.x + 12, P.y + 10, 13, 0, C_MUTED);
        txt(fUi, "para ouvir, e toque junto se quiser.", P.x + 12, P.y + 27, 13, 0, C_MUTED);
    }
    txt(fBold, "VOLUME", P.x + 12, P.y + 66, 12, 1.2f, C_MUTED);
    hslider((Rectangle){ P.x + 78, P.y + 72, P.width - 92, 5 }, &playVolN, 0.8f, -1);
}

static void drawSidebar(void)
{
    DrawRectangle(880, 56, 320, 656, C_PANEL);
    DrawLine(880, 56, 880, 712, C_LINE);
    const char* names[4] = { "PRESETS", "EDITAR", "ÁUDIO", "GRAVAÇÕES" };
    for (int i = 0; i < 4; i++) {
        Rectangle t = { 880 + i * 80.0f, 56, 80.0f, 44 };
        int sel = tab == i, h = hover(t);
        DrawRectangleRec(t, sel ? C_PANEL : (h ? C_HOVER : C_BAR));
        txtC(fBold, names[i], t.x + t.width / 2, t.y + 14, 14, 0.9f, sel ? C_ACC : C_MUTED);
        if (sel) DrawRectangle((int)t.x, (int)(t.y + t.height - 2), (int)t.width + 1, 2, C_ACC);
        else DrawLine((int)t.x, (int)(t.y + t.height - 1), (int)(t.x + t.width + 1), (int)(t.y + t.height - 1), C_LINE);
        if (clicked(t)) { tab = i; if (i != 0) typing = 0; if (i == 3) rec_list(); }
    }
    if (tab == 0) drawPresetsTab();
    else if (tab == 1) { if (selBlock == SEL_AMP) drawEditAmp(); else drawEditSlot(selBlock); }
    else if (tab == 2) drawAudioTab();
    else drawRecTab();
}

/* ---------- barras ---------- */
static void drawTopbar(void)
{
    DrawRectangle(0, 0, VW, 56, C_BAR);
    DrawLine(0, 55, VW, 55, C_LINE);
    DrawCircle(28, 28, 11, C_ACC);
    DrawCircle(28, 28, 7, (Color){ 40, 28, 12, 255 });
    DrawLineEx((Vector2){ 28, 28 }, (Vector2){ 32.5f, 23.5f }, 2, C_ACC);
    txt(fBold, "Guitarra no PC", 48, 15, 24, 0.5f, C_TEXT);

    Rectangle box = { 330, 10, 360, 36 };
    DrawRectangleRounded(box, 0.22f, 8, C_FIELD);
    DrawRectangleRoundedLinesEx(box, 0.22f, 8, 1, C_LINE);
    Rectangle lb = { box.x, box.y, 38, box.height }, rb = { box.x + box.width - 38, box.y, 38, box.height };
    int hl = hover(lb), hr = hover(rb);
    if (hl) DrawRectangleRounded(lb, 0.22f, 8, C_HOVER);
    if (hr) DrawRectangleRounded(rb, 0.22f, 8, C_HOVER);
    float my = box.y + box.height / 2;
    tri((Vector2){ lb.x + 14, my }, (Vector2){ lb.x + 23, my - 6 }, (Vector2){ lb.x + 23, my + 6 }, hl ? C_TEXT : C_MUTED);
    tri((Vector2){ rb.x + 24, my }, (Vector2){ rb.x + 15, my - 6 }, (Vector2){ rb.x + 15, my + 6 }, hr ? C_TEXT : C_MUTED);
    if (clicked(lb) && !typing) applyPreset((curPreset + nPresets - 1) % nPresets);
    if (clicked(rb) && !typing) applyPreset((curPreset + 1) % nPresets);
    int mod = presetModified();
    char nm[96]; fitText(nm, sizeof nm, fUi, presets[curPreset].name, 16, box.width - 110);
    float nw = tw(fUi, nm, 16, 0) + (mod ? 14 : 0);
    float nx = box.x + (box.width - nw) / 2;
    txt(fUi, nm, nx, box.y + 9, 16, 0, C_TEXT);
    if (mod) DrawCircle((int)(nx + nw - 4), (int)(box.y + 18), 3, C_ACC);
    if (clicked((Rectangle){ lb.x + lb.width, box.y, box.width - 76, box.height })) tab = 0;

    if (button((Rectangle){ 698, 10, 76, 36 }, "Salvar", mod ? 1 : 0) && !typing) saveCurrent();

    {   /* botão gravar */
        Rectangle r = { 786, 10, 132, 36 };
        int on = rec_active(), h = hover(r);
        DrawRectangleRounded(r, 0.22f, 8, on ? (Color){ 128, 30, 26, 255 } : (h ? C_HOVER : C_FIELD));
        DrawRectangleRoundedLinesEx(r, 0.22f, 8, 1, on ? C_BAD : C_LINE);
        if (on) {
            DrawRectangle((int)r.x + 14, (int)r.y + 13, 10, 10, C_TEXT);
            char t[24]; fmtTime(rec_seconds(), t, sizeof t);
            txt(fUi, t, r.x + 34, r.y + 9, 16, 0, C_TEXT);
            float pulse = 0.55f + 0.45f * sinf((float)GetTime() * 5);
            DrawCircle((int)(r.x + r.width - 16), (int)(r.y + 18), 5, withA((Color){ 255, 120, 110, 255 }, pulse));
        } else {
            DrawCircle((int)r.x + 20, (int)r.y + 18, 6, C_BAD);
            txt(fUi, "Gravar", r.x + 34, r.y + 9, 16, 0, C_TEXT);
        }
        if (clicked(r) && !typing) toggleRecord();
    }

    int run = audio_running();
    const char* st = run ? "Áudio ligado" : (bannerIsError && banner[0] ? "Erro no áudio" : "Áudio desligado");
    Color sc = run ? C_OK : (bannerIsError && banner[0] ? C_BAD : C_MUTED);
    float sw = tw(fUi, st, 14, 0) + 30;
    Rectangle pill = { 1066 - sw - 12, 16, sw, 24 };
    DrawRectangleRounded(pill, 0.5f, 10, mixc(C_BAR, sc, 0.14f));
    DrawCircle((int)(pill.x + 13), (int)(pill.y + 12), 4, sc);
    txt(fUi, st, pill.x + 23, pill.y + 4, 14, 0, run ? C_TEXT : C_MUTED);
    if (button((Rectangle){ 1066, 10, 118, 36 }, run ? "Desligar" : "Ligar", run ? 0 : 1) && !typing) togglePower();
}

static void drawBottombar(void)
{
    DrawRectangle(0, 712, VW, 48, C_BAR);
    DrawLine(0, 712, VW, 712, C_LINE);
    txt(fBold, "ENTRADA", 20, 722, 12, 1.4f, C_MUTED);
    meter((Rectangle){ 20, 740, 210, 8 }, lvIn, pkIn);
    int clip = GetTime() < clipUntil;
    Rectangle cr = { 242, 728, 48, 22 };
    DrawRectangleRounded(cr, 0.3f, 6, clip ? C_BAD : (Color){ 52, 28, 26, 255 });
    txtC(fBold, "CLIP", cr.x + cr.width / 2, cr.y + 4, 13, 1.4f, clip ? (Color){ 30, 6, 4, 255 } : (Color){ 112, 60, 54, 255 });

    unsigned rate; float buf; int inCh;
    audio_info(&rate, &buf, &inCh);
    char info[160];
    if (audio_running())
        snprintf(info, sizeof info, "%.1f kHz   ·   buffer de cerca de %.0f ms   ·   %s", rate / 1000.0f, buf, audio_exclusive_active() ? "baixa latência" : "modo normal");
    else snprintf(info, sizeof info, "Áudio desligado   ·   Espaço liga e desliga");
    if (rec_active()) { char t[24]; fmtTime(rec_seconds(), t, sizeof t); snprintf(info, sizeof info, "Gravando   %s   ·   R para parar e salvar", t); }
    txtC(fUi, info, 600, 728, 13.5f, 0, C_MUTED);

    txtR(fBold, "SAÍDA", 1180, 722, 12, 1.4f, C_MUTED);
    meter((Rectangle){ 970, 740, 210, 8 }, lvOut, pkOut);
}

/* ---------- laço principal ---------- */
static void updateLevels(void)
{
    float in, out; audio_peaks(&in, &out);
    double now = GetTime();
    float din = 20 * log10f(in > 1e-5f ? in : 1e-5f), dout = 20 * log10f(out > 1e-5f ? out : 1e-5f);
    lvIn = din > lvIn ? din : lvIn - 1.4f; if (lvIn < -60) lvIn = -60;
    lvOut = dout > lvOut ? dout : lvOut - 1.4f; if (lvOut < -60) lvOut = -60;
    if (din >= pkIn || now > pkInT) { pkIn = din < -60 ? -60 : din; pkInT = now + 1.0; }
    if (dout >= pkOut || now > pkOutT) { pkOut = dout < -60 ? -60 : dout; pkOutT = now + 1.0; }
    if (in >= 0.99f || out >= 0.99f) clipUntil = now + 1.2;
}

static void handleTyping(void)
{
    if (!typing) return;
    int c;
    while ((c = GetCharPressed()) > 0) {
        int sz = 0; const char* u = CodepointToUTF8(c, &sz);
        if (c >= 32 && nameLen + sz < 48) { memcpy(nameBuf + nameLen, u, sz); nameLen += sz; nameBuf[nameLen] = 0; }
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        while (nameLen > 0) { nameLen--; if ((nameBuf[nameLen] & 0xC0) != 0x80) break; }
        nameBuf[nameLen] = 0;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) confirmNewPreset();
    if (IsKeyPressed(KEY_ESCAPE)) typing = 0;
}

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(VW, VH, "Guitarra no PC");
    SetExitKey(KEY_NULL);
    {
        int mon = GetCurrentMonitor();
        int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
        if (mw > 0 && mh > 0) {
            float s = fminf(1.0f, fminf(mw * 0.92f / VW, mh * 0.86f / VH));
            int w = (int)(VW * s), h = (int)(VH * s);
            SetWindowSize(w, h);
            SetWindowPosition((mw - w) / 2, (mh - h) / 2);
        }
        SetWindowMinSize(800, 507);
    }
    SetTargetFPS(60);
    {   /* ícone da janela e da barra de tarefas */
        Image ics[5] = {
            LoadImageFromMemory(".png", icon_png_16, sizeof icon_png_16),
            LoadImageFromMemory(".png", icon_png_32, sizeof icon_png_32),
            LoadImageFromMemory(".png", icon_png_48, sizeof icon_png_48),
            LoadImageFromMemory(".png", icon_png_64, sizeof icon_png_64),
            LoadImageFromMemory(".png", icon_png_256, sizeof icon_png_256),
        };
        for (int i = 0; i < 5; i++) ImageFormat(&ics[i], PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        SetWindowIcons(ics, 5);
        for (int i = 0; i < 5; i++) UnloadImage(ics[i]);
    }

    int cps[256]; int ncp = 0;
    for (int c = 32; c < 127; c++) cps[ncp++] = c;
    for (int c = 160; c < 256; c++) cps[ncp++] = c;
    cps[ncp++] = 0x2026; cps[ncp++] = 0x2014; cps[ncp++] = 0x2022;
    fUi = LoadFontFromMemory(".ttf", font_ui, sizeof font_ui, 36, cps, ncp);
    fBold = LoadFontFromMemory(".ttf", font_bold, sizeof font_bold, 52, cps, ncp);
    fScript = LoadFontFromMemory(".ttf", font_script, sizeof font_script, 168, cps, ncp);
    SetTextureFilter(fUi.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(fBold.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(fScript.texture, TEXTURE_FILTER_BILINEAR);

    makeTextures();
    rt = LoadRenderTexture(VW * SS, VH * SS);

    char dir[520]; app_dir(dir, sizeof dir);
    snprintf(iniPath, sizeof iniPath, "%sguitarra.ini", dir);
    snprintf(presetPath, sizeof presetPath, "%spresets.txt", dir);

    if (!audio_init()) setBanner("Não foi possível acessar o sistema de áudio do Windows.", C_BAD, 0, 1);
    rec_init();
    loadPresets();
    rig = presets[0].rig;
    char inName[256] = "", outName[256] = "", presetName[64] = "";
    loadIni(inName, outName, presetName);
    inTrimDb = trimDb();
    selIn = audio_find_capture(inName); selOut = audio_find_playback(outName);
    for (int i = 0; i < nPresets; i++) if (!strcmp(presets[i].name, presetName)) curPreset = i;
    if (inName[0] && selIn < 0) setBanner("A entrada usada da última vez não foi encontrada. Usando a padrão do Windows.", C_WARN, 7, 0);

    const char* shot = getenv("GNP_SHOT");
    if (getenv("GNP_TAB")) tab = atoi(getenv("GNP_TAB"));
    if (getenv("GNP_PRESET")) applyPreset(atoi(getenv("GNP_PRESET")));
    if (getenv("GNP_SEL")) selBlock = atoi(getenv("GNP_SEL"));
    startAudio();
    int frame = 0;

    while (!WindowShouldClose()) {
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        float sc = fminf((float)sw / VW, (float)sh / VH);
        float ox = (sw - VW * sc) / 2, oy = (sh - VH * sc) / 2;
        Vector2 mp = GetMousePosition();
        M = (Vector2){ (mp.x - ox) / sc, (mp.y - oy) / sc };
        if (getenv("GNP_MX")) M = (Vector2){ (float)atof(getenv("GNP_MX")), (float)atof(getenv("GNP_MY")) };
        mDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        mPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        mReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
        wheel = GetMouseWheelMove();
        cursorWant = 0;

        if (activeV && !mDown) activeV = NULL;
        handleTyping();
        if (!typing) {
            if (IsKeyPressed(KEY_SPACE)) togglePower();
            if (IsKeyPressed(KEY_R)) toggleRecord();
            if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_S)) saveCurrent();
        }
        if (audio_lost()) { finishRecording("O aparelho parou, a gravação foi salva:"); rec_play_stop(1); audio_stop(); setBanner("O aparelho de áudio parou ou foi desconectado. Confira a aba Áudio e ligue de novo.", C_BAD, 0, 1); }
        updateLevels();
        rec_set_play_vol(playVolN);

        BeginTextureMode(rt);
        BeginMode2D((Camera2D){ .zoom = SS });
        ClearBackground(C_BG);
        radial(440, 250, 540, (Color){ 46, 48, 56, 255 }, (Color){ 17, 18, 21, 0 });
        drawAmp();
        drawAmpCable();
        drawBoard();
        drawBanner();
        drawSidebar();
        drawTopbar();
        drawBottombar();
        EndMode2D();
        EndTextureMode();
        GenTextureMipmaps(&rt.texture);
        SetTextureFilter(rt.texture, TEXTURE_FILTER_TRILINEAR);

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(rt.texture, (Rectangle){ 0, 0, (float)rt.texture.width, -(float)rt.texture.height },
                       (Rectangle){ ox, oy, VW * sc, VH * sc }, (Vector2){ 0, 0 }, 0, WHITE);
        EndDrawing();

        static int lastCursor = -1;
        if (cursorWant != lastCursor) {
            SetMouseCursor(cursorWant == 1 ? MOUSE_CURSOR_POINTING_HAND : (cursorWant == 2 ? MOUSE_CURSOR_RESIZE_NS : MOUSE_CURSOR_DEFAULT));
            lastCursor = cursorWant;
        }
        if (dirty && !mDown && GetTime() - dirtyAt > 0.6) saveIni();
        frame++;
        if (shot && frame == 30) { TakeScreenshot(shot); break; }
    }

    if (dirty) saveIni();
    audio_shutdown();
    rec_shutdown();
    UnloadRenderTexture(rt);
    CloseWindow();
    return 0;
}
