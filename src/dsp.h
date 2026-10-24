#ifndef GNP_DSP_H
#define GNP_DSP_H

#define NSLOT 8
#define MAXK  8
#define NAMP  5
#define NCAB  4
#define AMP_POS 4            /* o amplificador entra antes do slot 4 */

enum { S_GATE, S_COMP, S_FILTER, S_DRIVE, S_EQ, S_MOD, S_DELAY, S_REVERB };
enum { F_PCT, F_DIAL, F_DB, F_HZ, F_MS, F_RATIO, F_MSLIN };

typedef struct { const char* label; float def; int fmt; float a, b; } ParamDef;
typedef struct { const char* name; const char* shortName; const char* desc; int nk; ParamDef k[MAXK]; unsigned color; } FxType;
typedef struct { const char* category; int ntypes; const FxType* types; } SlotDef;
typedef struct { const char* name; const char* logo; const char* desc; } AmpInfo;

typedef struct { int on, type; float k[MAXK]; } SlotState;
typedef struct { int amp, cab; float ak[6]; SlotState s[NSLOT]; } Rig;

extern const SlotDef SLOTS[NSLOT];
extern const AmpInfo AMPS[NAMP];
extern const char* const CABS[NCAB];
extern const char* const CAB_DESC[NCAB];
extern const ParamDef AMPK[6];

extern Rig rig;                 /* editado pela tela, lido pelo áudio */
extern volatile float inTrimDb; /* nível da entrada, -12..+24 dB */

float param_value(const ParamDef* p, float v);
void  param_format(const ParamDef* p, float v, char* buf, int n);
void  slot_defaults(SlotState* s, int slot, int type);
void  rig_defaults(Rig* r);

void  dsp_init(float fs);
void  dsp_reset(void);
void  dsp_process(const float* in, float* out, int n, float* peakIn, float* peakOut);

#endif
