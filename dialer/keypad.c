#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "log.h"
#include "gui.h"
#include "ofono.h"
#include "ussd.h"
#include "util.h"
#include "simple-popup.h"

/* timeout before a popup is show for supplementary services.  It is
 * not shown immediately as the call may fail as "not supported" in
 * that case the dial will be done and we don't want a popup before
 * it.
 */
#define SS_POPUP_SHOW_TIMEOUT (0.5)

/* timeout to change keys into modified, like 0 -> + */
#define MOD_TIMEOUT (1.0)

/* timeouts to repeat key while pressed */
#define REP_TIMEOUT_INIT (0.3)
#define REP_TIMEOUT (0.2)

typedef struct _Keypad
{
	Evas_Object *self;
	Evas_Object *ss_popup;
	Ecore_Timer *ss_popup_show_timeout;
	OFono_Pending *ss_pending;
	Eina_Strbuf *number;
	const char *last;
	Ecore_Timer *mod_timeout;
	Ecore_Timer *rep_timeout;
} Keypad;

static void _number_display(Keypad *ctx)
{
	const char *number = eina_strbuf_string_get(ctx->number);
	char *s = phone_format(number);
	const char *type;
	if (!s) {
		elm_object_part_text_set(ctx->self, "elm.text.display", "");
		elm_object_part_text_set(ctx->self, "elm.text.contact-and-type",
						"");
		elm_object_part_text_set(ctx->self, "elm.text.contact", "");
		elm_object_part_text_set(ctx->self, "elm.text.phone.type", "");
		elm_object_signal_emit(ctx->self, "disable,save", "keypad");
		elm_object_signal_emit(ctx->self, "disable,backspace",
					"keypad");
		elm_object_signal_emit(ctx->self, "hide,contact", "keypad");
		return;
	}

	Contact_Info *info = gui_contact_search(number, &type);
	if (info) {
		const char *name = contact_info_full_name_get(info);
		char buf[1024];

		snprintf(buf, sizeof(buf), "%s - %s", name, type);

		elm_object_part_text_set(ctx->self, "elm.text.contact-and-type",
						buf);
		elm_object_part_text_set(ctx->self, "elm.text.contact", name);
		elm_object_part_text_set(ctx->self, "elm.text.phone.type", type);
		elm_object_signal_emit(ctx->self, "show,contact", "keypad");
	} else {
		elm_object_signal_emit(ctx->self, "hide,contact", "keypad");
		elm_object_part_text_set(ctx->self, "elm.text.contact-and-type",
						"");
		elm_object_part_text_set(ctx->self, "elm.text.contact", "");
		elm_object_part_text_set(ctx->self, "elm.text.phone.type", "");
	}

	elm_object_part_text_set(ctx->self, "elm.text.display", s);
	free(s);

	elm_object_signal_emit(ctx->self, "enable,save", "keypad");
	elm_object_signal_emit(ctx->self, "enable,backspace", "keypad");
}


static void _imei_show(void)
{
	const char *imei = ofono_modem_serial_get();
	gui_simple_popup("IMEI Request", imei ? imei : "No modem");
	INF("Show IMEI: %s", imei);
}

static void _pin_reply(void *data, OFono_Error error)
{
	const char *title = data;
	const char *msg = ofono_error_message_get(error);

	gui_simple_popup(title, msg);
	if (error == OFONO_ERROR_NONE)
		INF("%s", title);
	else
		ERR("%s", title);
}

static void _change_pin(const char *what, const char *old, const char *new)
{
	DBG("ask change of %s from %s to %s", what, old, new);
	ofono_modem_change_pin(what, old, new, _pin_reply, "PIN Change");
}

static void _reset_pin(const char *what, const char *old, const char *new)
{
	DBG("ask reset of %s using %s to %s", what, old, new);
	ofono_modem_reset_pin(what, old, new, _pin_reply, "PIN Reset");
}

static void _parse_pin(const char *str, char **old, char **new)
{
	const char *p1, *p2, *p3;
	int len1, len2, len3;

	p1 = strchr(str, '*');
	if (!p1)
		goto error;

	p2 = strchr(p1 + 1, '*');
	if (!p2)
		goto error;

	p3 = strchr(p2 + 1, '#');
	if (!p3)
		goto error;

	len1 = p1 - str;
	len2 = p2 - (p1 + 1);
	len3 = p3 - (p2 + 1);

	if ((len2 != len3) || (memcmp(p1 + 1, p2 + 1, len2) != 0)) {
		ERR("New PIN code check failed: '%.*s' '%.*s'",
			len2, p1 + 1,
			len3, p2 + 1);
		goto error;
	}

	*old = strndup(str, len1);
	*new = strndup(p1 + 1, len2);

	return;

error:
	ERR("Invalid PIN change format: %s", str);
}

