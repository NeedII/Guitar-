/* Guitarra no PC — motor de efeitos e amplificadores */
#include "dsp.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <xmmintrin.h>
#define HAVE_SSE 1
#endif

#define PI_F 3.14159265358979f

/* =================== tabelas de efeitos =================== */
static const FxType T_GATE[] = {
    { "Noise Gate", "GATE", "Corta o chiado quando você para de tocar. Suba o limiar até o chiado sumir sem cortar o fim das notas.",
      2, { { "LIMIAR", 0.36f, F_DB, -90, -20 }, { "SOLTURA", 0.40f, F_MS, 10, 500 } }, 0x5C626Bu },
};
static const FxType T_COMP[] = {
    { "Compressor", "COMP", "Iguala o volume das notas e aumenta a sustentação. Essencial no funk e nos limpos.",
      5, { { "SUSTAIN", 0.5f, F_PCT, 0, 1 }, { "TAXA", 0.35f, F_RATIO, 2, 20 }, { "ATAQUE", 0.3f, F_MS, 1, 60 },
           { "VOLUME", 0.35f, F_DB, 0, 24 }, { "MIX", 1.0f, F_PCT, 0, 1 } }, 0x2F6FD0u },
};
static const FxType T_FILTER[] = {
    { "Wah fixo", "WAH", "Wah parado numa posição, como o pedal meio pisado. Som anasalado e cortante para solos.",
      3, { { "POSIÇÃO", 0.5f, F_HZ, 350, 2400 }, { "RESSON.", 0.5f, F_PCT, 0, 1 }, { "MIX", 1.0f, F_PCT, 0, 1 } }, 0x2B2B30u },
    { "Auto-wah", "AUTO-WAH", "O filtro abre conforme a força da palhetada. Clássico do funk.",
      4, { { "SENSIB.", 0.5f, F_PCT, 0, 1 }, { "FAIXA", 0.6f, F_HZ, 800, 4000 }, { "RESSON.", 0.55f, F_PCT, 0, 1 }, { "MIX", 1.0f, F_PCT, 0, 1 } }, 0x7A3FB0u },
};
static const FxType T_DRIVE[] = {
    { "Overdrive", "OVERDRIVE", "Saturação suave com médios na frente. Com pouco drive e nível alto ele empurra o amplificador, como no blues texano.",
      3, { { "DRIVE", 0.3f, F_DIAL, 0, 1 }, { "TOM", 0.5f, F_DIAL, 0, 1 }, { "NÍVEL", 0.6f, F_DIAL, 0, 1 } }, 0x3FA34Du },
    { "Distorção", "DISTORÇÃO", "Distorção pesada e cortante para riffs de rock.",
      3, { { "DIST", 0.5f, F_DIAL, 0, 1 }, { "TOM", 0.5f, F_DIAL, 0, 1 }, { "NÍVEL", 0.5f, F_DIAL, 0, 1 } }, 0xE07A1Fu },
    { "Fuzz", "FUZZ", "Fuzz gordo e com muita sustentação, bom para solos sujos.",
      3, { { "SUSTAIN", 0.6f, F_DIAL, 0, 1 }, { "TOM", 0.5f, F_DIAL, 0, 1 }, { "NÍVEL", 0.5f, F_DIAL, 0, 1 } }, 0xA8322Cu },
    { "Boost", "BOOST", "Aumenta volume e ganho do amplificador sem mudar muito o timbre. Bom para destacar solos.",
      2, { { "GANHO", 0.4f, F_DB, 0, 20 }, { "BRILHO", 0.3f, F_PCT, 0, 1 } }, 0xC9CED6u },
};
static const FxType T_EQ[] = {
    { "EQ gráfico", "EQ", "Sete bandas para esculpir o timbre. Com o knob no meio a banda não muda nada.",
      8, { { "100", 0.5f, F_DB, -15, 15 }, { "200", 0.5f, F_DB, -15, 15 }, { "400", 0.5f, F_DB, -15, 15 }, { "800", 0.5f, F_DB, -15, 15 },
           { "1.6k", 0.5f, F_DB, -15, 15 }, { "3.2k", 0.5f, F_DB, -15, 15 }, { "6.4k", 0.5f, F_DB, -15, 15 }, { "NÍVEL", 0.5f, F_DB, -12, 12 } }, 0xE4E6EAu },
};
static const FxType T_MOD[] = {
    { "Chorus", "CHORUS", "Dobra o som com uma leve oscilação, deixando o limpo mais largo e brilhante.",
      3, { { "VELOC.", 0.3f, F_HZ, 0.1f, 5 }, { "PROFUND.", 0.5f, F_PCT, 0, 1 }, { "MIX", 0.5f, F_PCT, 0, 1 } }, 0x2BB3B1u },
    { "Flanger", "FLANGER", "Efeito de jato que sobe e desce. Fica ótimo em riffs e limpos.",
      4, { { "VELOC.", 0.2f, F_HZ, 0.05f, 2 }, { "PROFUND.", 0.6f, F_PCT, 0, 1 }, { "REALIM.", 0.5f, F_PCT, 0, 1 }, { "MIX", 0.5f, F_PCT, 0, 1 } }, 0x6B4FBFu },
    { "Phaser", "PHASER", "Ondulação macia que gira no som. Funciona bem em funk e solos.",
      4, { { "VELOC.", 0.3f, F_HZ, 0.1f, 6 }, { "PROFUND.", 0.7f, F_PCT, 0, 1 }, { "REALIM.", 0.3f, F_PCT, 0, 1 }, { "MIX", 0.5f, F_PCT, 0, 1 } }, 0xF08A24u },
    { "Tremolo", "TREMOLO", "Volume pulsando no ritmo. Som de amplificador antigo.",
      3, { { "VELOC.", 0.45f, F_HZ, 1, 12 }, { "PROFUND.", 0.5f, F_PCT, 0, 1 }, { "FORMA", 0.2f, F_PCT, 0, 1 } }, 0xE8C33Au },
};
static const FxType T_DELAY[] = {
    { "Delay digital", "DELAY", "Repetições limpas e definidas.",
      4, { { "TEMPO", 0.55f, F_MS, 40, 1500 }, { "REPET.", 0.3f, F_PCT, 0, 1 }, { "MIX", 0.25f, F_PCT, 0, 1 }, { "TOM", 0.8f, F_PCT, 0, 1 } }, 0xDDE3EAu },
    { "Delay analógico", "ANALÓGICO", "Repetições mais escuras e quentes, que se misturam ao som. Ótimo para solos.",
      4, { { "TEMPO", 0.5f, F_MS, 40, 1500 }, { "REPET.", 0.35f, F_PCT, 0, 1 }, { "MIX", 0.25f, F_PCT, 0, 1 }, { "TOM", 0.35f, F_PCT, 0, 1 } }, 0x2F5D3Au },
};
static const FxType T_REVERB[] = {
    { "Reverb sala", "SALA", "Ambiente curto e natural, como tocar numa sala.",
      4, { { "DECAIM.", 0.4f, F_PCT, 0, 1 }, { "TOM", 0.5f, F_PCT, 0, 1 }, { "MIX", 0.2f, F_PCT, 0, 1 }, { "PRÉ-DELAY", 0.1f, F_MSLIN, 0, 120 } }, 0x3D8FD9u },
    { "Reverb hall", "HALL", "Reverb longo e amplo, de salão grande. Bom para solos e baladas.",
      4, { { "DECAIM.", 0.6f, F_PCT, 0, 1 }, { "TOM", 0.5f, F_PCT, 0, 1 }, { "MIX", 0.25f, F_PCT, 0, 1 }, { "PRÉ-DELAY", 0.25f, F_MSLIN, 0, 120 } }, 0x2B4C9Cu },
    { "Reverb mola", "MOLA", "Reverb de mola, aquele dos amplificadores antigos. Combina com blues e surf.",
      4, { { "DECAIM.", 0.4f, F_PCT, 0, 1 }, { "TOM", 0.45f, F_PCT, 0, 1 }, { "MIX", 0.25f, F_PCT, 0, 1 }, { "PRÉ-DELAY", 0.05f, F_MSLIN, 0, 120 } }, 0x9C6B2Eu },
};

