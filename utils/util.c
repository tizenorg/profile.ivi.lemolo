#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Eina.h>
#include <time.h>
#include <Evas.h>
#include <Elementary.h>

#include "util.h"
#include "log.h"

static char def_theme[PATH_MAX] = "";
static char *_last_user_mode_config_path;
static Eet_Data_Descriptor *_last_user_mode_descriptor;

/* TODO: find a configurable way to format the number.
 * Right now it's: 1-234-567-8901 as per
 * http://en.wikipedia.org/wiki/Local_conventions_for_writing_telephone_numbers#North_America
 *
 * IDEA: use ordered set of regexp -> replacement.
 */
char *phone_format(const char *number)
{
	size_t i, slen;
	Eina_Strbuf *d;
	char *ret;

	if (!number)
		return NULL;

	slen = strlen(number);
	if (slen < 1)
		return NULL;

	if ((slen <= 4) || (slen > 12))
		goto show_verbatim;

	if ((slen == 12) && (number[0] != '+'))
		goto show_verbatim;

	for (i = 0; i < slen; i++) {
		if ((number[i] < '0') || (number[i] > '9')) {
			if ((number[i] == '+') && (i == 0))
				continue;
			goto show_verbatim;
		}
	}

	d = eina_strbuf_new();
	eina_strbuf_append_length(d, number, slen);
	if ((slen > 4) && (number[slen - 5] != '+'))
		eina_strbuf_insert_char(d, '-', slen - 4);
	if (slen > 7) {
		if ((slen > 7) && (number[slen - 8] != '+'))
			eina_strbuf_insert_char(d, '-', slen - 7);
		if ((slen > 10) && (number[slen - 11] != '+'))
			eina_strbuf_insert_char(d, '-', slen - 10);
	}

	ret = eina_strbuf_string_steal(d);
	eina_strbuf_free(d);
	return ret;

show_verbatim:
	return strdup(number);
}

char *date_format(time_t date)
{
	time_t now = time(NULL);
	double dt = difftime(now, date);
	char *buf;
	int r;

	if (dt < 30)
		r = asprintf(&buf, "Just now");
	else if (dt < (MINUTE * 2))
		r = asprintf(&buf, "One minute ago");
	else if (dt < (HOUR * 2))
		r = asprintf(&buf, "%dmin ago", (int)dt/60);
	else if (dt < (HOUR * 4))
		r = asprintf(&buf, "%dh ago", (int)dt/3600);
	else if (dt <= DAY) {
		struct tm *f_time = localtime(&date);
		r = asprintf(&buf,  "%02d:%02d", f_time->tm_hour,
				f_time->tm_min);
	} else if (dt < WEEK) {
		char tmp[256];
		struct tm *tm = localtime(&date);
		strftime(tmp, sizeof(tmp), "%A", tm);
		r = asprintf(&buf, "%s", tmp);
	} else {
		char tmp[256];
		struct tm *tm = localtime(&date);
		strftime(tmp, sizeof(tmp), "%x", tm);
		r = asprintf(&buf, "%s", tmp);
	}

	if (r < 0)
		buf = strdup("");

	return buf;
}

char *date_short_format(time_t date)
{
	time_t now = time(NULL);
	double dt = difftime(now, date);
	char *buf;
	int r;

	if (dt <= DAY) {
		struct tm *f_time = localtime(&date);
		r = asprintf(&buf,  "%02d:%02d", f_time->tm_hour,
				f_time->tm_min);
	} else if (dt < WEEK) {
		char tmp[256];
		struct tm *tm = localtime(&date);
		strftime(tmp, sizeof(tmp), "%A", tm);
		r = asprintf(&buf, "%s", tmp);
	} else {
		char tmp[256];
		struct tm *tm = localtime(&date);
		strftime(tmp, sizeof(tmp), "%x", tm);
		r = asprintf(&buf, "%s", tmp);
	}

	if (r < 0)
		buf = strdup("");

	return buf;
}

Evas_Object *picture_icon_get(Evas_Object *parent, const char *picture)
{
	Evas_Object *icon = elm_icon_add(parent);

	if (!picture || *picture == '\0')
		elm_icon_standard_set(icon, "no-picture");
	else {
		char path[PATH_MAX];
		const char *prefix;
		prefix = efreet_config_home_get();
		snprintf(path, sizeof(path), "%s/%s/%s", prefix, PACKAGE_NAME,
				picture);
		elm_image_file_set(icon, path, NULL);
	}
	return icon;
}

Evas_Object *layout_add(Evas_Object *parent, const char *style)
{
	Evas_Object *layout = elm_layout_add(parent);
	if (!elm_layout_theme_set(layout, "layout", "ofono-efl", style)) {
		CRITICAL("No theme for 'elm/layout/ofono-efl/%s' at %s",
				style, def_theme);
		evas_object_del(layout);
		return NULL;
	}
	return layout;
}

