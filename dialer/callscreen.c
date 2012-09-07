#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "log.h"
#include "gui.h"
#include "ofono.h"
#include "util.h"

typedef struct _Callscreen
{
	Evas_Object *self;
	Evas_Object *gui_activecall; /* for gui.h */
	struct {
		Evas_Object *sc;
		Evas_Object *bx;
		Eina_List *calls;
		double start;
	} multiparty;
	struct {
		OFono_Call *first;
		OFono_Call *second;
		OFono_Call *current; /* first or second */
		OFono_Call *waiting;
		OFono_Call *incoming; /* should be first && current */
		Eina_List *list;
	} calls;
	Ecore_Timer *elapsed_updater;
	struct {
		Eina_Strbuf *todo;
		OFono_Pending *pending;
	} tones;
	struct {
		const void *call;
		const char *number;
		Evas_Object *popup;
	} disconnected;
} Callscreen;

static void _on_active_call_clicked(void *data __UNUSED__,
					Evas_Object *o __UNUSED__,
					const char *emission __UNUSED__,
					const char *source __UNUSED__)
{
	gui_call_enter();
}

static OFono_Callback_List_Modem_Node *callback_node_modem_changed = NULL;

static OFono_Callback_List_Call_Node *callback_node_call_added = NULL;
static OFono_Callback_List_Call_Node *callback_node_call_removed = NULL;
static OFono_Callback_List_Call_Node *callback_node_call_changed = NULL;

static OFono_Callback_List_Call_Disconnected_Node
*callback_node_call_disconnected = NULL;

static char *_call_name_or_id(const OFono_Call *call)
{
	const char *s = ofono_call_line_id_get(call);
	Contact_Info *info;

	if (!s)
		return NULL;;

	info = gui_contact_search(s, NULL);
	if (info)
		return strdup(contact_info_full_name_get(info));

	return phone_format(s);
}

static char *_call_name_get(const Callscreen *ctx __UNUSED__,
				const OFono_Call *call)
{
	if (!ofono_call_multiparty_get(call))
		return _call_name_or_id(call);

	return strdup("Conference");
}

static const char *_call_type_get(const OFono_Call *call)
{
	const char *type;
	const char *s = ofono_call_line_id_get(call);
	Contact_Info *info;

	if (!s)
		return NULL;

	info = gui_contact_search(s, &type);
	if (info)
		return type;

	return NULL;
}

static void _on_mp_hangup(void *data, Evas_Object *o __UNUSED__,
				const char *emission __UNUSED__,
				const char *source __UNUSED__)
{
	OFono_Call *call = data;
	DBG("User ask hangup of multiparty call=%p", call);
	ofono_call_hangup(call, NULL, NULL);
}

static void _on_mp_pvt_reply(void *data, OFono_Error err)
{
	Callscreen *ctx = data;

	DBG("PrivateChat: err=%d", err);

	if (err == OFONO_ERROR_NONE)
		elm_object_signal_emit(ctx->self, "multiparty,private", "call");
}

static void _on_mp_pvt(void *data, Evas_Object *o,
				const char *emission __UNUSED__,
				const char *source __UNUSED__)
{
	Callscreen *ctx = evas_object_data_get(o, "callscreen.ctx");
	OFono_Call *call = data;
	DBG("User ask private chat of multiparty call=%p", call);
	ofono_private_chat(call, _on_mp_pvt_reply, ctx);
}

static void _on_raise(void *data __UNUSED__, Evas_Object *o,
				const char *emission __UNUSED__,
				const char *source __UNUSED__)
{
	evas_object_raise(o);
}

static void _multiparty_update(Callscreen *ctx)
{
	Eina_List *new = NULL, *old = NULL;
	const Eina_List *n1, *n2;
	OFono_Call *c;
	Evas_Object *it;

	EINA_LIST_FOREACH(ctx->calls.list, n1, c) {
		if (ofono_call_multiparty_get(c))
			new = eina_list_append(new, c);
	}

	old = ctx->multiparty.calls;
	if (eina_list_count(new) != eina_list_count(old))
		goto repopulate;

	for (n1 = new, n2 = old; n1 && n2; n1 = n1->next, n2 = n2->next) {
		if (n1->data != n2->data)
			break;
	}

	if (n1)
		goto repopulate;

	eina_list_free(new);
	return;

repopulate:
	if ((new) && (!ctx->multiparty.calls))
		ctx->multiparty.start = ecore_loop_time_get();

	eina_list_free(ctx->multiparty.calls);
	ctx->multiparty.calls = new;

	elm_box_clear(ctx->multiparty.bx);

	if (!new) {
		ctx->multiparty.start = -1.0;
		elm_object_signal_emit(ctx->self, "hide,multiparty-details",
					"call");
		return;
	}

	EINA_LIST_FOREACH(new, n1, c) {
		char *name, *number;
		const char *type;

		name = _call_name_or_id(c);
		type = _call_type_get(c);
		number = phone_format(ofono_call_line_id_get(c));

		it = gui_layout_add(ctx->multiparty.bx, "multiparty-details");
		evas_object_size_hint_align_set(it,
						EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(it);

		elm_object_part_text_set(it, "elm.text.name", name);
		elm_object_part_text_set(it, "elm.text.number", number);
		elm_object_part_text_set(it, "elm.text.type", type);

		if (strcmp(name, number) == 0)
			elm_object_signal_emit(it, "hide,name", "call");
		else
			elm_object_signal_emit(it, "show,name", "call");

		elm_object_signal_callback_add(it, "clicked,hangup", "call",
						_on_mp_hangup, c);
		elm_object_signal_callback_add(it, "clicked,private", "call",
						_on_mp_pvt, c);
		elm_object_signal_callback_add(it, "raise", "call",
						_on_raise, NULL);

		evas_object_data_set(it, "callscreen.ctx", ctx);
		elm_box_pack_end(ctx->multiparty.bx, it);

		free(name);
		free(number);
	}
	elm_object_signal_emit(ctx->self, "show,multiparty-details", "call");
}

static void _multiparty_private_available_update(Callscreen *ctx)
{
	Eina_List *lst = elm_box_children_get(ctx->multiparty.bx);
	const char *sig = ctx->calls.second ? "hide,private" : "show,private";
	Evas_Object *it;

	EINA_LIST_FREE(lst, it)
		elm_object_signal_emit(it, sig, "call");
}

static void _call_text_set(Callscreen *ctx, unsigned int id,
				const char *part, const char *str)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "elm.text.%u.%s", id, part);
	elm_object_part_text_set(ctx->self, buf, str ? str : "");
}