const SlotDef SLOTS[NSLOT] = {
    { "GATE", 1, T_GATE }, { "DINÂMICA", 1, T_COMP }, { "FILTRO", 2, T_FILTER }, { "DRIVE", 4, T_DRIVE },
    { "EQ", 1, T_EQ }, { "MODULAÇÃO", 4, T_MOD }, { "DELAY", 2, T_DELAY }, { "REVERB", 3, T_REVERB },
};

const AmpInfo AMPS[NAMP] = {
    { "Americano Limpo",  "Americano", "Limpo e cristalino, com muita folga. Base para blues, funk e som limpo." },
    { "Tweed Blues",      "Tweed",     "Quebra cedo e tem médios gordos. Responde à força da palhetada, ótimo para blues." },
    { "Britânico Crunch", "Britânico", "Crunch de rock clássico com médios fortes. É a base do som do AC/DC." },
    { "Britânico Hot-Rod","Hot-Rod",   "Britânico modificado, com mais ganho e sustentação. Hard rock e solos no estilo do Slash." },
    { "Moderno",          "Moderno",   "Alto ganho com graves firmes. Para rock pesado e metal." },
};
const char* const CABS[NCAB] = { "Sem gabinete", "1x12 aberto", "2x12", "4x12 fechado" };
const char* const CAB_DESC[NCAB] = {
    "Sinal direto do amplificador. Use só se for mandar para outro simulador.",
    "Aberto atrás: som solto e brilhante, com menos grave.",
    "Equilibrado, com corpo e definição.",
    "Grave cheio e médios agressivos. O som clássico de rock.",
};
const ParamDef AMPK[6] = {
    { "GANHO", 0.5f, F_DIAL, 0, 1 }, { "GRAVE", 0.5f, F_DIAL, 0, 1 }, { "MÉDIO", 0.5f, F_DIAL, 0, 1 },
    { "AGUDO", 0.5f, F_DIAL, 0, 1 }, { "PRESENÇA", 0.5f, F_DIAL, 0, 1 }, { "MASTER", 0.5f, F_DIAL, 0, 1 },
};

Rig rig;
volatile float inTrimDb = 0.0f;

float param_value(const ParamDef* p, float v)
{
    if (v < 0) v = 0; if (v > 1) v = 1;
    switch (p->fmt) {
    case F_HZ: case F_MS: return p->a * powf(p->b / p->a, v);
    case F_DIAL: return v * 10.0f;
    default: return p->a + (p->b - p->a) * v;
    }
}

void param_format(const ParamDef* p, float v, char* b, int n)
{
    float x = param_value(p, v);
    switch (p->fmt) {
    case F_PCT: snprintf(b, n, "%.0f%%", v * 100); break;
    case F_DIAL: snprintf(b, n, "%.1f", x); break;
    case F_DB: snprintf(b, n, "%+.1f dB", x); break;
    case F_HZ: if (x >= 1000) snprintf(b, n, "%.1f kHz", x / 1000); else if (x >= 10) snprintf(b, n, "%.0f Hz", x); else snprintf(b, n, "%.2f Hz", x); break;
    case F_MS: case F_MSLIN: snprintf(b, n, "%.0f ms", x); break;
    case F_RATIO: snprintf(b, n, "%.1f:1", x); break;
    default: snprintf(b, n, "%.2f", x);
    }
}

