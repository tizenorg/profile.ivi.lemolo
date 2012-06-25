#ifndef _EFL_OFONO_CALLSCREEN_H__
#define _EFL_OFONO_CALLSCREEN_H__ 1

#include "ofono.h"

Evas_Object *callscreen_add(Evas_Object *parent);
void callscreen_call_add(Evas_Object *obj, OFono_Call *call);

#endif
