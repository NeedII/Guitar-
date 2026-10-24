#ifndef GNP_RIGIO_H
#define GNP_RIGIO_H
#include "dsp.h"

typedef struct { const char* name; const char* rig; } FactoryPreset;
extern const FactoryPreset FACTORY[];
extern const int NFACTORY;
extern const char* const SLOT_KEYS[NSLOT];

void rig_serialize(const Rig* r, char* buf, int n);
void rig_parse(Rig* r, const char* s);    /* parte dos padrões e aplica o texto */
int  rig_equal(const Rig* a, const Rig* b);

#endif