void slot_defaults(SlotState* s, int slot, int type)
{
    if (type < 0 || type >= SLOTS[slot].ntypes) type = 0;
    const FxType* t = &SLOTS[slot].types[type];
    s->type = type;
    for (int i = 0; i < MAXK; i++) s->k[i] = i < t->nk ? t->k[i].def : 0.5f;
}

void rig_defaults(Rig* r)
{
    memset(r, 0, sizeof *r);
    r->amp = 0; r->cab = 2;
    for (int i = 0; i < 6; i++) r->ak[i] = AMPK[i].def;
    r->ak[0] = 0.3f;
    for (int s = 0; s < NSLOT; s++) { slot_defaults(&r->s[s], s, 0); r->s[s].on = 0; }
}

/* =================== blocos básicos =================== */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } Biquad;
enum { BQ_LP, BQ_HP, BQ_BP, BQ_PEAK, BQ_LS, BQ_HS };

static float FS = 48000.0f;

static inline float bq(Biquad* f, float x)
{
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}

static void bqSet(Biquad* f, int type, float fs, float f0, float Q, float dB)
{
    if (f0 > fs * 0.45f) f0 = fs * 0.45f;
    if (f0 < 5) f0 = 5;
    float A = powf(10.0f, dB / 40.0f), w = 2 * PI_F * f0 / fs, cw = cosf(w), sw = sinf(w), al = sw / (2 * Q);
    float b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type) {
    case BQ_LP: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    case BQ_HP: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    case BQ_BP: b0 = al; b1 = 0; b2 = -al; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    case BQ_PEAK: b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
    case BQ_LS: { float s = 2 * sqrtf(A) * al;
        b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s);
        a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s; } break;
    case BQ_HS: { float s = 2 * sqrtf(A) * al;
        b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
        a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s; } break;
    }
    f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0; f->a1 = a1 / a0; f->a2 = a2 / a0;
}

typedef struct { float a, z; } OnePole;
static void opSet(OnePole* p, float fs, float fc) { if (fc > fs * 0.45f) fc = fs * 0.45f; p->a = 1.0f - expf(-2 * PI_F * fc / fs); }
static inline float opLP(OnePole* p, float x) { p->z += p->a * (x - p->z); return p->z; }
static inline float opHP(OnePole* p, float x) { p->z += p->a * (x - p->z); return x - p->z; }

static inline float db2lin(float d) { return powf(10.0f, d / 20.0f); }
static inline float smooth(float cur, float target, float k) { return cur + (target - cur) * k; }

/* sobreamostragem 4x (Butterworth de 8ª ordem) */
typedef struct { Biquad up[4], dn[4]; } OS4;
static void osInit(OS4* o)
{
    static const float Q[4] = { 0.5098f, 0.6013f, 0.9000f, 2.5629f };
    float fsu = FS * 4, fc = FS * 0.38f;
    if (fc > 18000) fc = 18000;
    for (int i = 0; i < 4; i++) { bqSet(&o->up[i], BQ_LP, fsu, fc, Q[i], 0); bqSet(&o->dn[i], BQ_LP, fsu, fc, Q[i], 0); }
}
static inline float osUp(OS4* o, float u) { for (int i = 0; i < 4; i++) u = bq(&o->up[i], u); return u; }
static inline float osDn(OS4* o, float u) { for (int i = 0; i < 4; i++) u = bq(&o->dn[i], u); return u; }

typedef struct { float* buf; int mask, w; } DLine;
static void dlAlloc(DLine* d, int minLen)
{
    int n = 1; while (n < minLen) n <<= 1;
    free(d->buf);
    d->buf = (float*)calloc((size_t)n, sizeof(float));
    d->mask = n - 1; d->w = 0;
}
static void dlClear(DLine* d) { if (d->buf) memset(d->buf, 0, sizeof(float) * (d->mask + 1)); d->w = 0; }
static inline void dlWrite(DLine* d, float x) { d->buf[d->w] = x; d->w = (d->w + 1) & d->mask; }
static inline float dlRead(DLine* d, float delay)
{
    float p = (float)d->w - 1.0f - delay;
    int i = (int)floorf(p); float f = p - (float)i;
    float a = d->buf[i & d->mask], b = d->buf[(i + 1) & d->mask];
    return a + (b - a) * f;
}

static inline float tube(float x, float bias) { return tanhf(x + bias) - tanhf(bias); }

/* =================== estado dos efeitos =================== */
static Rig snap, applied;
static int appliedValid = 0;

/* gate */
static struct { float env, g; int open; } gate;
/* compressor */
static struct { float env, gsm; } comp;
/* filtro */
static struct { float ic1, ic2, env; } wah;
/* drive */
static struct { OS4 os, os2; Biquad pre, pre2, tone, tone2, post; OnePole lp1, lp2; float gsm, lsm, nenv, ng, natk, nrel; } drv;
/* amp */
static struct {
    OS4 os; Biquad inHp, bright, bass, mid, treb, voice, pres;
    OnePole ihp[3], ilp[3], dc;
    float g1, gS, master, g1t, gSt, mastert;
} amp;
static struct { Biquad f[8]; int n; float trim; } cab;
/* eq */
static Biquad eqf[7]; static float eqLevel = 1;
/* modulação */
static struct { DLine dl; float ph, fb, apx[6], apy[6], last; } mod;
/* delay */
static struct { DLine dl; float dsm; OnePole tone; float ph; } dly;
/* reverb */
#define RV_COMBS 8
#define RV_APS 4
static const int RV_CT[RV_COMBS] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int RV_AT[RV_APS] = { 556, 441, 341, 225 };
static struct {
    float* comb[RV_COMBS]; int clen[RV_COMBS], cidx[RV_COMBS]; float cstore[RV_COMBS];
    float* ap[RV_APS]; int alen[RV_APS], aidx[RV_APS];
    DLine pre; DLine sp[3]; Biquad shp, slp; int type;
    float fb, damp;
} rv;

