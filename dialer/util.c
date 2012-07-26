#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Eina.h>
#include <time.h>

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
	eina_strbuf_insert_char(d, '-', slen - 4);
	if (slen > 7) {
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
	else if (dt < 90)
		r = asprintf(&buf, "One minute ago");
	else if (dt < 3700)
		r = asprintf(&buf, "%d minutes ago", (int)dt/60);
	else if (dt < 3600 * 4)
		r = asprintf(&buf, "%d hours ago", (int)dt/3600);
	else if (dt < 3600 * 24) {
		struct tm *f_time = gmtime(&date);
		EINA_SAFETY_ON_NULL_GOTO(f_time, err_gmtime);
		r = asprintf(&buf,  "%02d:%02d", f_time->tm_hour,
				f_time->tm_min);
	} else if(dt < 3600 * 24 * 30){
		struct tm f_time;
		struct tm f_now;
		EINA_SAFETY_ON_NULL_GOTO(gmtime_r(&now, &f_now), err_gmtime);
		EINA_SAFETY_ON_NULL_GOTO(gmtime_r(&date, &f_time), err_gmtime);
		r = asprintf(&buf, "%d days ago",
				f_now.tm_mday - f_time.tm_mday);
	} else if (dt < 3600 * 24 * 30 * 12) {
		struct tm f_time;
		struct tm f_now;
		EINA_SAFETY_ON_NULL_GOTO(gmtime_r(&now, &f_now), err_gmtime);
		EINA_SAFETY_ON_NULL_GOTO(gmtime_r(&date, &f_time), err_gmtime);
		r = asprintf(&buf, "%d years ago",
				f_now.tm_mon - f_time.tm_mon);
	} else {
		struct tm f_time;
		struct tm f_now;
		EINA_SAFETY_ON_NULL_GOTO(gmtime_r(&now, &f_now), err_gmtime);
		EINA_SAFETY_ON_NULL_GOTO(gmtime_r(&date, &f_time), err_gmtime);
		r = asprintf(&buf, "%d years ago",
				f_now.tm_year - f_time.tm_year);
	}

	if (r < 0)
		buf = strdup("");

	return buf;

err_gmtime:
	return strdup("");
}
