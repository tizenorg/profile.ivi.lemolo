#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Eina.h>

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
