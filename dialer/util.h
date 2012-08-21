#ifndef _EFL_OFONO_UTIL_H__
#define _EFL_OFONO_UTIL_H__

#define MINUTE 60
#define HOUR ((MINUTE) * 60)
#define DAY ((HOUR) * 24)
#define WEEK ((DAY) * 7)
#define MONTH ((DAY) * 30)
#define YEAR ((MONTH) *12)

#include "ofono.h"

char *phone_format(const char *number);

char *date_format(time_t date);

Evas_Object *picture_icon_get(Evas_Object *parent, const char *picture);

OFono_Pending *dial(const char *number);

#endif