/* =================== configuração por bloco =================== */
static void setupGateComp(void) {}

static void setupDrive(const SlotState* s)
{
    osInit(&drv.os); osInit(&drv.os2);
    drv.natk = 1.0f - expf(-1.0f / (0.0005f * FS));
    drv.nrel = 1.0f - expf(-1.0f / (0.12f * FS));
    if (drv.ng <= 0) drv.ng = 1;
    float tone = s->k[1];
    switch (s->type) {
    case 0: /* overdrive */
        bqSet(&drv.pre, BQ_HP, FS, 720, 0.5f, 0);
        bqSet(&drv.tone, BQ_LP, FS, 800 * powf(10.0f, tone), 0.707f, 0);
        bqSet(&drv.tone2, BQ_LP, FS, 9000, 0.707f, 0);
        break;
    case 1: /* distorção */
        bqSet(&drv.pre, BQ_HP, FS, 70, 0.707f, 0);
        bqSet(&drv.pre2, BQ_LP, FS, 5500, 0.707f, 0);
        bqSet(&drv.tone, BQ_LP, FS, 600 * powf(15.0f, tone), 0.707f, 0);
        bqSet(&drv.tone2, BQ_LP, FS, 10000, 0.707f, 0);
        break;
    case 2: /* fuzz */
        bqSet(&drv.pre, BQ_HP, FS, 40, 0.707f, 0);
        opSet(&drv.lp1, FS * 4, 2200);
        opSet(&drv.lp2, FS * 4, 4500);
        bqSet(&drv.tone, BQ_LP, FS, 500, 0.707f, 0);
        bqSet(&drv.tone2, BQ_HP, FS, 1100, 0.707f, 0);
        bqSet(&drv.pre2, BQ_LP, FS, 5000, 0.707f, 0);
        bqSet(&drv.post, BQ_LP, FS, 6500, 0.707f, 0);
        break;
    case 3: /* boost */
        bqSet(&drv.pre, BQ_HS, FS, 1500, 0.707f, s->k[1] * 9);
        bqSet(&drv.pre2, BQ_HP, FS, 50, 0.707f, 0);
        break;
    }
}

typedef struct {
    float inHp, brightDb, minDb, maxDb; int stages; float bias, interHp, interLp;
    float voiceHz, voiceDb, voiceQ, bassHz, midHz, midQ, trebHz, power, stageDb;
} AmpVoice;
static const AmpVoice VOICES[NAMP] = {
    /*               inHp br min max st bias  iHp  iLp  vHz  vDb vQ    bass mid  mQ    treb  pow  stageDb */
    /* Americano */ {  60, 5,  0, 22, 1, 0.10f,   0, 9000, 450, -5, 0.8f, 100, 500, 0.7f, 3000, 0.7f,  0 },
    /* Tweed     */ {  70, 2,  0, 30, 2, 0.25f, 120, 6000, 700,  4, 0.7f, 120, 700, 0.7f, 2500, 2.0f,  6 },
    /* Britânico */ {  90, 5,  2, 34, 2, 0.20f, 160, 7000, 900,  3, 0.8f, 110, 750, 0.8f, 3200, 1.6f,  8 },
    /* Hot-Rod   */ { 100, 3,  0, 34, 3, 0.18f, 300, 6500, 800,  3, 0.9f, 110, 750, 0.8f, 3200, 1.4f,  8 },
    /* Moderno   */ { 110, 2,  4, 42, 3, 0.12f, 500, 6000, 600, -3, 0.8f,  90, 600, 0.8f, 3000, 1.2f, 12 },
};
static float AMP_TRIM[NAMP] = { 1.8864f, 1.0835f, 0.7831f, 0.6504f, 0.5634f };
static float CAB_TRIM[NCAB] = { 1.0000f, 0.8521f, 0.8493f, 0.8372f };

static void setupAmp(const Rig* r)
{
    const AmpVoice* v = &VOICES[r->amp];
    float g = r->ak[0];
    osInit(&amp.os);
    bqSet(&amp.inHp, BQ_HP, FS, v->inHp, 0.707f, 0);
    opSet(&amp.dc, FS, 12);
    bqSet(&amp.bright, BQ_HS, FS, 2500, 0.707f, v->brightDb * (1 - g));
    for (int i = 0; i < 3; i++) {
        opSet(&amp.ihp[i], FS * 4, v->interHp > 0 ? v->interHp : 10);
        opSet(&amp.ilp[i], FS * 4, v->interLp);
    }
    bqSet(&amp.bass, BQ_LS, FS, v->bassHz, 0.707f, (r->ak[1] * 2 - 1) * 12);
    bqSet(&amp.mid, BQ_PEAK, FS, v->midHz, v->midQ, (r->ak[2] * 2 - 1) * 10);
    bqSet(&amp.treb, BQ_HS, FS, v->trebHz, 0.707f, (r->ak[3] * 2 - 1) * 12);
    bqSet(&amp.voice, BQ_PEAK, FS, v->voiceHz, v->voiceQ, v->voiceDb);
    bqSet(&amp.pres, BQ_HS, FS, 4500, 0.707f, (r->ak[4] * 2 - 1) * 8);
    float total = v->minDb + g * (v->maxDb - v->minDb);
    amp.g1t = db2lin(total + 3);
    amp.gSt = db2lin(v->stageDb);
    amp.mastert = db2lin(-24 + r->ak[5] * 30) * AMP_TRIM[r->amp];
}