static const char *_call_state_str(OFono_Call_State state)
{
	switch (state) {
	case OFONO_CALL_STATE_DISCONNECTED:
		return "Disconnected";
	case OFONO_CALL_STATE_ACTIVE:
		return "Active";
	case OFONO_CALL_STATE_HELD:
		return "On Hold";
	case OFONO_CALL_STATE_DIALING:
		return "Dialing...";
	case OFONO_CALL_STATE_ALERTING:
		return "Alerting...";
	case OFONO_CALL_STATE_INCOMING:
		return "Incoming...";
	case OFONO_CALL_STATE_WAITING:
		return "Waiting...";
	default:
		ERR("unknown state: %d", state);
		return NULL;
	}
}

static const char *_call_state_id(OFono_Call_State state)
{
	switch (state) {
	case OFONO_CALL_STATE_DISCONNECTED:
		return "disconnected";
	case OFONO_CALL_STATE_ACTIVE:
		return "active";
	case OFONO_CALL_STATE_HELD:
		return "held";
	case OFONO_CALL_STATE_DIALING:
		return "dialing";
	case OFONO_CALL_STATE_ALERTING:
		return "alerting";
	case OFONO_CALL_STATE_INCOMING:
		return "incoming";
	case OFONO_CALL_STATE_WAITING:
		return "waiting";
	default:
		ERR("unknown state: %d", state);
		return NULL;
	}
}

static void _call_show(Callscreen *ctx, unsigned int id, const OFono_Call *c)
{
	char *contact = _call_name_get(ctx, c);
	const char *type = _call_type_get(c);
	const char *status = _call_state_str(ofono_call_state_get(c));

	_call_text_set(ctx, id, "name", contact);
	_call_text_set(ctx, id, "phone.type", type);
	_call_text_set(ctx, id, "status", status);
	_call_text_set(ctx, id, "elapsed", "");

	free(contact);
}

static void _call_elapsed_update(Callscreen *ctx, unsigned int id,
					const OFono_Call *c)
{
	Edje_Message_Float msgf = {0};
	Evas_Object *ed;
	OFono_Call_State state = ofono_call_state_get(c);
	double start, now, elapsed;
	char part[128], buf[128] = "";

	if ((state != OFONO_CALL_STATE_ACTIVE) &&
		(state != OFONO_CALL_STATE_HELD))
		goto end;

	if (ofono_call_multiparty_get(c))
		start = ctx->multiparty.start;
	else
		start = ofono_call_start_time_get(c);

	if (start < 0) {
		ERR("Unknown start time for call");
		goto end;
	}

	now = ecore_loop_time_get();
	elapsed =  now - start;
	if (elapsed < 0) {
		ERR("Time rewinded? %f - %f = %f", now, start, elapsed);
		goto end;
	}

	msgf.val = elapsed;
	if (elapsed > 3600)
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
				(int)elapsed / 3600,
				(int)elapsed % 3600 / 60,
				(int)elapsed % 60);
	else
		snprintf(buf, sizeof(buf), "%02d:%02d",
				(int)elapsed / 60,
				(int)elapsed % 60);

end:
	ed = elm_layout_edje_get(ctx->self);
	edje_object_message_send(ed, EDJE_MESSAGE_FLOAT, 10 + id, &msgf);

	snprintf(part, sizeof(part), "elm.text.%u.elapsed", id);
	elm_object_part_text_set(ctx->self, part, buf);
}

static void _activecall_elapsed_update(Callscreen *ctx)
{
	Edje_Message_Float msgf = {0};
	Evas_Object *ed;
	OFono_Call_State state = ofono_call_state_get(ctx->calls.current);
	double start, now, elapsed;
	char buf[128] = "";

	if ((state != OFONO_CALL_STATE_ACTIVE) &&
		(state != OFONO_CALL_STATE_HELD))
		goto end;

	if (ofono_call_multiparty_get(ctx->calls.current))
		start = ctx->multiparty.start;
	else
		start = ofono_call_start_time_get(ctx->calls.current);

	if (start < 0) {
		ERR("Unknown start time for call");
		goto end;
	}

	now = ecore_loop_time_get();
	elapsed =  now - start;
	if (elapsed < 0) {
		ERR("Time rewinded? %f - %f = %f", now, start, elapsed);
		goto end;
	}

	msgf.val = elapsed;
	if (elapsed > 3600)
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
				(int)elapsed / 3600,
				(int)elapsed % 3600 / 60,
				(int)elapsed % 60);
	else
		snprintf(buf, sizeof(buf), "%02d:%02d",
				(int)elapsed / 60,
				(int)elapsed % 60);

