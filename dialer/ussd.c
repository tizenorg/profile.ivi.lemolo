#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "log.h"
#include "gui.h"
#include "ofono.h"

typedef struct _USSD
{
	Evas_Object *popup;
	OFono_Pending *pending;
	OFono_USSD_State state;
	const char *message;
	OFono_Callback_List_Modem_Node *cb_changed;
} USSD;

static void _ussd_respond_reply(void *data, OFono_Error err, const char *str)
{
	USSD *ctx = data;

	DBG("ctx=%p, e=%d, str=%s, popup=%p", ctx, err, str, ctx->popup);

	ctx->pending = NULL;

	/* no popup? then it was canceled */
	if (!ctx->popup)
		return;

	if (err == OFONO_ERROR_NONE) {
		eina_stringshare_replace(&(ctx->message), str);
		gui_simple_popup_message_set(ctx->popup, ctx->message);
	} else if (err == OFONO_ERROR_OFFLINE) {
		gui_simple_popup_title_set(ctx->popup, "Offline");
		gui_simple_popup_message_set(ctx->popup, "System is Offline");
		gui_simple_popup_button_dismiss_set(ctx->popup);
	} else {
		char buf[256];

		if (ctx->state == OFONO_USSD_STATE_USER_RESPONSE)
			snprintf(buf, sizeof(buf),
					"Could not complete.<br>Error: %s.<br>"
					"Try again:<br><br>%s",
					ofono_error_message_get(err),
					ctx->message);
		else
			snprintf(buf, sizeof(buf),
					"Could not complete.<br>Error: %s.",
					ofono_error_message_get(err));

		gui_simple_popup_title_set(ctx->popup, "Error");
		gui_simple_popup_message_set(ctx->popup, buf);
	}
}

static void _ussd_respond(void *data, Evas_Object *o __UNUSED__,
				void *event_info __UNUSED__)
{
	USSD *ctx = data;
	const char *markup = gui_simple_popup_entry_get(ctx->popup);
	char *utf8;

	DBG("ctx=%p, markup=%s, pending=%p, popup=%p",
		ctx, markup, ctx->pending, ctx->popup);

	if (!markup)
		return;

	/* do not respond if there is a pending ss call */
	EINA_SAFETY_ON_TRUE_RETURN(ctx->pending != NULL);

	utf8 = elm_entry_markup_to_utf8(markup);
	EINA_SAFETY_ON_NULL_RETURN(utf8);

	ctx->pending = ofono_ussd_respond(utf8, _ussd_respond_reply, ctx);
	free(utf8);

	gui_simple_popup_message_set(ctx->popup, NULL);
	gui_simple_popup_entry_disable(ctx->popup);
}

static void _ussd_cancel_reply(void *data, OFono_Error e)
{
	USSD *ctx = data;

	DBG("ctx=%p, e=%d, pending=%p, popup=%p",
		ctx, e, ctx->pending, ctx->popup);

	if (ctx->popup)
		evas_object_del(ctx->popup);
}

static void _ussd_cancel(void *data, Evas_Object *o __UNUSED__,
				void *event_info __UNUSED__)
{
	USSD *ctx = data;
	ofono_ussd_cancel(_ussd_cancel_reply, ctx);
}

static void _ofono_changed(void *data)
{
	USSD *ctx = data;
	OFono_USSD_State state = ofono_ussd_state_get();

	DBG("ctx=%p, state=%d (was: %d), pending=%p",
		ctx, state, ctx->state, ctx->pending);

	if (ctx->state == state)
		return;

	if (state != OFONO_USSD_STATE_USER_RESPONSE) {
		gui_simple_popup_entry_disable(ctx->popup);
		gui_simple_popup_button_dismiss_set(ctx->popup);
	} else {
		gui_simple_popup_entry_enable(ctx->popup);
		gui_simple_popup_buttons_set(ctx->popup,
						"Respond",
						"dialer",
						_ussd_respond,
						"Cancel",
						"dialer-caution",
						_ussd_cancel,
						ctx);
	}

	ctx->state = state;
}

static void _on_del(void *data, Evas *e __UNUSED__, Evas_Object *o __UNUSED__,
			void *event_info __UNUSED__)
{
	USSD *ctx = data;

	DBG("ctx=%p, pending=%p, cb_changed=%p",
		ctx, ctx->pending, ctx->cb_changed);

	ctx->popup = NULL;

	if (ctx->pending) {
		ofono_ussd_cancel(NULL, NULL);
		ofono_pending_cancel(ctx->pending);
	}

	eina_stringshare_del(ctx->message);
	ofono_modem_changed_cb_del(ctx->cb_changed);
}

void ussd_start(const char *message)
{
	USSD *ctx;

	EINA_SAFETY_ON_NULL_RETURN(message);
	ctx = calloc(1, sizeof(USSD));
	EINA_SAFETY_ON_NULL_RETURN(ctx);

	ctx->message = eina_stringshare_add(message);
	ctx->state = -1;
	ctx->cb_changed = ofono_modem_changed_cb_add(_ofono_changed, ctx);

	ctx->popup = gui_simple_popup("Supplementary Services", ctx->message);
	evas_object_event_callback_add(ctx->popup, EVAS_CALLBACK_DEL,
					_on_del, ctx);
	_ofono_changed(ctx);
}
