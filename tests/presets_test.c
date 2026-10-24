/* Banco de teste: guitarra sintética passando pelos presets */
#include "dsp.h"
#include "rigio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void dsp_set_trims(const float* ampTrim, const float* cabTrim);

#define FSR 48000
#define SECS 8
static float sig[FSR * SECS], outb[FSR * SECS];

static unsigned rs = 1;
static float frand(void) { rs = rs * 1664525u + 1013904223u; return ((rs >> 8) / 16777216.0f) * 2 - 1; }

static void pluck(float freq, float t0, float dur, float amp)
{
    int n = (int)(FSR / freq); float* d = calloc(n, sizeof(float));
    for (int i = 0; i < n; i++) d[i] = frand() * amp;
    for (int i = 0; i < 3; i++) for (int j = 1; j < n; j++) d[j] = 0.5f * (d[j] + d[j - 1]);
    int s0 = (int)(t0 * FSR), s1 = s0 + (int)(dur * FSR); if (s1 > FSR * SECS) s1 = FSR * SECS;
    int idx = 0; float prev = 0;
    for (int s = s0; s < s1; s++) {
        float v = d[idx];
        int nx = (idx + 1) % n;
        d[idx] = 0.996f * 0.5f * (v + d[nx]);
        prev = v; idx = nx;
        float env = 1.0f; float rem = (s1 - s) / (float)FSR; if (rem < 0.02f) env = rem / 0.02f;
        sig[s] += prev * env;
    }
    free(d);
}

static void makeSignal(void)
{
    memset(sig, 0, sizeof sig);
    /* riff de acordes de força */
    float roots[] = { 82.41f, 82.41f, 110.0f, 146.83f, 82.41f, 98.0f, 110.0f, 82.41f };
    for (int i = 0; i < 8; i++) {
        float t = i * 0.35f, r = roots[i];
        pluck(r, t, 0.33f, 0.5f); pluck(r * 1.4983f, t + 0.004f, 0.33f, 0.45f); pluck(r * 2, t + 0.008f, 0.33f, 0.35f);
    }
    /* acorde aberto de Mi tocado */
    float ch[] = { 82.41f, 123.47f, 164.81f, 207.65f, 246.94f, 329.63f };
    for (int r = 0; r < 2; r++) for (int i = 0; i < 6; i++) pluck(ch[i], 3.0f + r * 1.0f + i * 0.012f, 0.95f, 0.35f);
    /* solo */
    float lick[] = { 329.63f, 392.0f, 440.0f, 493.88f, 587.33f, 493.88f, 440.0f, 659.25f };
    for (int i = 0; i < 8; i++) pluck(lick[i], 5.2f + i * 0.28f, 0.5f, 0.6f);
    float pk = 0; for (int i = 0; i < FSR * SECS; i++) if (fabsf(sig[i]) > pk) pk = fabsf(sig[i]);
    for (int i = 0; i < FSR * SECS; i++) sig[i] = sig[i] / pk * 0.3f + frand() * 0.0003f;
}

typedef struct { float rms, peak, hf, nan; } Res;

static Res run(void)
{
    dsp_init(FSR);
    float pi = 0, po = 0;
    for (int i = 0; i < FSR * SECS; i += 256) { pi = po = 0; dsp_process(sig + i, outb + i, 256, &pi, &po); }
    Res r = { 0 };
    double s = 0; int cnt = 0;
    for (int i = FSR / 2; i < FSR * SECS; i++) {
        float v = outb[i]; if (v != v) r.nan = 1;
        s += v * v; cnt++; if (fabsf(v) > r.peak) r.peak = fabsf(v);
    }
    r.rms = 10 * log10f((float)(s / cnt) + 1e-12f);
    /* energia acima de ~7 kHz via diferença de segunda ordem (aproximação) */
    double h = 0;
    for (int i = FSR / 2 + 2; i < FSR * SECS; i++) { float d2 = outb[i] - 2 * outb[i - 1] + outb[i - 2]; h += d2 * d2; }
    r.hf = 10 * log10f((float)(h / s) + 1e-12f);
    return r;
}