end:
	ed = elm_layout_edje_get(ctx->gui_activecall);
	edje_object_message_send(ed, EDJE_MESSAGE_FLOAT, 1, &msgf);

	elm_object_part_text_set(ctx->gui_activecall, "elm.text.elapsed", buf);
}

static void _call_update(Callscreen *ctx, unsigned int id, const OFono_Call *c)
{
	Evas_Object *o = ctx->self;
	OFono_Call_State state = ofono_call_state_get(c);
	const char *status_label = _call_state_str(state);
	const char *status_id = _call_state_id(state);
	char buf[128];

	_call_text_set(ctx, id, "status", status_label);

	snprintf(buf, sizeof(buf), "state,%u,%s", id, status_id);
	elm_object_signal_emit(o, buf, "call");

	snprintf(buf, sizeof(buf), "%s,%u,multiparty",
			ofono_call_multiparty_get(c) ? "show" : "hide", id);
	elm_object_signal_emit(o, buf, "call");

	if ((state == OFONO_CALL_STATE_ACTIVE) ||
		(state == OFONO_CALL_STATE_HELD)) {
		_call_elapsed_update(ctx, id, c);
		snprintf(buf, sizeof(buf), "show,%u,elapsed", id);
		elm_object_signal_emit(o, buf, "call");
	} else {
		Edje_Message_Float msgf;

		_call_text_set(ctx, id, "elapsed", "");
		snprintf(buf, sizeof(buf), "hide,%u,elapsed", id);
		elm_object_signal_emit(o, buf, "call");

		msgf.val = 0.0;
		edje_object_message_send(elm_layout_edje_get(o),
				EDJE_MESSAGE_FLOAT, 10 + id, &msgf);
	}
}

static void _call_clear(Callscreen *ctx, unsigned int id)
{
	Evas_Object *o = ctx->self;
	Edje_Message_Float msgf;
	char buf[128];

	_call_text_set(ctx, id, "name", "");
	_call_text_set(ctx, id, "phone.type", "");
	_call_text_set(ctx, id, "status", "");
	_call_text_set(ctx, id, "elapsed", "");

	snprintf(buf, sizeof(buf), "state,%u,disconnected", id);
	elm_object_signal_emit(o, buf, "call");

	snprintf(buf, sizeof(buf), "hide,%u,multiparty", id);
	elm_object_signal_emit(o, buf, "call");

	snprintf(buf, sizeof(buf), "hide,%u,elapsed", id);
	elm_object_signal_emit(o, buf, "call");

	msgf.val = 0.0;
	edje_object_message_send(elm_layout_edje_get(o),
					EDJE_MESSAGE_FLOAT, 10 + id, &msgf);
}

static void _activecall_update(Callscreen *ctx)
{
	OFono_Call_State state = ofono_call_state_get(ctx->calls.current);
	char *contact = _call_name_get(ctx, ctx->calls.current);
	const char *type = _call_type_get(ctx->calls.current);
	const char *status_label = _call_state_str(state);
	const char *status_id = _call_state_id(state);
	Evas_Object *o = ctx->gui_activecall;
	char buf[128];

	elm_object_part_text_set(o, "elm.text.name", contact);
	elm_object_part_text_set(o, "elm.text.phone.type", type);
	elm_object_part_text_set(o, "elm.text.status", status_label);

	snprintf(buf, sizeof(buf), "state,%s", status_id);
	elm_object_signal_emit(o, buf, "call");

	_activecall_elapsed_update(ctx);

	free(contact);
}

static void _call_waiting_set(Callscreen *ctx, OFono_Call *c)
{
	Evas_Object *o = ctx->self;

	if (!c) {
		elm_object_part_text_set(o, "elm.text.waiting", "");
		elm_object_signal_emit(o, "hide,waiting", "call");
	} else {
		char *name = _call_name_get(ctx, c);
		elm_object_part_text_set(o, "elm.text.waiting", name);
		elm_object_signal_emit(o, "show,waiting", "call");
		free(name);
	}

	ctx->calls.waiting = c;
}

static void _call_incoming_set(Callscreen *ctx, OFono_Call *c)
{
	Evas_Object *o = ctx->self;

	if (!c)
		elm_object_signal_emit(o, "hide,answer", "call");
	else
		elm_object_signal_emit(o, "show,answer", "call");

	ctx->calls.incoming = c;
}

static void _call_current_actions_update(Callscreen *ctx)
{
	Evas_Object *o = ctx->self;
	OFono_Call_State s;

	if (ctx->calls.current)
		s = ofono_call_state_get(ctx->calls.current);
	else
		s = OFONO_CALL_STATE_DISCONNECTED;

	if ((s == OFONO_CALL_STATE_ACTIVE) || (s == OFONO_CALL_STATE_HELD))
		elm_object_signal_emit(o, "enable,actions", "call");
	else
		elm_object_signal_emit(o, "disable,actions", "call");
}