Eina_Bool util_set_night_mode(Eina_Bool night_mode)
{
        snprintf(def_theme, sizeof(def_theme), "%s/themes/night.edj",
                        elm_app_data_dir_get());

        elm_theme_extension_add(NULL, def_theme);

        if (night_mode)
        {
                night_mode = EINA_TRUE;
                elm_theme_overlay_add(NULL, def_theme);
                DBG("Load theme at %s", def_theme);
        }
        else
        {
                night_mode = EINA_FALSE;
                elm_theme_overlay_del(NULL, def_theme);
                DBG("Unload theme at %s", def_theme);
        }

	return EINA_TRUE;
}

Eina_Bool util_set_last_user_mode(Last_User_Mode *mode) {
	Eina_Bool ret;
	Eet_File *efile;

	EINA_SAFETY_ON_NULL_RETURN_VAL(_last_user_mode_config_path, EINA_FALSE);
	EINA_SAFETY_ON_NULL_RETURN_VAL(_last_user_mode_descriptor, EINA_FALSE);

	efile = eet_open(_last_user_mode_config_path, EET_FILE_MODE_WRITE);
	if (!efile) {
		ERR("Cannot open %s for write", _last_user_mode_config_path);
		return EINA_FALSE;
	}

	ret = eet_data_write(efile, _last_user_mode_descriptor, LAST_USER_MODE_ENTRY, mode, EINA_TRUE);
	if (!ret) {
		ERR("Cannot write to %s", _last_user_mode_config_path);
	}
	eet_close(efile);
	return ret;
}

Last_User_Mode *util_get_last_user_mode() {
	Eet_File *efile;
	Last_User_Mode *mode;

	EINA_SAFETY_ON_NULL_RETURN_VAL(_last_user_mode_config_path, NULL);

	efile = eet_open(_last_user_mode_config_path, EET_FILE_MODE_READ);
	if (!efile) {
		ERR("Cannot open %s for read", _last_user_mode_config_path);
		return NULL;
	}

	mode = eet_data_read(efile, _last_user_mode_descriptor, LAST_USER_MODE_ENTRY);
	eet_close(efile);

	return mode;
}

static void _last_user_mode_descriptor_init(void) {
	Eet_Data_Descriptor_Class eddc;

	EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Last_User_Mode);
	_last_user_mode_descriptor = eet_data_descriptor_stream_new(&eddc);

	EET_DATA_DESCRIPTOR_ADD_BASIC(_last_user_mode_descriptor, Last_User_Mode,
					"last_view", last_view, EET_T_UINT);
	EET_DATA_DESCRIPTOR_ADD_BASIC(_last_user_mode_descriptor, Last_User_Mode,
					"last_history_view", last_history_view, EET_T_UINT);
	EET_DATA_DESCRIPTOR_ADD_BASIC(_last_user_mode_descriptor, Last_User_Mode,
					"last_number", last_number, EET_T_STRING);
}


Eina_Bool util_init(const char *theme)
{
	int ret;
	const char *config_path;
	char base_dir[PATH_MAX];
	Eet_File *efile;

	eet_init();
	config_path = efreet_config_home_get();
	snprintf(base_dir, sizeof(base_dir), "%s/%s", config_path, PACKAGE_NAME);
	ecore_file_mkpath(base_dir);
	ret = asprintf(&_last_user_mode_config_path,  "%s/%s/last_user_mode.eet", config_path, PACKAGE_NAME);
	if (ret < 0)
		return EINA_FALSE;

	_last_user_mode_descriptor_init();
	elm_app_compile_bin_dir_set(PACKAGE_BIN_DIR);
	elm_app_compile_data_dir_set(PACKAGE_DATA_DIR);
	elm_app_info_set(util_init, "ofono-efl", "themes/default.edj");

	snprintf(def_theme, sizeof(def_theme), "%s/themes/default.edj",
			elm_app_data_dir_get());

	elm_theme_extension_add(NULL, def_theme);
	if (!theme)
		elm_theme_overlay_add(NULL, def_theme);
	else {
		char tmp[PATH_MAX];
		if (theme[0] != '/') {
			if (eina_str_has_suffix(theme, ".edj")) {
				snprintf(tmp, sizeof(tmp), "%s/themes/%s",
						elm_app_data_dir_get(), theme);
			} else {
				snprintf(tmp, sizeof(tmp), "%s/themes/%s.edj",
						elm_app_data_dir_get(), theme);
			}
			theme = tmp;
		}
		elm_theme_overlay_add(NULL, theme);
	}

	return EINA_TRUE;
}

void util_shutdown(void)
{
	if (_last_user_mode_config_path)
		free(_last_user_mode_config_path);
	if (_last_user_mode_descriptor)
		eet_data_descriptor_free(_last_user_mode_descriptor);
	eet_shutdown();
}