static void setupCab(int c)
{
    cab.n = 0;
    Biquad* f = cab.f;
    switch (c) {
    case 1: /* 1x12 aberto */
        bqSet(&f[cab.n++], BQ_HP, FS, 90, 0.707f, 0);
        bqSet(&f[cab.n++], BQ_PEAK, FS, 130, 1.0f, 2);
        bqSet(&f[cab.n++], BQ_PEAK, FS, 2200, 1.2f, 3);
        bqSet(&f[cab.n++], BQ_LP, FS, 5600, 0.54f, 0);
        bqSet(&f[cab.n++], BQ_LP, FS, 5600, 1.31f, 0);
        break;
    case 2: /* 2x12 */
        bqSet(&f[cab.n++], BQ_HP, FS, 75, 0.8f, 0);
        bqSet(&f[cab.n++], BQ_PEAK, FS, 110, 1.1f, 3);
        bqSet(&f[cab.n++], BQ_PEAK, FS, 500, 1.0f, -2);
        bqSet(&f[cab.n++], BQ_PEAK, FS, 2500, 1.4f, 4);
        bqSet(&f[cab.n++], BQ_LP, FS, 5000, 0.54f, 0);
        bqSet(&f[cab.n++], BQ_LP, FS, 5000, 1.31f, 0);
        break;
    case 3: /* 4x12 fechado */
        bqSet(&f[cab.n++], BQ_HP, FS, 70, 0.9f, 0);
        bqSet(&f[cab.n++], BQ_PEAK, FS, 100, 1.2f, 5);
        bqSet(&f[cab.n++], BQ_PEAK, FS, 450, 0.9f, -3);
        bqSet(&f[cab.n++], BQ_PEAK, FS, 2800, 1.6f, 5);
        bqSet(&f[cab.n++], BQ_LP, FS, 4800, 0.54f, 0);
        bqSet(&f[cab.n++], BQ_LP, FS, 4800, 1.31f, 0);
        bqSet(&f[cab.n++], BQ_LP, FS, 7500, 0.707f, 0);
        break;
    default: break;
    }
    cab.trim = CAB_TRIM[c];
}

static void setupEq(const SlotState* s)
{
    static const float F[7] = { 100, 200, 400, 800, 1600, 3200, 6400 };
    for (int i = 0; i < 7; i++) bqSet(&eqf[i], BQ_PEAK, FS, F[i], 1.1f, (s->k[i] * 2 - 1) * 15);
    eqLevel = db2lin((s->k[7] * 2 - 1) * 12);
}

static void setupDelay(const SlotState* s)
{
    float fc = 800 * powf(15.0f, s->k[3]);
    opSet(&dly.tone, FS, fc);
}

static void setupReverb(const SlotState* s)
{
    static const float SCALE[3] = { 0.55f, 1.0f, 0.42f };
    int t = s->type;
    if (t != rv.type) {
        for (int i = 0; i < RV_COMBS; i++) {
            rv.clen[i] = (int)(RV_CT[i] * SCALE[t] * FS / 44100.0f); if (rv.clen[i] < 16) rv.clen[i] = 16;
            memset(rv.comb[i], 0, sizeof(float) * 8192); rv.cidx[i] = 0; rv.cstore[i] = 0;
        }
        for (int i = 0; i < RV_APS; i++) {
            rv.alen[i] = (int)(RV_AT[i] * (t == 1 ? 1.0f : 0.7f) * FS / 44100.0f); if (rv.alen[i] < 8) rv.alen[i] = 8;
            memset(rv.ap[i], 0, sizeof(float) * 8192); rv.aidx[i] = 0;
        }
        for (int i = 0; i < 3; i++) dlClear(&rv.sp[i]);
        rv.type = t;
    }
    float d = s->k[0];
    if (t == 0) rv.fb = 0.62f + d * 0.30f;
    else if (t == 1) rv.fb = 0.72f + d * 0.27f;
    else rv.fb = 0.60f + d * 0.32f;
    rv.damp = 0.05f + (1 - s->k[1]) * 0.6f;
    bqSet(&rv.shp, BQ_HP, FS, 300, 0.707f, 0);
    bqSet(&rv.slp, BQ_LP, FS, 3800, 0.707f, 0);
}

static void resetSlot(int s)
{
    switch (s) {
    case S_GATE: memset(&gate, 0, sizeof gate); gate.g = 1; break;
    case S_COMP: memset(&comp, 0, sizeof comp); comp.gsm = 1; break;
    case S_FILTER: memset(&wah, 0, sizeof wah); break;
    case S_DRIVE: { memset(&drv, 0, sizeof drv); } break;
    case S_EQ: for (int i = 0; i < 7; i++) { eqf[i].z1 = eqf[i].z2 = 0; } break;
    case S_MOD: dlClear(&mod.dl); mod.ph = 0; mod.fb = 0; mod.last = 0; memset(mod.apx, 0, sizeof mod.apx); memset(mod.apy, 0, sizeof mod.apy); break;
    case S_DELAY: dlClear(&dly.dl); dly.tone.z = 0; break;
    case S_REVERB: rv.type = -1; dlClear(&rv.pre); break;
    }
}

static void applyChanges(void)
{
    int first = !appliedValid;
    for (int s = 0; s < NSLOT; s++) {
        int typeChanged = first || snap.s[s].type != applied.s[s].type;
        int onChanged = first || snap.s[s].on != applied.s[s].on;
        int kChanged = first || memcmp(snap.s[s].k, applied.s[s].k, sizeof snap.s[s].k) != 0;
        if (typeChanged || (onChanged && snap.s[s].on)) resetSlot(s);
        if (typeChanged || kChanged || onChanged) {
            switch (s) {
            case S_DRIVE: setupDrive(&snap.s[s]); break;
            case S_EQ: setupEq(&snap.s[s]); break;
            case S_DELAY: setupDelay(&snap.s[s]); break;
            case S_REVERB: setupReverb(&snap.s[s]); break;
            default: break;
            }
        }
    }
    if (first || snap.amp != applied.amp || memcmp(snap.ak, applied.ak, sizeof snap.ak) != 0) {
        if (first || snap.amp != applied.amp) {
            memset(&amp, 0, sizeof amp);
        }
        setupAmp(&snap);
        if (first || snap.amp != applied.amp) { amp.g1 = amp.g1t; amp.gS = amp.gSt; amp.master = amp.mastert; }
    }
    if (first || snap.cab != applied.cab) { for (int i = 0; i < 8; i++) cab.f[i].z1 = cab.f[i].z2 = 0; setupCab(snap.cab); }
    applied = snap; appliedValid = 1;
}