static void wav(const char* path)
{
    FILE* f = fopen(path, "wb"); if (!f) return;
    int n = FSR * SECS, bytes = n * 2;
    int hdr[] = { 0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16, 0x00010001, FSR, FSR * 2, 0x00100002, 0x61746164, bytes };
    fwrite(hdr, 4, 11, f);
    for (int i = 0; i < n; i++) { short v = (short)(outb[i] * 32000); fwrite(&v, 2, 1, f); }
    fclose(f);
}

int main(int argc, char** argv)
{
    makeSignal();
    float at[NAMP] = { 1, 1, 1, 1, 1 }, ct[NCAB] = { 1, 1, 1, 1 };
    const float target = -20.0f;
    /* calibra gabinetes com o amp limpo */
    dsp_set_trims(at, ct);
    float cabRms[NCAB];
    for (int c = 0; c < NCAB; c++) { rig_parse(&rig, "amp=0 cab=0 ak=.3,.5,.5,.5,.5,.5"); rig.cab = c; cabRms[c] = run().rms; }
    for (int c = 1; c < NCAB; c++) ct[c] = powf(10, (cabRms[0] - cabRms[c]) / 20);
    dsp_set_trims(at, ct);
    /* calibra amplificadores com 4x12, ganho 5, master 5 */
    for (int a = 0; a < NAMP; a++) {
        rig_parse(&rig, "cab=3 ak=.5,.5,.5,.5,.5,.5"); rig.amp = a;
        Res r = run();
        at[a] = powf(10, (target - r.rms) / 20);
    }
    dsp_set_trims(at, ct);
    printf("AMP_TRIM = { "); for (int a = 0; a < NAMP; a++) printf("%.4ff, ", at[a]); printf("}\n");
    printf("CAB_TRIM = { "); for (int c = 0; c < NCAB; c++) printf("%.4ff, ", ct[c]); printf("}\n\n");

    printf("%-34s %8s %7s %8s\n", "preset", "RMS dB", "pico", "agudo");
    for (int p = 0; p < NFACTORY; p++) {
        rig_parse(&rig, FACTORY[p].rig);
        Res r = run();
        printf("%-34s %8.1f %7.3f %8.1f%s\n", FACTORY[p].name, r.rms, r.peak, r.hf, r.nan ? "  NaN!" : "");
        if (argc > 1) { char path[64]; snprintf(path, sizeof path, "wav/p%02d.wav", p); wav(path); }
    }
    /* cada pedal sozinho no amp limpo, para checar estabilidade */
    printf("\n%-34s %8s %7s\n", "pedal", "RMS dB", "pico");
    for (int s = 0; s < NSLOT; s++) for (int t = 0; t < SLOTS[s].ntypes; t++) {
        rig_parse(&rig, "amp=0 cab=2 ak=.3,.5,.5,.5,.5,.5");
        slot_defaults(&rig.s[s], s, t); rig.s[s].on = 1;
        Res r = run();
        printf("%-34s %8.1f %7.3f%s\n", SLOTS[s].types[t].name, r.rms, r.peak, r.nan ? "  NaN!" : "");
        /* extremos dos knobs */
        for (int k = 0; k < MAXK; k++) rig.s[s].k[k] = 1.0f;
        Res r2 = run();
        if (r2.nan || r2.peak > 0.999f) printf("   no máximo: pico %.3f%s\n", r2.peak, r2.nan ? " NaN!" : "");
    }
    rig_parse(&rig, "amp=0 cab=2 ak=.3,.5,.5,.5,.5,.5"); Res base = run();
    printf("\nreferência amp limpo sem pedais: %.1f dB\n", base.rms);
    return 0;
}
