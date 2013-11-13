#ifndef _EFL_OFONO_UTIL_H__
#define _EFL_OFONO_UTIL_H__

#define MINUTE 60
#define HOUR ((MINUTE) * 60)
#define DAY ((HOUR) * 24)
#define WEEK ((DAY) * 7)
#define MONTH ((DAY) * 30)
#define YEAR ((MONTH) *12)

enum {
	DIALER_LAST_VIEW_KEYPAD = 0,
	DIALER_LAST_VIEW_CONTACTS,
	DIALER_LAST_VIEW_HISTORY
};

enum {
	DIALER_LAST_HISTORY_VIEW_ALL = 0,
	DIALER_LAST_HISTORY_VIEW_MISSED
};

#define LAST_USER_MODE_ENTRY "last_user_mode"

typedef struct
{
	int last_view;
	int last_history_view;
	const char *last_number;
} Last_User_Mode;

char *phone_format(const char *number);

char *date_format(time_t date);
char *date_short_format(time_t date);

Evas_Object *picture_icon_get(Evas_Object *parent, const char *picture);

Evas_Object *layout_add(Evas_Object *parent, const char *style);

Eina_Bool util_set_night_mode(Eina_Bool night_mode);

Eina_Bool util_set_last_user_mode(Last_User_Mode *mode);

Last_User_Mode *util_get_last_user_mode();

Eina_Bool util_init(const char *theme);
void util_shutdown(void);

#endif