/* =================== processamento =================== */
static inline float doGate(const SlotState* s, float x)
{
    float th = db2lin(param_value(&T_GATE[0].k[0], s->k[0]));
    float relMs = param_value(&T_GATE[0].k[1], s->k[1]);
    float a = fabsf(x);
    gate.env = a > gate.env ? a : gate.env * 0.9993f;
    if (!gate.open && gate.env > th) gate.open = 1;
    else if (gate.open && gate.env < th * 0.5f) gate.open = 0;
    float target = gate.open ? 1.0f : 0.0f;
    float k = gate.open ? (1.0f - expf(-1.0f / (0.001f * FS))) : (1.0f - expf(-1.0f / (relMs * 0.001f * FS)));
    gate.g += (target - gate.g) * k;
    return x * gate.g;
}

static inline float doComp(const SlotState* s, float x)
{
    float thr = -50.0f * s->k[0];
    float R = param_value(&T_COMP[0].k[1], s->k[1]);
    float attMs = param_value(&T_COMP[0].k[2], s->k[2]);
    float makeup = param_value(&T_COMP[0].k[3], s->k[3]);
    float mix = s->k[4];
    float att = expf(-1.0f / (attMs * 0.001f * FS)), rel = expf(-1.0f / (0.15f * FS));
    float a = fabsf(x);
    comp.env = a > comp.env ? att * comp.env + (1 - att) * a : rel * comp.env + (1 - rel) * a;
    float e = 20.0f * log10f(comp.env + 1e-9f);
    float over = e - thr, gr;
    if (over <= -3) gr = 0;
    else if (over < 3) gr = (1 - 1 / R) * (over + 3) * (over + 3) / 12.0f;
    else gr = (1 - 1 / R) * over;
    float g = db2lin(-gr + makeup * 0.5f + thr * -0.25f * (1 - 1 / R));
    comp.gsm = smooth(comp.gsm, g, 0.02f);
    return x * (1 - mix) + x * comp.gsm * mix;
}

static inline float doFilter(const SlotState* s, float x)
{
    float fc, Q, mix;
    if (s->type == 0) {
        fc = param_value(&T_FILTER[0].k[0], s->k[0]);
        Q = 2.0f + s->k[1] * 9.0f; mix = s->k[2];
    } else {
        float a = fabsf(x);
        float atk = 1.0f - expf(-1.0f / (0.003f * FS)), rel = 1.0f - expf(-1.0f / (0.09f * FS));
        wah.env += (a - wah.env) * (a > wah.env ? atk : rel);
        float t = wah.env * (0.5f + s->k[0] * 30.0f);
        if (t > 1) t = 1;
        float top = param_value(&T_FILTER[1].k[1], s->k[1]);
        fc = 280.0f * powf(top / 280.0f, t);
        Q = 2.0f + s->k[2] * 9.0f; mix = s->k[3];
    }
    float g = tanf(PI_F * fc / FS), k = 1.0f / Q;
    float a1 = 1.0f / (1.0f + g * (g + k));
    float v3 = x - wah.ic2;
    float v1 = a1 * wah.ic1 + g * a1 * v3;
    float v2 = wah.ic2 + g * v1;
    wah.ic1 = 2 * v1 - wah.ic1; wah.ic2 = 2 * v2 - wah.ic2;
    float y = v1 * k * 3.4f + v2 * 0.35f;
    return x * (1 - mix) + y * mix;
}

/* redutor de ruído interno dos drives de alto ganho: abaixa o chiado quando você para de tocar */
static inline float driveExpander(float x)
{
    float a = fabsf(x);
    drv.nenv += (a - drv.nenv) * (a > drv.nenv ? drv.natk : drv.nrel);
    const float th = 0.00126f;  /* -58 dBFS */
    float g = drv.nenv >= th ? 1.0f : (drv.nenv / th) * (drv.nenv / th);
    drv.ng += (g - drv.ng) * (g > drv.ng ? 0.05f : 0.002f);
    return x * drv.ng;
}

