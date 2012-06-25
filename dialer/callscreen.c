#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "log.h"
#include "gui.h"
#include "ofono.h"

typedef struct _Callscreen
{
	Evas_Object *self;
	OFono_Call *in_use;
	Eina_List *calls;
	struct {
		const char *number;
		Evas_Object *popup;
	} disconnected;
} Callscreen;

static void _call_in_use_update(Callscreen *ctx)
{
	const Eina_List *n;
	OFono_Call *c, *found = NULL;
	OFono_Call_State found_state = OFONO_CALL_STATE_DISCONNECTED;
	static const int state_priority[] = {
		[OFONO_CALL_STATE_DISCONNECTED] = 0,
		[OFONO_CALL_STATE_ACTIVE] = 6,
		[OFONO_CALL_STATE_HELD] = 1,
		[OFONO_CALL_STATE_DIALING] = 5,
		[OFONO_CALL_STATE_ALERTING] = 4,
		[OFONO_CALL_STATE_INCOMING] = 3,
		[OFONO_CALL_STATE_WAITING] = 2
	};

	if (ctx->in_use)
		return;

	EINA_LIST_FOREACH(ctx->calls, n, c) {
		OFono_Call_State state = ofono_call_state_get(c);

		DBG("compare %p (%d) to %p (%d)", found, found_state,
			c, state);
		if (state_priority[state] > state_priority[found_state]) {
			found_state = state;
			found = c;
		}
	}

	if (!found) {
		DBG("No calls usable");
		return;
	}

	DBG("found=%p, state=%d", found, found_state);
	ctx->in_use = found;
	gui_call_enter();
}

static void _call_disconnected_done(Callscreen *ctx)
{
	if (!ctx->calls)
		gui_call_exit();
	else
		_call_in_use_update(ctx);
}

static void _popup_close(void *data, Evas_Object *o __UNUSED__, void *event __UNUSED__)
{
	Callscreen *ctx = data;

	evas_object_del(ctx->disconnected.popup);
	ctx->disconnected.popup = NULL;

	eina_stringshare_replace(&ctx->disconnected.number, NULL);

	_call_disconnected_done(ctx);
}

static void _popup_redial(void *data, Evas_Object *o __UNUSED__, void *event __UNUSED__)
{
	Callscreen *ctx = data;

	ofono_dial(ctx->disconnected.number, NULL, NULL, NULL);
	_popup_close(ctx, NULL, NULL);
}

static void _call_disconnected_show(Callscreen *ctx, OFono_Call *c,
					const char *reason)
{
	Evas_Object *p, *bt;
	const char *number, *title;
	char msg[1024];

	DBG("ctx=%p, call=%p, previous=%s, disconnected=%p (%s)",
		ctx, ctx->in_use, ctx->disconnected.number, c, reason);

	if ((ctx->in_use) && (ctx->in_use != c))
		return;
	ctx->in_use = NULL;

	if ((strcmp(reason, "local") == 0) || (strcmp(reason, "remote") == 0)) {
		_call_disconnected_done(ctx);
		return;
	}

	number = ofono_call_line_id_get(c);
	if ((!number) || (number[0] == '\0')) {
		_call_disconnected_done(ctx);
		return;
	}

	if (ctx->disconnected.number)
		return;

	if (strcmp(reason, "network") == 0)
		title = "Network Disconnected!";
	else
		title = "Disconnected!";

	snprintf(msg, sizeof(msg), "Try to redial %s", number);

	eina_stringshare_replace(&ctx->disconnected.number, number);

	ctx->disconnected.popup = p = elm_popup_add(ctx->self);
	evas_object_size_hint_weight_set(p, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	elm_object_part_text_set(p, "title,text", title);
	elm_object_text_set(p, msg);

	bt = elm_button_add(p);
	elm_object_text_set(bt, "Close");
	elm_object_part_content_set(p, "button1", bt);
	evas_object_smart_callback_add(bt, "clicked", _popup_close, ctx);

	bt = elm_button_add(p);
	elm_object_text_set(bt, "Redial");
	elm_object_part_content_set(p, "button2", bt);
	evas_object_smart_callback_add(bt, "clicked", _popup_redial, ctx);

	evas_object_show(p);
}

static void _on_pressed(void *data, Evas_Object *obj __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, call=%p, signal: %s", ctx, ctx->in_use, emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "pressed,"));
	emission += strlen("pressed,");

}

static void _on_released(void *data, Evas_Object *obj __UNUSED__,
				const char *emission,
				const char *source __UNUSED__)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, call=%p, signal: %s", ctx, ctx->in_use, emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "released,"));
	emission += strlen("released,");

}