static void _call_current_set(Callscreen *ctx, OFono_Call *c)
{
	Evas_Object *o = ctx->self;
	Eina_Bool need_activecall_update = EINA_FALSE;

	DBG("was=%p, now=%p, first=%p, second=%p",
		ctx->calls.current, c, ctx->calls.first, ctx->calls.second);

	if (!c) {
		elm_object_signal_emit(o, "active,disconnected", "call");
		if (ctx->gui_activecall) {
			gui_activecall_set(NULL);
			evas_object_del(ctx->gui_activecall);
			ctx->gui_activecall = NULL;
		}
	} else {
		if (!ctx->gui_activecall) {
			ctx->gui_activecall = gui_layout_add(o, "activecall");
			elm_object_signal_callback_add(
				ctx->gui_activecall, "clicked", "call",
				_on_active_call_clicked, ctx);
			gui_activecall_set(ctx->gui_activecall);
			need_activecall_update = EINA_TRUE;
		} else if (ctx->calls.current != c)
			need_activecall_update = EINA_TRUE;
	}

	ctx->calls.current = c;

	_call_current_actions_update(ctx);

	if (need_activecall_update)
		_activecall_update(ctx);
}

static void _call_first_set(Callscreen *ctx, OFono_Call *c)
{
	DBG("was=%p, now=%p", ctx->calls.first, c);

	if (!c)
		_call_clear(ctx, 1);
	else {
		_call_show(ctx, 1, c);
		_call_update(ctx, 1, c);
	}

	ctx->calls.first = c;
}

static void _call_second_set(Callscreen *ctx, OFono_Call *c)
{
	DBG("was=%p, now=%p", ctx->calls.second, c);

	if (!c) {
		_call_clear(ctx, 2);
		elm_object_signal_emit(ctx->self, "calls,1", "call");
	} else {
		_call_show(ctx, 2, c);
		_call_update(ctx, 2, c);
		elm_object_signal_emit(ctx->self, "calls,2", "call");
	}

	ctx->calls.second = c;
}

static void _call_auto_place(Callscreen *ctx, OFono_Call *c)
{
	OFono_Call_State state = ofono_call_state_get(c);
	Eina_Bool is_multiparty = ofono_call_multiparty_get(c);

	DBG("ctx=%p, %p, %p, call=%p, state=%d, multiparty=%d",
		ctx, ctx->calls.first, ctx->calls.second, c,
		state, is_multiparty);

	if (!ctx->calls.first) {
		DBG("first call %p", c);
		_call_first_set(ctx, c);
		_call_current_set(ctx, c);
		return;
	}

	if (is_multiparty) {
		if (ofono_call_multiparty_get(ctx->calls.first)) {
			DBG("call %p is already part of first multiparty", c);
			return;
		} else if (ctx->calls.second &&
				ofono_call_multiparty_get(ctx->calls.second)) {
			DBG("call %p is already part of second multiparty", c);
			return;
		}
	}

	DBG("second call %p", c);
	_call_second_set(ctx, c);
	if (state == OFONO_CALL_STATE_ACTIVE)
		_call_current_set(ctx, c);
}

static OFono_Call *_multiparty_find_other(const Callscreen *ctx,
						const OFono_Call *c)
{
	OFono_Call *itr;
	const Eina_List *n;
	EINA_LIST_FOREACH(ctx->calls.list, n, itr) {
		if (itr == c)
			continue;
		if (!ofono_call_multiparty_get(itr))
			continue;
		if (ofono_call_state_get(itr) == OFONO_CALL_STATE_DISCONNECTED)
			continue;
		DBG("%p is another multiparty peer of %p", itr, c);
		return itr;
	}
	DBG("no other multiparty peers of %p", c);
	return NULL;
}

static void _call_auto_unplace(Callscreen *ctx, OFono_Call *c)
{
	Eina_Bool is_multiparty = ofono_call_multiparty_get(c);
	OFono_Call *replacement = NULL;

	DBG("ctx=%p, %p, %p, current=%p, call=%p, multiparty=%d",
		ctx, ctx->calls.first, ctx->calls.second, ctx->calls.current,
		c, is_multiparty);

	if (is_multiparty) {
		replacement = _multiparty_find_other(ctx, c);
		DBG("replacement=%p", replacement);
	}

	if (ctx->calls.first == c) {
		if (replacement) {
			_call_first_set(ctx, replacement);
			if (ctx->calls.current == c)
				_call_current_set(ctx, replacement);
			return;
		}

		if (!ctx->calls.second) {
			DBG("no calls left");
			_call_first_set(ctx, NULL);
			_call_current_set(ctx, NULL);
		} else {
			DBG("move second to first");
			_call_first_set(ctx, ctx->calls.second);
			_call_second_set(ctx, NULL);
			_call_current_set(ctx, ctx->calls.first);
		}
	} else if (ctx->calls.second == c) {
		if (replacement) {
			_call_second_set(ctx, replacement);
			if (ctx->calls.current == c)
				_call_current_set(ctx, replacement);
			return;
		}

		DBG("remove second");
		_call_second_set(ctx, NULL);
		_call_current_set(ctx, ctx->calls.first);
	}
}

static void _call_disconnected_done(Callscreen *ctx,
					const char *reason __UNUSED__)
{
	_multiparty_update(ctx);

	if (!ctx->calls.list)
		gui_call_exit();
	ctx->disconnected.call = NULL;
}