static inline float doDrive(const SlotState* s, float x)
{
    float y = 0;
    switch (s->type) {
    case 0: { /* overdrive */
        float gt = db2lin(6 + s->k[0] * 36);
        drv.gsm = smooth(drv.gsm, gt, 0.002f);
        float h = bq(&drv.pre, x);
        float u = 0;
        for (int j = 0; j < 4; j++) { float z = osUp(&drv.os, j == 0 ? h * 4.0f : 0.0f); z = tanhf(z * drv.gsm); u = osDn(&drv.os, z); }
        y = x * 0.5f + u * 0.55f;
        y = bq(&drv.tone2, bq(&drv.tone, y));
        drv.lsm = smooth(drv.lsm, db2lin(-24 + s->k[2] * 30), 0.002f);
        y *= drv.lsm;
    } break;
    case 1: { /* distorção */
        float gt = db2lin(18 + s->k[0] * 40);
        drv.gsm = smooth(drv.gsm, gt, 0.002f);
        float h = bq(&drv.pre2, bq(&drv.pre, driveExpander(x)));
        float u = 0;
        for (int j = 0; j < 4; j++) {
            float z = osUp(&drv.os, j == 0 ? h * 4.0f : 0.0f) * drv.gsm;
            float az = fabsf(z);
            z = z / powf(1.0f + powf(az, 2.5f), 0.4f);
            u = osDn(&drv.os, z);
        }
        y = bq(&drv.tone2, bq(&drv.tone, u)) * 0.6f;
        drv.lsm = smooth(drv.lsm, db2lin(-31 + s->k[2] * 30), 0.002f);
        y *= drv.lsm;
    } break;
    case 2: { /* fuzz */
        float gt = db2lin(14 + s->k[0] * 26);
        drv.gsm = smooth(drv.gsm, gt, 0.002f);
        float h = bq(&drv.pre, driveExpander(x));
        float u = 0;
        for (int j = 0; j < 4; j++) {
            float z = osUp(&drv.os, j == 0 ? h * 4.0f : 0.0f);
            z = tanhf(z * drv.gsm + 0.1f) - tanhf(0.1f);
            z = opLP(&drv.lp1, z);
            z = tanhf(z * 3.0f);
            z = opLP(&drv.lp2, z);
            u = osDn(&drv.os, z);
        }
        u = bq(&drv.pre2, u);
        float t = s->k[1];
        float lo = bq(&drv.tone, u), hi = bq(&drv.tone2, u);
        y = bq(&drv.post, (lo * (1 - t) + hi * t * 1.1f) * 0.8f);
        drv.lsm = smooth(drv.lsm, db2lin(-30 + s->k[2] * 30), 0.002f);
        y *= drv.lsm;
    } break;
    case 3: { /* boost */
        float gt = db2lin(param_value(&T_DRIVE[3].k[0], s->k[0]));
        drv.gsm = smooth(drv.gsm, gt, 0.002f);
        float h = bq(&drv.pre2, bq(&drv.pre, x)) * drv.gsm;
        y = h > 1.5f || h < -1.5f ? tanhf(h / 1.5f) * 1.5f : h;
    } break;
    }
    return y;
}

static inline float doAmp(float x)
{
    const AmpVoice* v = &VOICES[snap.amp];
    amp.g1 = smooth(amp.g1, amp.g1t, 0.002f);
    amp.gS = smooth(amp.gS, amp.gSt, 0.002f);
    amp.master = smooth(amp.master, amp.mastert, 0.002f);
    float h = bq(&amp.bright, bq(&amp.inHp, x));
    float u = 0;
    for (int j = 0; j < 4; j++) {
        float z = osUp(&amp.os, j == 0 ? h * 4.0f : 0.0f);
        z = tube(z * amp.g1, v->bias);
        for (int st = 1; st < v->stages; st++) {
            if (v->interHp > 0) z = opHP(&amp.ihp[st], z);
            z = opLP(&amp.ilp[st], z);
            z = tube(z * amp.gS, v->bias * 0.7f);
        }
        z = opLP(&amp.ilp[0], z);
        u = osDn(&amp.os, z);
    }
    u = opHP(&amp.dc, u);
    float y = bq(&amp.voice, u);
    y = bq(&amp.treb, bq(&amp.mid, bq(&amp.bass, y)));
    y = tanhf(y * v->power) / v->power;
    y = bq(&amp.pres, y);
    y *= amp.master;
    for (int i = 0; i < cab.n; i++) y = bq(&cab.f[i], y);
    return y * cab.trim;
}

static inline float doEq(float x)
{
    for (int i = 0; i < 7; i++) x = bq(&eqf[i], x);
    return x * eqLevel;
}

static inline float lfoSin(float ph) { return sinf(2 * PI_F * ph); }

static inline float doMod(const SlotState* s, float x)
{
    const FxType* t = &T_MOD[s->type];
    float rate = param_value(&t->k[0], s->k[0]);
    float depth = s->k[1];
    mod.ph += rate / FS; if (mod.ph >= 1) mod.ph -= 1;
    float ms = FS * 0.001f;
    switch (s->type) {
    case 0: { /* chorus */
        float mix = s->k[2];
        dlWrite(&mod.dl, x);
        float ph2 = mod.ph + 0.33f; if (ph2 >= 1) ph2 -= 1;
        float d1 = (7.0f + depth * 5.0f * (0.5f + 0.5f * lfoSin(mod.ph))) * ms;
        float d2 = (11.0f + depth * 5.0f * (0.5f + 0.5f * lfoSin(ph2))) * ms;
        float wet = 0.5f * (dlRead(&mod.dl, d1) + dlRead(&mod.dl, d2));
        return x * (1 - 0.3f * mix) + wet * mix * 0.75f;
    }
    case 1: { /* flanger */
        float fb = s->k[2] * 0.9f, mix = s->k[3];
        float d = (0.3f + depth * 4.0f * (0.5f + 0.5f * lfoSin(mod.ph))) * ms;
        float r = dlRead(&mod.dl, d);
        dlWrite(&mod.dl, x + r * fb);
        return x * (1 - mix) + r * mix;
    }
    case 2: { /* phaser */
        float fb = s->k[2] * 0.75f, mix = s->k[3];
        float fc = 200.0f * powf(1.0f + depth * 14.0f, 0.5f + 0.5f * lfoSin(mod.ph));
        float w = tanf(PI_F * fc / FS), a = (w - 1) / (w + 1);
        float z = x + mod.last * fb;
        for (int i = 0; i < 4; i++) {
            float y = a * z + mod.apx[i] - a * mod.apy[i];
            mod.apx[i] = z; mod.apy[i] = y; z = y;
        }
        mod.last = z;
        return x * (1 - mix) + z * mix;
    }
    default: { /* tremolo */
        float kk = 1.0f + s->k[2] * 15.0f;
        float sq = tanhf(kk * lfoSin(mod.ph)) / tanhf(kk);
        float g = 1.0f - depth * 0.5f * (1.0f - sq);
        return x * g;
    }
    }
}