static Eina_Bool _handle_if_mmi(Keypad *ctx)
{
	const char *str = eina_strbuf_string_get(ctx->number);
	size_t len = eina_strbuf_length_get(ctx->number);

	if (!str)
		return EINA_FALSE;

	if (len < sizeof("*#06#") - 1)
		return EINA_FALSE;

	if (str[0] != '*')
		return EINA_FALSE;
	if (str[len - 1] != '#')
		return EINA_FALSE;

	DBG("Possible MMI code: %s", str);

	str++;
	len -= 2;

	if (strcmp(str, "#06#") == 0) {
		_imei_show();
		eina_strbuf_reset(ctx->number);
		_number_display(ctx);
		return EINA_TRUE;
	} else if (strncmp(str, "*0", 2) == 0) {
		const char *what = NULL, *p = str + 2;
		char *old = NULL, *new = NULL;
		void (*action)(const char *, const char *, const char *) = NULL;

		if (*p == '4') {
			/* MMI codes to change PIN:
			 *  - **04*OLD_PIN*NEW_PIN*NEW_PIN#
			 *  - **042*OLD-PIN2*NEW_PIN2*NEW_PIN2#
			 */
			p++;
			action = _change_pin;
			if (*p == '*') {
				what = "pin";
				p++;
			} else if (strncmp(p, "2*", 2) == 0) {
				what = "pin2";
				p += 2;
			}

			if (what)
				_parse_pin(p, &old, &new);
		} else if (*p == '5') {
			/* MMI codes to reset PIN:
			 *  - **05*PIN_UNBLOCKING_KEY*NEW_PIN*NEW_PIN#
			 *  - **052*PIN2_UNBLOCKING_KEY*NEW_PIN2*NEW_PIN2#
			 */
			p++;
			action = _reset_pin;
			if (*p == '*') {
				what = "pin";
				p++;
			} else if (strncmp(p, "2*", 2) == 0) {
				what = "pin2";
				p += 2;
			}

			if (what)
				_parse_pin(p, &old, &new);
		}

		DBG("PIN management '%s' what=%s, old=%s, new=%s",
			str, what, old, new);
		if (action && what && old && new) {
			action(what, old, new);
			eina_strbuf_reset(ctx->number);
			_number_display(ctx);
		}

		free(old);
		free(new);
		return EINA_TRUE;
	}

	return EINA_FALSE;
}

static void _dial(Keypad *ctx)
{
	const char *number = eina_strbuf_string_get(ctx->number);

	INF("call %s", number);
	gui_dial(number);
	eina_stringshare_replace(&(ctx->last), number);
	eina_strbuf_reset(ctx->number);
	_number_display(ctx);
}

static void _ss_initiate_reply(void *data, OFono_Error err, const char *str)
{
	Keypad *ctx = data;

	DBG("e=%d, str=%s, ss_popup_show_timeout=%p, ss_popup=%p",
		err, str, ctx->ss_popup_show_timeout, ctx->ss_popup);

	ctx->ss_pending = NULL;

	if (ctx->ss_popup_show_timeout) {
		ecore_timer_del(ctx->ss_popup_show_timeout);
		ctx->ss_popup_show_timeout = NULL;
	}

	if ((err == OFONO_ERROR_NOT_RECOGNIZED) ||
		(err == OFONO_ERROR_INVALID_FORMAT)) {
		_dial(ctx);
		evas_object_del(ctx->ss_popup);
	} else if (err == OFONO_ERROR_OFFLINE) {
		simple_popup_title_set(ctx->ss_popup, "Offline");
		simple_popup_message_set(ctx->ss_popup,
						"System is Offline");
		simple_popup_button_dismiss_set(ctx->ss_popup);
		evas_object_show(ctx->ss_popup);
	} else if (err != OFONO_ERROR_NONE) {
		char buf[256];

		/* no popup? then it was canceled */
		if (!ctx->ss_popup)
			return;

		snprintf(buf, sizeof(buf), "Could not complete.<br>Error: %s",
				ofono_error_message_get(err));
		simple_popup_title_set(ctx->ss_popup, "Error");
		simple_popup_message_set(ctx->ss_popup, buf);
		simple_popup_button_dismiss_set(ctx->ss_popup);
		evas_object_show(ctx->ss_popup);
	} else {
		evas_object_del(ctx->ss_popup);
		eina_strbuf_reset(ctx->number);
		_number_display(ctx);

		ussd_start(str);
	}
}