static void _popup_close(void *data, Evas_Object *o __UNUSED__,
				void *event __UNUSED__)
{
	Callscreen *ctx = data;

	evas_object_del(ctx->disconnected.popup);
	ctx->disconnected.popup = NULL;

	eina_stringshare_replace(&ctx->disconnected.number, NULL);

	_call_disconnected_done(ctx, "network");
}

static void _popup_redial(void *data, Evas_Object *o __UNUSED__,
				void *event __UNUSED__)
{
	Callscreen *ctx = data;

	gui_dial(ctx->disconnected.number);
	_popup_close(ctx, NULL, NULL);
}

static void _call_disconnected_show(Callscreen *ctx, OFono_Call *c,
					const char *reason)
{
	Evas_Object *p;
	const char *number, *title;
	char msg[1024];

	DBG("ctx=%p, %p, %p, current=%p, waiting=%p, previous=%s, "
		"disconnected=%p (%s)",
		ctx, ctx->calls.first, ctx->calls.second, ctx->calls.current,
		ctx->calls.waiting, ctx->disconnected.number, c, reason);

	if (ctx->calls.waiting == c) {
		_call_waiting_set(ctx, NULL);
		goto done; /* do not ask to redial to waiting number */
	}

	_call_auto_unplace(ctx, c);

	ctx->disconnected.call = c;

	elm_object_signal_emit(ctx->self, "active,disconnected", "call");

	if ((strcmp(reason, "local") == 0) || (strcmp(reason, "remote") == 0))
		goto done;

	number = ofono_call_line_id_get(c);
	if ((!number) || (number[0] == '\0'))
		goto done;

	if (ctx->disconnected.number)
		goto done;

	if (strcmp(reason, "network") == 0)
		title = "Network Disconnected!";
	else
		title = "Disconnected!";

	snprintf(msg, sizeof(msg), "Try to redial %s", number);

	eina_stringshare_replace(&ctx->disconnected.number, number);

	ctx->disconnected.popup = p = gui_simple_popup(title, msg);
	gui_simple_popup_buttons_set(p,
					"Dismiss",
					"dialer",
					_popup_close,
					"Redial",
					"dialer",
					_popup_redial,
					ctx);

	/* TODO: sound to notify user */

	return;

done:
	_call_disconnected_done(ctx, reason);
}

static void _tones_send_reply(void *data, OFono_Error err)
{
	Callscreen *ctx = data;

	if (err)
		ERR("Failed to send tones: %d", err);

	ctx->tones.pending = NULL;
	if (eina_strbuf_length_get(ctx->tones.todo) > 0) {
		const char *tones = eina_strbuf_string_get(ctx->tones.todo);

		DBG("Send pending tones: %s", tones);
		ctx->tones.pending = ofono_tones_send(
			tones, _tones_send_reply, ctx);
		eina_strbuf_reset(ctx->tones.todo);
	}
}

static void _on_pressed(void *data, Evas_Object *obj __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, %p, %p, current=%p, waiting=%p, signal: %s",
		ctx, ctx->calls.first, ctx->calls.second, ctx->calls.current,
		ctx->calls.waiting, emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "pressed,"));
	emission += strlen("pressed,");

}

static void _on_released(void *data, Evas_Object *obj __UNUSED__,
				const char *emission,
				const char *source __UNUSED__)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, %p, %p, current=%p, waiting=%p, signal: %s",
		ctx, ctx->calls.first, ctx->calls.second, ctx->calls.current,
		ctx->calls.waiting, emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "released,"));
	emission += strlen("released,");

}

static void _on_clicked(void *data, Evas_Object *obj __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	Callscreen *ctx = data;
	const char *dtmf = NULL;

	DBG("ctx=%p, %p, %p, current=%p, waiting=%p, signal: %s",
		ctx, ctx->calls.first, ctx->calls.second, ctx->calls.current,
		ctx->calls.waiting, emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "clicked,"));
	emission += strlen("clicked,");

	if ((emission[0] >= '0') && (emission[0] <= '9') && (emission[1] == 0))
		dtmf = emission;
	else if (strcmp(emission, "star") == 0)
		dtmf = "*";
	else if (strcmp(emission, "hash") == 0)
		dtmf = "#";

	if (dtmf) {
		if (!ctx->tones.pending)
			ctx->tones.pending = ofono_tones_send(
				dtmf, _tones_send_reply, ctx);
		else
			eina_strbuf_append_char(ctx->tones.todo, dtmf[0]);
		return;
	}

	if (strcmp(emission, "hangup") == 0) {
		OFono_Call *c = ctx->calls.current;
		if ((c) && (ofono_call_state_get(c) == OFONO_CALL_STATE_ACTIVE))
			ofono_release_and_swap(NULL, NULL);
		else if (c)
			ofono_call_hangup(c, NULL, NULL);
	} else if (strcmp(emission, "answer") == 0) {
		if (ctx->calls.current)
			ofono_call_answer(ctx->calls.current, NULL, NULL);
	} else if (strcmp(emission, "mute") == 0) {
		Eina_Bool val = !ofono_mute_get();
		ofono_mute_set(val, NULL, NULL);
	} else if (strcmp(emission, "speaker") == 0) {
		ERR("TODO - implement platform loudspeaker code");
	} else if (strcmp(emission, "contacts") == 0) {
		gui_contacts_show();
	} else if (strcmp(emission, "add-call") == 0) {
		gui_call_exit();
	} else if (strcmp(emission, "merge") == 0) {
		ofono_multiparty_create(NULL, NULL);
	} else if (strcmp(emission, "swap") == 0) {
		if (ctx->calls.current) {
			OFono_Call_State state;
			state = ofono_call_state_get(ctx->calls.current);
			if (state == OFONO_CALL_STATE_HELD ||
				state == OFONO_CALL_STATE_ACTIVE)
				ofono_swap_calls(NULL, NULL);
		}
	} else if (strcmp(emission, "waiting-hangup") == 0) {
		if (ctx->calls.waiting)
			ofono_call_hangup(ctx->calls.waiting, NULL, NULL);
	} else if (strcmp(emission, "hangup-answer") == 0) {
		if (ctx->calls.waiting)
			ofono_release_and_answer(NULL, NULL);
	} else if (strcmp(emission, "hold-answer") == 0) {
		if (ctx->calls.waiting)
			ofono_hold_and_answer(NULL, NULL);
	}
}