static void _on_clicked(void *data, Evas_Object *obj __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, call=%p, signal: %s", ctx, ctx->in_use, emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "clicked,"));
	emission += strlen("clicked,");

	if (strcmp(emission, "hangup") == 0) {
		if (ctx->in_use)
			ofono_call_hangup(ctx->in_use, NULL, NULL);
	} else if (strcmp(emission, "answer") == 0) {
		if (ctx->in_use)
			ofono_call_answer(ctx->in_use, NULL, NULL);
	}
}

static void _call_added(void *data, OFono_Call *c)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, call=%p, added=%p", ctx, ctx->in_use, c);
	ctx->calls = eina_list_append(ctx->calls, c);
	if ((!ctx->in_use) && (ofono_call_state_valid_check(c))) {
		ctx->in_use = c;
		gui_call_enter();
	}
}

static void _call_removed(void *data, OFono_Call *c)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, call=%p, removed=%p", ctx, ctx->in_use, c);
	ctx->calls = eina_list_remove(ctx->calls, c);
	_call_disconnected_show(ctx, c, "local");
}

static void _call_changed(void *data, OFono_Call *c)
{
	Callscreen *ctx = data;
	OFono_Call_State state;
	const char *contact, *status, *signal = "hide,answer";

	DBG("ctx=%p, call=%p, changed=%p", ctx, ctx->in_use, c);

	_call_in_use_update(ctx);
	if (ctx->in_use != c)
		return;

	contact = ofono_call_name_get(c);
	if ((!contact) || (contact[0] == '\0'))
		contact = ofono_call_line_id_get(c);

	state = ofono_call_state_get(c);
	switch (state) {
	case OFONO_CALL_STATE_DISCONNECTED:
		status = "Disconnected";
		break;
	case OFONO_CALL_STATE_ACTIVE:
		status = "Active";
		break;
	case OFONO_CALL_STATE_HELD:
		status = "Held";
		break;
	case OFONO_CALL_STATE_DIALING:
		status = "Dialing...";
		break;
	case OFONO_CALL_STATE_ALERTING:
		status = "Alerting...";
		break;
	case OFONO_CALL_STATE_INCOMING:
		status = "Incoming...";
		signal = "show,answer";
		break;
	case OFONO_CALL_STATE_WAITING:
		status = "Waiting...";
		break;
	default:
		status = "?";
	}

	elm_object_part_text_set(ctx->self, "elm.text.name", contact);
	elm_object_part_text_set(ctx->self, "elm.text.status", status);
	elm_object_signal_emit(ctx->self, signal, "call");

	if (state == OFONO_CALL_STATE_DISCONNECTED)
		_call_disconnected_show(ctx, c, "local");
}

static void _call_disconnected(void *data, OFono_Call *c, const char *reason)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, call=%p, disconnected=%p (%s)",
		ctx, ctx->in_use, c, reason);

	EINA_SAFETY_ON_NULL_RETURN(reason);
	_call_disconnected_show(ctx, c, reason);
}

static void _on_del(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Callscreen *ctx = data;

	ofono_call_added_cb_set(NULL, NULL);
	ofono_call_removed_cb_set(NULL, NULL);
	ofono_call_changed_cb_set(NULL, NULL);
	ofono_call_disconnected_cb_set(NULL, NULL);

	eina_stringshare_del(ctx->disconnected.number);

	eina_list_free(ctx->calls);
	free(ctx);
}

Evas_Object *callscreen_add(Evas_Object *parent) {
	Callscreen *ctx;
	Evas_Object *obj = gui_layout_add(parent, "call");
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);

	ctx = calloc(1, sizeof(Callscreen));
	ctx->self = obj;

	evas_object_data_set(obj, "callscreen.ctx", ctx);

	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL,
					_on_del, ctx);
	elm_object_signal_callback_add(obj, "pressed,*", "call",
					_on_pressed, ctx);
	elm_object_signal_callback_add(obj, "released,*", "call",
					_on_released, ctx);
	elm_object_signal_callback_add(obj, "clicked,*", "call",
					_on_clicked, ctx);

	elm_object_part_text_set(obj, "elm.text.name", "");
	elm_object_part_text_set(obj, "elm.text.status", "");

	ofono_call_added_cb_set(_call_added, ctx);
	ofono_call_removed_cb_set(_call_removed, ctx);
	ofono_call_changed_cb_set(_call_changed, ctx);
	ofono_call_disconnected_cb_set(_call_disconnected, ctx);

	return obj;
}