static Eina_Bool _on_ss_popup_show_timeout(void *data)
{
	Keypad *ctx = data;

	if (ctx->ss_popup)
		evas_object_show(ctx->ss_popup);

	ctx->ss_popup_show_timeout = NULL;
	return EINA_FALSE;
}

static void _on_ss_popup_del(void *data, Evas *e __UNUSED__,
				Evas_Object *o __UNUSED__,
				void *event_info __UNUSED__)
{
	Keypad *ctx = data;

	ctx->ss_popup = NULL;

	if (ctx->ss_popup_show_timeout) {
		ecore_timer_del(ctx->ss_popup_show_timeout);
		ctx->ss_popup_show_timeout = NULL;
	}

	if (ctx->ss_pending) {
		ofono_pending_cancel(ctx->ss_pending);
		ctx->ss_pending = NULL;
	}
}

/* Procedure as ofono/doc/mmi-codes.txt:
 * - send number to SupplementaryServices.Initiate()
 * - if NotRecognized is returned, then forward VoiceCallManager.Dial()
 */
static void _call(Keypad *ctx)
{
	const char *number = eina_strbuf_string_get(ctx->number);
	int len = eina_strbuf_length_get(ctx->number);

	INF("calling %s...", number);

	if (len < 1)
		return;

	if (ctx->ss_popup)
		evas_object_del(ctx->ss_popup);

	if (ctx->ss_pending)
		ofono_pending_cancel(ctx->ss_pending);

	ctx->ss_popup = gui_simple_popup(NULL, NULL);
	evas_object_event_callback_add(ctx->ss_popup, EVAS_CALLBACK_DEL,
					_on_ss_popup_del, ctx);
	evas_object_hide(ctx->ss_popup);
	if (ctx->ss_popup_show_timeout)
		ecore_timer_del(ctx->ss_popup_show_timeout);
	ctx->ss_popup_show_timeout = ecore_timer_add(
		SS_POPUP_SHOW_TIMEOUT, _on_ss_popup_show_timeout, ctx);

	ctx->ss_pending = ofono_ss_initiate(number, _ss_initiate_reply, ctx);
}

static Eina_Bool _on_mod_timeout(void *data)
{
	Keypad *ctx = data;
	size_t len = eina_strbuf_length_get(ctx->number);

	if (len > 0) {
		const char *str = eina_strbuf_string_get(ctx->number);
		if (str[len - 1] == '0') {
			eina_strbuf_remove(ctx->number, len - 1, len);
			eina_strbuf_append_char(ctx->number, '+');
			_number_display(ctx);
		}
	}

	ctx->mod_timeout = NULL;
	return EINA_FALSE;
}

static Eina_Bool _on_rep_timeout(void *data)
{
	Keypad *ctx = data;
	size_t len = eina_strbuf_length_get(ctx->number);

	if (len > 0) {
		eina_strbuf_remove(ctx->number, len - 1, len);
		_number_display(ctx);
	}

	if (len == 1) {
		ctx->rep_timeout = NULL;
		return EINA_FALSE;
	}

	ecore_timer_interval_set(ctx->rep_timeout, REP_TIMEOUT);
	return EINA_TRUE;
}

static void _on_pressed(void *data, Evas_Object *obj __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	Keypad *ctx = data;
	DBG("ctx=%p, signal: %s", ctx, emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "pressed,"));
	emission += strlen("pressed,");

	if ((emission[0] >= '0') && (emission[0] <= '9')) {
		eina_strbuf_append_char(ctx->number, emission[0]);
		_number_display(ctx);

		if ((emission[0] == '0') &&
			(eina_strbuf_length_get(ctx->number) == 1)) {
			if (ctx->mod_timeout)
				ecore_timer_del(ctx->mod_timeout);
			ctx->mod_timeout = ecore_timer_add(MOD_TIMEOUT,
						_on_mod_timeout, ctx);
		}

	} else if (strcmp(emission, "backspace") == 0) {
		size_t len = eina_strbuf_length_get(ctx->number);
		if (len > 0) {
			eina_strbuf_remove(ctx->number, len - 1, len);
			_number_display(ctx);
			if (len > 1) {
				if (ctx->rep_timeout)
					ecore_timer_del(ctx->rep_timeout);
				ctx->rep_timeout = ecore_timer_add(
					REP_TIMEOUT_INIT, _on_rep_timeout, ctx);
			}
		}

	} else if (strcmp(emission, "star") == 0) {
		eina_strbuf_append_char(ctx->number, '*');
		_number_display(ctx);
	} else if (strcmp(emission, "hash") == 0) {
		eina_strbuf_append_char(ctx->number, '#');
		_number_display(ctx);
	}

	_handle_if_mmi(ctx);
}