static void _on_key_down(void *data, Evas *e __UNUSED__,
				Evas_Object *o __UNUSED__,
				void *event_info)
{
	Callscreen *ctx = data;
	Evas_Event_Key_Down *ev = event_info;
	DBG("ctx=%p, key=%s (%s, %s)", ctx, ev->keyname, ev->key, ev->string);

	if ((strcmp(ev->key, "minus") == 0) ||
		(strcmp(ev->key, "KP_Subtract") == 0) ||
		(strcmp(ev->key, "XF86AudioLowerVolume") == 0)) {
		unsigned char last, cur;

		last = cur = ofono_volume_speaker_get();
		if (cur < 10)
			cur = 0;
		else
			cur -= 10;

		if (last != cur)
			ofono_volume_speaker_set(cur, NULL, NULL);
	} else if ((strcmp(ev->key, "plus") == 0) ||
			(strcmp(ev->key, "KP_Add") == 0) ||
			(strcmp(ev->key, "XF86AudioRaiseVolume") == 0)) {
		unsigned char last, cur;

		last = cur = ofono_volume_speaker_get();
		if (cur > 90)
			cur = 100;
		else
			cur += 10;

		if (last != cur)
			ofono_volume_speaker_set(cur, NULL, NULL);
	}
}

static void _ofono_changed(void *data)
{
	Callscreen *ctx = data;
	Edje_Message_Float msgf;
	Evas_Object *ed;
	const char *sig;

	sig = ofono_mute_get() ? "toggle,on,mute" : "toggle,off,mute";
	elm_object_signal_emit(ctx->self, sig, "call");

	ed = elm_layout_edje_get(ctx->self);

	msgf.val = (ofono_volume_speaker_get() / 100.0);
	edje_object_message_send(ed, EDJE_MESSAGE_FLOAT, 1, &msgf);

	msgf.val = (ofono_volume_microphone_get() / 100.0);
	edje_object_message_send(ed, EDJE_MESSAGE_FLOAT, 2, &msgf);
}

static void _call_added(void *data, OFono_Call *c)
{
	Callscreen *ctx = data;
	OFono_Call_State state = ofono_call_state_get(c);

	DBG("ctx=%p, %p, %p, current=%p, waiting=%p, added=%p(%d)",
		ctx, ctx->calls.first, ctx->calls.second, ctx->calls.current,
		ctx->calls.waiting, c, state);

	ctx->calls.list = eina_list_append(ctx->calls.list, c);

	if (state == OFONO_CALL_STATE_WAITING)
		_call_waiting_set(ctx, c);
	else {
		_call_auto_place(ctx, c);
		if (state == OFONO_CALL_STATE_INCOMING)
			_call_incoming_set(ctx, c);
	}

	gui_call_enter();
}

static void _call_removed(void *data, OFono_Call *c)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, %p, %p, current=%p, waiting=%p, removed=%p",
		ctx, ctx->calls.first, ctx->calls.second, ctx->calls.current,
		ctx->calls.waiting, c);

	ctx->calls.list = eina_list_remove(ctx->calls.list, c);
	_call_disconnected_show(ctx, c, "local");
}

static Eina_Bool _on_elapsed_updater(void *data)
{
	Callscreen *ctx = data;
	double next, now;

	if ((!ctx->calls.first) && (!ctx->calls.second)) {
		ctx->elapsed_updater = NULL;
		return EINA_FALSE;
	}

	if (ctx->calls.first)
		_call_elapsed_update(ctx, 1, ctx->calls.first);
	if (ctx->calls.second)
		_call_elapsed_update(ctx, 2, ctx->calls.second);

	if (ctx->gui_activecall)
		_activecall_elapsed_update(ctx);

	now = ecore_loop_time_get();
	next = 1.0 - (now - (int)now);
	ctx->elapsed_updater = ecore_timer_add(next, _on_elapsed_updater, ctx);
	return EINA_FALSE;
}

static Eina_Bool _call_multiparty_changed(const Callscreen *ctx,
						const OFono_Call *c)
{
	Eina_Bool current = ofono_call_multiparty_get(c);
	Eina_Bool previous = !!eina_list_data_find(ctx->multiparty.calls, c);
	return current ^ previous;
}

static inline int _call_priority_cmp(OFono_Call_State a, OFono_Call_State b)
{
	static const int state_priority[] = {
		[OFONO_CALL_STATE_DISCONNECTED] = 0,
		[OFONO_CALL_STATE_ACTIVE] = 6,
		[OFONO_CALL_STATE_HELD] = 3,
		[OFONO_CALL_STATE_DIALING] = 5,
		[OFONO_CALL_STATE_ALERTING] = 4,
		[OFONO_CALL_STATE_INCOMING] = 2,
		[OFONO_CALL_STATE_WAITING] = 1
	};
	return state_priority[a] - state_priority[b];
}

