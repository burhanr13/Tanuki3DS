#ifndef SVC_H
#define SVC_H

#include "3ds.h"

#define SVC_MAX BIT(8)

typedef void (*SVCFunc)(HLE3DS* s);
#define DECL_SVC(name) void svc_##name(HLE3DS* s)

extern SVCFunc svc_table[SVC_MAX];
extern char* svc_names[SVC_MAX];

void hle3ds_handle_svc(HLE3DS* s, u32 num);

#endif