static void _on_released(void *data, Evas_Object *obj __UNUSED__,
				const char *emission,
				const char *source __UNUSED__)
{
	Keypad *ctx = data;
	DBG("ctx=%p, signal: %s", ctx, emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "released,"));
	emission += strlen("released,");

	if (ctx->mod_timeout) {
		if (emission[0] != '0')
			ERR("Expected '0' but got '%s' instead", emission);
		ecore_timer_del(ctx->mod_timeout);
		ctx->mod_timeout = NULL;
	}

	if (ctx->rep_timeout) {
		if (strcmp(emission, "backspace") != 0)
			ERR("Expected 'backspace' but got '%s' instead",
				emission);
		ecore_timer_del(ctx->rep_timeout);
		ctx->rep_timeout = NULL;
	}
}

static void _on_clicked(void *data, Evas_Object *obj __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	Keypad *ctx = data;
	DBG("ctx=%p, signal: %s", ctx, emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "clicked,"));
	emission += strlen("clicked,");

	if (strcmp(emission, "call") == 0) {
		if (eina_strbuf_length_get(ctx->number) > 0)
			_call(ctx);
		else if (ctx->last) {
			eina_strbuf_append(ctx->number, ctx->last);
			_number_display(ctx);
		}
	} else if (strcmp(emission, "save") == 0) {
		ERR("TODO save contact %s!",
			eina_strbuf_string_get(ctx->number));
	}
}

static void _on_del(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Keypad *ctx = data;

	if (ctx->ss_pending)
		ofono_pending_cancel(ctx->ss_pending);

	if (ctx->mod_timeout)
		ecore_timer_del(ctx->mod_timeout);
	if (ctx->rep_timeout)
		ecore_timer_del(ctx->rep_timeout);
	if (ctx->ss_popup_show_timeout)
		ecore_timer_del(ctx->ss_popup_show_timeout);

	eina_strbuf_free(ctx->number);
	eina_stringshare_del(ctx->last);
	free(ctx);
}

Evas_Object *keypad_add(Evas_Object *parent)
{
	Keypad *ctx;
	Evas_Object *obj = layout_add(parent, "keypad");
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);

	ctx = calloc(1, sizeof(Keypad));
	ctx->self = obj;
	ctx->number = eina_strbuf_new();

	evas_object_data_set(obj, "keypad.ctx", ctx);

	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL,
					_on_del, ctx);
	elm_object_signal_callback_add(obj, "pressed,*", "keypad",
					_on_pressed, ctx);
	elm_object_signal_callback_add(obj, "released,*", "keypad",
					_on_released, ctx);
	elm_object_signal_callback_add(obj, "clicked,*", "keypad",
					_on_clicked, ctx);

	elm_object_part_text_set(obj, "elm.text.display", "");
	elm_object_part_text_set(ctx->self, "elm.text.contact-and-type", "");
	elm_object_part_text_set(ctx->self, "elm.text.contact", "");
	elm_object_part_text_set(ctx->self, "elm.text.phone.type", "");
	elm_object_signal_emit(obj, "hide,contact", "keypad");
	elm_object_signal_emit(obj, "disable,save", "keypad");
	elm_object_signal_emit(obj, "disable,backspace", "keypad");

	elm_object_focus_allow_set(obj, EINA_TRUE);

	return obj;
}

void keypad_number_set(Evas_Object *obj, const char *number,
			Eina_Bool auto_dial)
{
	Keypad *ctx;
	EINA_SAFETY_ON_NULL_RETURN(obj);
	EINA_SAFETY_ON_NULL_RETURN(number);

	ctx = evas_object_data_get(obj, "keypad.ctx");
	EINA_SAFETY_ON_NULL_RETURN(ctx);

	eina_strbuf_reset(ctx->number);
	eina_strbuf_append(ctx->number, number);
	_number_display(ctx);
	if (auto_dial)
		_call(ctx);
}