static Eina_Bool _call_priority_higher_than_current(const Callscreen *ctx,
							const OFono_Call *c)
{
	OFono_Call_State cur_state, new_state;

	new_state = ofono_call_state_get(c);
	if ((new_state == OFONO_CALL_STATE_DISCONNECTED) ||
		(new_state == OFONO_CALL_STATE_WAITING))
		return EINA_FALSE;

	if (!ctx->calls.current)
		return EINA_TRUE;

	cur_state = ofono_call_state_get(ctx->calls.current);
	return _call_priority_cmp(new_state, cur_state) > 0;
}

static OFono_Call *_call_priority_find_higher(const Callscreen *ctx,
						OFono_Call *c)
{
	OFono_Call_State first, second, query, higher_state;
	OFono_Call *higher = NULL;

	if (!ctx->calls.first)
		return NULL;

	query = ofono_call_state_get(c);
	first = ofono_call_state_get(ctx->calls.first);
	if (ctx->calls.second)
		second = ofono_call_state_get(ctx->calls.second);
	else
		second = OFONO_CALL_STATE_DISCONNECTED;

	if (_call_priority_cmp(first, query) > 0) {
		higher = ctx->calls.first;
		higher_state = first;
	} else {
		higher = c;
		higher_state = query;
	}

	if (_call_priority_cmp(second, higher_state) > 0) {
		higher = ctx->calls.second;
		higher_state = second;
	}

	if (higher != c)
		return higher;

	return NULL;
}

static void _call_changed_waiting_update(Callscreen *ctx, OFono_Call *c)
{
	OFono_Call_State state = ofono_call_state_get(c);

	if (state == OFONO_CALL_STATE_DISCONNECTED) {
		DBG("waiting was disconnected");
		_call_waiting_set(ctx, NULL);
	} else if (state != OFONO_CALL_STATE_WAITING) {
		_call_waiting_set(ctx, NULL);
		DBG("waiting was answered");
		_call_auto_place(ctx, c);
	}
}

static void _call_changed_current_update(Callscreen *ctx, OFono_Call *c)
{
	if (ctx->calls.current != c) {
		if (_call_priority_higher_than_current(ctx, c)) {
			DBG("changed call %p is higher than previous %p",
				c, ctx->calls.current);
			_call_current_set(ctx, c);
		} else {
			DBG("changed call %p is lower than previous %p",
				c, ctx->calls.current);
		}
	} else if (ctx->calls.current == c) {
		OFono_Call *other = _call_priority_find_higher(ctx, c);
		if (other) {
			DBG("other call %p is higher than changed %p",
				other, c);
			_call_current_set(ctx, other);
		} else {
			DBG("changed call %p is still the current", c);
			_call_current_actions_update(ctx);
		}
	}
}

static void _call_changed_multiparty_update(Callscreen *ctx, OFono_Call *c)
{
	if (!_call_multiparty_changed(ctx, c)) {
		DBG("multiparty unchanged");
		return;
	}

	_multiparty_update(ctx);

	_call_show(ctx, 1, ctx->calls.first);
	_call_update(ctx, 1, ctx->calls.first);
	_activecall_update(ctx);

	if (ofono_call_state_get(c) == OFONO_CALL_STATE_DISCONNECTED) {
		DBG("multiparty peer was disconnected");
		return;
	}

	if (ofono_call_multiparty_get(c)) {
		if (ctx->calls.first &&
			(!ofono_call_multiparty_get(ctx->calls.first)))
			DBG("multiparty added to second display");
		else {
			DBG("multiparty merged to first display, "
				"remove second display");
			_call_second_set(ctx, NULL);
		}
	} else if (ctx->calls.first != c) {
		DBG("multiparty split, add peer as second display");
		_call_second_set(ctx, c);
	} else {
		OFono_Call *other = _multiparty_find_other(ctx, c);
		DBG("multiparty split, add another %p peer as second display",
			other);
		_call_second_set(ctx, other);
	}
}

static void _call_changed_swap_merge_update(Callscreen *ctx)
{
	Evas_Object *o = ctx->self;

	if (!ctx->calls.first) {
		DBG("no calls, disable swap, pause and merge");
		elm_object_signal_emit(o, "disable,swap", "call");
		elm_object_signal_emit(o, "disable,pause", "call");
		elm_object_signal_emit(o, "disable,merge", "call");
		return;
	}

	if (ctx->calls.second) {
		DBG("two calls, enable swap and merge, disable pause");
		elm_object_signal_emit(o, "enable,swap", "call");
		elm_object_signal_emit(o, "disable,pause", "call");
		elm_object_signal_emit(o, "enable,merge", "call");
	} else {
		const char *sig_swap, *sig_pause;
		if (ofono_call_state_get(ctx->calls.first) ==
			OFONO_CALL_STATE_HELD) {
			sig_swap = "enable,swap";
			sig_pause = "disable,pause";
		} else {
			sig_swap = "disable,swap";
			sig_pause = "enable,pause";
		}
		DBG("one call, disable merge and %s %s", sig_swap, sig_pause);
		elm_object_signal_emit(o, sig_swap, "call");
		elm_object_signal_emit(o, sig_pause, "call");
		elm_object_signal_emit(o, "disable,merge", "call");
	}
}

