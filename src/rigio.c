/* Presets: formato de texto e presets de fábrica */
#include "rigio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

const char* const SLOT_KEYS[NSLOT] = { "gate", "comp", "filter", "drive", "eq", "mod", "delay", "reverb" };

/* ak = GANHO, GRAVE, MÉDIO, AGUDO, PRESENÇA, MASTER
   pedal = ligado,tipo:knob1,knob2,...   (knobs que faltam ficam no padrão do tipo) */
const FactoryPreset FACTORY[] = {
    { "Limpo",
      "amp=0 cab=2 ak=.3,.5,.5,.6,.5,.6 reverb=1,2:.35,.45,.18" },
    { "Funk limpo · Chili Peppers",
      "amp=2 cab=3 ak=.16,.45,.6,.65,.6,.55 comp=1,0:.5,.4,.25,.35,.8 reverb=1,0:.3,.55,.1" },
    { "Funk auto-wah",
      "amp=0 cab=2 ak=.3,.45,.55,.6,.55,.37 comp=1,0:.55,.4,.2,.35,.9 filter=1,1:.55,.6,.6,1 reverb=1,0:.25,.5,.08" },
    { "Under the Bridge · Chili Peppers",
      "amp=0 cab=2 ak=.35,.5,.55,.55,.5,.5 comp=1,0:.35,.3,.4,.3,.6 mod=1,0:.25,.45,.4 reverb=1,1:.5,.45,.2,.2" },
    { "Crunch · Chili Peppers",
      "amp=2 cab=3 ak=.5,.5,.62,.6,.55,.5 gate=1,0:.33,.4 reverb=1,0:.3,.5,.1" },
    { "Solo fuzz · Chili Peppers",
      "amp=2 cab=3 ak=.38,.5,.6,.55,.5,.5 gate=1,0:.36,.45 drive=1,2:.6,.55,.55 delay=1,1:.52,.3,.22,.35 reverb=1,1:.45,.45,.15,.2" },
    { "Blues texano",
      "amp=0 cab=2 ak=.5,.5,.55,.55,.55,.33 drive=1,0:.22,.55,.72 reverb=1,2:.4,.45,.25" },
    { "Blues tweed",
      "amp=1 cab=1 ak=.5,.5,.6,.5,.5,.42 reverb=1,2:.35,.45,.2" },
    { "Base · AC/DC",
      "amp=2 cab=3 ak=.55,.45,.7,.65,.55,.5 gate=1,0:.33,.35 reverb=1,0:.25,.5,.07" },
    { "Solo · AC/DC",
      "amp=2 cab=3 ak=.62,.45,.72,.65,.55,.5 gate=1,0:.34,.4 drive=1,3:.35,.2 delay=1,0:.42,.18,.12,.7 reverb=1,0:.3,.5,.1" },
    { "Base · Guns N' Roses",
      "amp=3 cab=3 ak=.62,.55,.65,.6,.5,.5 gate=1,0:.4,.35 reverb=1,0:.3,.5,.1" },
    { "Solo com wah · Guns N' Roses",
      "amp=3 cab=3 ak=.72,.5,.65,.6,.5,.5 gate=1,0:.4,.4 filter=1,0:.55,.55,1 delay=1,0:.6,.3,.2,.75 reverb=1,1:.45,.5,.15,.2" },
    { "Alto ganho",
      "amp=4 cab=3 ak=.7,.6,.45,.6,.55,.5 gate=1,0:.45,.3 eq=1,0:.55,.5,.42,.45,.5,.55,.5,.5" },
};
const int NFACTORY = (int)(sizeof FACTORY / sizeof FACTORY[0]);

void rig_serialize(const Rig* r, char* buf, int n)
{
    int p = snprintf(buf, n, "amp=%d cab=%d ak=", r->amp, r->cab);
    for (int i = 0; i < 6 && p < n; i++) p += snprintf(buf + p, n - p, "%s%.3f", i ? "," : "", r->ak[i]);
    for (int s = 0; s < NSLOT && p < n; s++) {
        const FxType* t = &SLOTS[s].types[r->s[s].type];
        p += snprintf(buf + p, n - p, " %s=%d,%d:", SLOT_KEYS[s], r->s[s].on, r->s[s].type);
        for (int k = 0; k < t->nk && p < n; k++) p += snprintf(buf + p, n - p, "%s%.3f", k ? "," : "", r->s[s].k[k]);
    }
}

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static void parseFloats(const char* s, float* out, int max)
{
    for (int i = 0; i < max && s && *s; i++) {
        char* end; float v = strtof(s, &end);
        if (end == s) break;
        out[i] = clamp01(v);
        s = end; if (*s == ',') s++; else break;
    }
}

void rig_parse(Rig* r, const char* text)
{
    rig_defaults(r);
    char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", text);
    char* save = NULL;
    for (char* tok = strtok_r(tmp, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        char* eq = strchr(tok, '='); if (!eq) continue;
        *eq = 0; const char* key = tok; const char* val = eq + 1;
        if (!strcmp(key, "amp")) { int a = atoi(val); r->amp = a >= 0 && a < NAMP ? a : 0; }
        else if (!strcmp(key, "cab")) { int c = atoi(val); r->cab = c >= 0 && c < NCAB ? c : 2; }
        else if (!strcmp(key, "ak")) parseFloats(val, r->ak, 6);
        else for (int s = 0; s < NSLOT; s++) if (!strcmp(key, SLOT_KEYS[s])) {
            int on = 0, type = 0;
            sscanf(val, "%d,%d", &on, &type);
            slot_defaults(&r->s[s], s, type);
            r->s[s].on = on ? 1 : 0;
            const char* colon = strchr(val, ':');
            if (colon) parseFloats(colon + 1, r->s[s].k, SLOTS[s].types[r->s[s].type].nk);
        }
    }
}

int rig_equal(const Rig* a, const Rig* b)
{
    if (a->amp != b->amp || a->cab != b->cab) return 0;
    for (int i = 0; i < 6; i++) if (fabsf(a->ak[i] - b->ak[i]) > 0.004f) return 0;
    for (int s = 0; s < NSLOT; s++) {
        if (a->s[s].on != b->s[s].on || a->s[s].type != b->s[s].type) return 0;
        int nk = SLOTS[s].types[a->s[s].type].nk;
        for (int k = 0; k < nk; k++) if (fabsf(a->s[s].k[k] - b->s[s].k[k]) > 0.004f) return 0;
    }
    return 1;
}
