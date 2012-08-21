#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Eina.h>
#include <time.h>
#include <Evas.h>
#include <Elementary.h>

#include "gui.h"
#include "util.h"

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
	else if (dt < (MINUTE + 30))
		r = asprintf(&buf, "One minute ago");
	else if (dt < (HOUR + 100))
		r = asprintf(&buf, "%d minutes ago", (int)dt/60);
	else if (dt < (HOUR * 4))
		r = asprintf(&buf, "%d hours ago", (int)dt/3600);
	else if (dt <= DAY) {
		struct tm *f_time = gmtime(&date);
		EINA_SAFETY_ON_NULL_GOTO(f_time, err_gmtime);
		r = asprintf(&buf,  "%02d:%02d", f_time->tm_hour,
				f_time->tm_min);
	} else if (dt < WEEK) {
		char tmp[256];
		struct tm *tm = localtime(&date);
		strftime(tmp, sizeof(tmp), "%A", tm);
		int days = dt / DAY;
		r = asprintf(&buf, "%s, %d days ago", tmp, days);
	} else {
		char tmp[256];
		struct tm *tm = localtime(&date);
		strftime(tmp, sizeof(tmp), "%x", tm);
		r = asprintf(&buf, "%s", tmp);
	}

	if (r < 0)
		buf = strdup("");

	return buf;

err_gmtime:
	return strdup("");
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

static void _dial_reply(void *data, OFono_Error err,
			OFono_Call *call __UNUSED__)
{
	char *number = data;

	if (err != OFONO_ERROR_NONE) {
		char buf[1024];
		const char *msg = ofono_error_message_get(err);
		snprintf(buf, sizeof(buf), "Could not call %s: %s",
				number, msg);
		gui_simple_popup("Error", buf);
	}

	free(number);
}

OFono_Pending *dial(const char *number)
{
	char *copy = strdup(number);
	return ofono_dial(copy, NULL, _dial_reply, copy);
}