static void _call_changed(void *data, OFono_Call *c)
{
	Callscreen *ctx = data;
	OFono_Call_State state = ofono_call_state_get(c);

	DBG("BEGIN: ctx=%p, %p, %p, current=%p, waiting=%p, changed=%p (%d)",
		ctx, ctx->calls.first, ctx->calls.second, ctx->calls.current,
		ctx->calls.waiting, c, state);

	if (ctx->calls.waiting == c)
		_call_changed_waiting_update(ctx, c);
	else if (ctx->calls.incoming == c) {
		if (state != OFONO_CALL_STATE_INCOMING)
			_call_incoming_set(ctx, NULL);
	}

	_call_changed_current_update(ctx, c);
	_call_changed_multiparty_update(ctx, c);

	if (ctx->calls.first == c)
		_call_update(ctx, 1, c);
	else if (ctx->calls.second == c)
		_call_update(ctx, 2, c);

	if (ctx->multiparty.calls)
		_multiparty_private_available_update(ctx);

	if (ctx->calls.current) {
		if (!ctx->elapsed_updater)
			_on_elapsed_updater(ctx);
	} else if (ctx->elapsed_updater) {
		ecore_timer_del(ctx->elapsed_updater);
		ctx->elapsed_updater = NULL;
	}

	_call_changed_swap_merge_update(ctx);

	DBG("END: ctx=%p, %p, %p, current=%p, waiting=%p, changed=%p (%d)",
		ctx, ctx->calls.first, ctx->calls.second, ctx->calls.current,
		ctx->calls.waiting, c, state);
}

static void _call_disconnected(void *data, OFono_Call *c, const char *reason)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, %p, %p, current=%p, waiting=%p, disconnected=%p (%s)",
		ctx, ctx->calls.first, ctx->calls.second, ctx->calls.current,
		ctx->calls.waiting, c, reason);

	EINA_SAFETY_ON_NULL_RETURN(reason);
	_call_disconnected_show(ctx, c, reason);
}

static void _on_del(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Callscreen *ctx = data;

	ofono_call_added_cb_del(callback_node_call_added);
	ofono_call_removed_cb_del(callback_node_call_removed);
	ofono_call_changed_cb_del(callback_node_call_changed);
	ofono_call_disconnected_cb_del(callback_node_call_disconnected);
	ofono_modem_changed_cb_del(callback_node_modem_changed);

	eina_strbuf_free(ctx->tones.todo);
	if (ctx->tones.pending)
		ofono_pending_cancel(ctx->tones.pending);

	if (ctx->elapsed_updater)
		ecore_timer_del(ctx->elapsed_updater);

	eina_stringshare_del(ctx->disconnected.number);

	eina_list_free(ctx->multiparty.calls);

	eina_list_free(ctx->calls.list);
	free(ctx);
}

Evas_Object *callscreen_add(Evas_Object *parent)
{
	Callscreen *ctx;
	Evas_Object *obj = gui_layout_add(parent, "call");
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);

	ctx = calloc(1, sizeof(Callscreen));
	ctx->self = obj;
	ctx->tones.todo = eina_strbuf_new();

	evas_object_data_set(obj, "callscreen.ctx", ctx);

	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL,
					_on_del, ctx);
	elm_object_signal_callback_add(obj, "pressed,*", "call",
					_on_pressed, ctx);
	elm_object_signal_callback_add(obj, "released,*", "call",
					_on_released, ctx);
	elm_object_signal_callback_add(obj, "clicked,*", "call",
					_on_clicked, ctx);

	elm_object_focus_allow_set(obj, EINA_TRUE);
	evas_object_event_callback_add(obj, EVAS_CALLBACK_KEY_DOWN,
					_on_key_down, ctx);

	_call_clear(ctx, 1);
	_call_clear(ctx, 2);
	elm_object_signal_emit(obj, "calls,1", "call");

	ctx->multiparty.sc = elm_scroller_add(obj);
	elm_scroller_policy_set(ctx->multiparty.sc, ELM_SCROLLER_POLICY_AUTO,
				ELM_SCROLLER_POLICY_OFF);
	elm_scroller_bounce_set(ctx->multiparty.sc, EINA_FALSE, EINA_TRUE);
	elm_object_style_set(ctx->multiparty.sc, "multiparty-details");

	ctx->multiparty.bx = elm_box_add(obj);
	evas_object_size_hint_weight_set(ctx->multiparty.bx,
						EVAS_HINT_EXPAND, 0.0);
	evas_object_size_hint_align_set(ctx->multiparty.bx,
					EVAS_HINT_FILL,	0.0);
	evas_object_show(ctx->multiparty.bx);
	elm_object_content_set(ctx->multiparty.sc, ctx->multiparty.bx);

	elm_object_part_content_set(obj, "elm.swallow.multiparty-details",
					ctx->multiparty.sc);

	callback_node_call_added = ofono_call_added_cb_add(_call_added, ctx);

	callback_node_call_removed =
		ofono_call_removed_cb_add(_call_removed, ctx);

	callback_node_call_changed =
		ofono_call_changed_cb_add(_call_changed, ctx);

	callback_node_call_disconnected =
		ofono_call_disconnected_cb_add(_call_disconnected, ctx);

	callback_node_modem_changed =
		ofono_modem_changed_cb_add(_ofono_changed, ctx);

	return obj;
}