static inline float doDelay(const SlotState* s, float x)
{
    float target = param_value(&T_DELAY[0].k[0], s->k[0]) * FS * 0.001f;
    if (dly.dsm <= 0) dly.dsm = target;
    dly.dsm = smooth(dly.dsm, target, 0.0003f);
    float d = dly.dsm;
    if (s->type == 1) {
        dly.ph += 0.35f / FS; if (dly.ph >= 1) dly.ph -= 1;
        d += lfoSin(dly.ph) * FS * 0.0006f;
    }
    float r = dlRead(&dly.dl, d);
    float fb = opLP(&dly.tone, r) * s->k[1] * 0.95f;
    if (s->type == 1) fb = tanhf(fb * 1.3f) / 1.3f;
    dlWrite(&dly.dl, x + fb);
    return x + r * s->k[2];
}

static inline float doReverb(const SlotState* s, float x)
{
    float pre = param_value(&T_REVERB[0].k[3], s->k[3]) * FS * 0.001f;
    dlWrite(&rv.pre, x);
    float in = (pre < 1 ? x : dlRead(&rv.pre, pre)) * 0.015f;
    if (rv.type == 2) {
        in = bq(&rv.slp, bq(&rv.shp, in));
        static const float SPD[3] = { 3.1f, 4.4f, 5.9f };
        for (int i = 0; i < 3; i++) {
            float dd = SPD[i] * FS * 0.001f;
            float b = dlRead(&rv.sp[i], dd);
            float o = -0.6f * in + b;
            dlWrite(&rv.sp[i], in + 0.6f * b);
            in = o;
        }
        in *= 1.4f;
    }
    float out = 0;
    for (int i = 0; i < RV_COMBS; i++) {
        float* b = rv.comb[i]; int idx = rv.cidx[i];
        float o = b[idx];
        rv.cstore[i] = o * (1 - rv.damp) + rv.cstore[i] * rv.damp;
        b[idx] = in + rv.cstore[i] * rv.fb;
        if (++idx >= rv.clen[i]) idx = 0;
        rv.cidx[i] = idx;
        out += o;
    }
    for (int i = 0; i < RV_APS; i++) {
        float* b = rv.ap[i]; int idx = rv.aidx[i];
        float bo = b[idx];
        float o = -out + bo;
        b[idx] = out + bo * 0.5f;
        if (++idx >= rv.alen[i]) idx = 0;
        rv.aidx[i] = idx;
        out = o;
    }
    return x + out * 3.0f * s->k[2];
}

static inline float safety(float y)
{
    float a = fabsf(y);
    if (a <= 0.8f) return y;
    float o = 0.8f + 0.2f * tanhf((a - 0.8f) / 0.2f);
    return y < 0 ? -o : o;
}

static float inTrimSm = 1.0f;

void dsp_process(const float* in, float* out, int n, float* peakIn, float* peakOut)
{
#ifdef HAVE_SSE
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
    memcpy(&snap, &rig, sizeof snap);
    if (snap.amp < 0 || snap.amp >= NAMP) snap.amp = 0;
    if (snap.cab < 0 || snap.cab >= NCAB) snap.cab = 0;
    for (int s = 0; s < NSLOT; s++) if (snap.s[s].type < 0 || snap.s[s].type >= SLOTS[s].ntypes) snap.s[s].type = 0;
    applyChanges();

    float trimT = db2lin(inTrimDb);
    float pi = *peakIn, po = *peakOut;
    for (int i = 0; i < n; i++) {
        inTrimSm = smooth(inTrimSm, trimT, 0.002f);
        float x = in[i] * inTrimSm;
        float ax = fabsf(x); if (ax > pi) pi = ax;
        if (snap.s[S_GATE].on) x = doGate(&snap.s[S_GATE], x);
        if (snap.s[S_COMP].on) x = doComp(&snap.s[S_COMP], x);
        if (snap.s[S_FILTER].on) x = doFilter(&snap.s[S_FILTER], x);
        if (snap.s[S_DRIVE].on) x = doDrive(&snap.s[S_DRIVE], x);
        x = doAmp(x);
        if (snap.s[S_EQ].on) x = doEq(x);
        if (snap.s[S_MOD].on) x = doMod(&snap.s[S_MOD], x);
        if (snap.s[S_DELAY].on) x = doDelay(&snap.s[S_DELAY], x);
        if (snap.s[S_REVERB].on) x = doReverb(&snap.s[S_REVERB], x);
        if (!(x == x)) x = 0;
        float ao = fabsf(x); if (ao > po) po = ao;
        out[i] = safety(x);
    }
    *peakIn = pi; *peakOut = po;
}

void dsp_reset(void)
{
    appliedValid = 0;
    rv.type = -1;
    for (int s = 0; s < NSLOT; s++) resetSlot(s);
    memset(&amp, 0, sizeof amp);
}

void dsp_init(float fs)
{
    FS = fs > 8000 ? fs : 48000;
    dlAlloc(&mod.dl, (int)(FS * 0.05f) + 8);
    dlAlloc(&dly.dl, (int)(FS * 1.6f) + 8);
    dlAlloc(&rv.pre, (int)(FS * 0.13f) + 8);
    for (int i = 0; i < 3; i++) dlAlloc(&rv.sp[i], (int)(FS * 0.008f) + 8);
    for (int i = 0; i < RV_COMBS; i++) if (!rv.comb[i]) rv.comb[i] = (float*)calloc(8192 * 5, sizeof(float));
    for (int i = 0; i < RV_APS; i++) if (!rv.ap[i]) rv.ap[i] = (float*)calloc(8192 * 5, sizeof(float));
    dsp_reset();
}

/* usado pelo teste para calibrar volumes */
void dsp_set_trims(const float* ampTrim, const float* cabTrim)
{
    for (int i = 0; i < NAMP; i++) AMP_TRIM[i] = ampTrim[i];
    for (int i = 0; i < NCAB; i++) CAB_TRIM[i] = cabTrim[i];
    appliedValid = 0;
}
