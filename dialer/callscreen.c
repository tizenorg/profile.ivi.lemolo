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
	} multiparty;
	struct {
		OFono_Call *active;
		OFono_Call *waiting;
		OFono_Call *held;
		Eina_List *list;
	} calls;
	OFono_Call_State last_state;
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
	eina_list_free(ctx->multiparty.calls);
	ctx->multiparty.calls = new;

	elm_box_clear(ctx->multiparty.bx);

	if (!new) {
		elm_object_signal_emit(ctx->self, "hide,multiparty-details",
					"call");
		return;
	}

	EINA_LIST_FOREACH(new, n1, c) {
		const char *name;
		char *number;

		name = ofono_call_name_get(c);
		number = phone_format(ofono_call_line_id_get(c));

		it = gui_layout_add(ctx->multiparty.bx, "multiparty-details");
		evas_object_size_hint_align_set(it,
						EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(it);

		elm_object_part_text_set(it, "elm.text.name", name);
		elm_object_part_text_set(it, "elm.text.number", number);

		if ((!name) || (*name == '\0'))
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

		free(number);
	}
	elm_object_signal_emit(ctx->self, "show,multiparty-details", "call");
}

static void _calls_update(Callscreen *ctx)
{
	const Eina_List *n;
	OFono_Call *c, *found = NULL, *waiting = NULL, *held = NULL;
	OFono_Call_State found_state = OFONO_CALL_STATE_DISCONNECTED;
	static const int state_priority[] = {
		[OFONO_CALL_STATE_DISCONNECTED] = 0,
		[OFONO_CALL_STATE_ACTIVE] = 6,
		[OFONO_CALL_STATE_HELD] = 3,
		[OFONO_CALL_STATE_DIALING] = 5,
		[OFONO_CALL_STATE_ALERTING] = 4,
		[OFONO_CALL_STATE_INCOMING] = 2,
		[OFONO_CALL_STATE_WAITING] = 1
	};

	if (ctx->calls.active) {
		OFono_Call_State s = ofono_call_state_get(ctx->calls.active);
		if (s != OFONO_CALL_STATE_ACTIVE) {
			ctx->calls.active = NULL;
			ctx->last_state = 0;
		}
	}
	if (ctx->calls.waiting) {
		OFono_Call_State s = ofono_call_state_get(ctx->calls.waiting);
		if (s != OFONO_CALL_STATE_WAITING)
			ctx->calls.waiting = NULL;
	}
	if (ctx->calls.held) {
		OFono_Call_State s = ofono_call_state_get(ctx->calls.held);
		if (s != OFONO_CALL_STATE_HELD)
			ctx->calls.held = NULL;
	}

	_multiparty_update(ctx);

	if (ctx->calls.active && ctx->calls.waiting && ctx->calls.held)
		return;

	EINA_LIST_FOREACH(ctx->calls.list, n, c) {
		OFono_Call_State state = ofono_call_state_get(c);

		DBG("compare %p (%d) to %p (%d)", found, found_state,
			c, state);
		if (state_priority[state] > state_priority[found_state]) {
			found_state = state;
			found = c;
		}
		if ((!waiting) && (state == OFONO_CALL_STATE_WAITING))
			waiting = c;
		if ((!held) && (state == OFONO_CALL_STATE_HELD))
			held = c;
	}

	DBG("found=%p, state=%d, waiting=%p, held=%p",
		found, found_state, waiting, held);
	if (!found)
		return;

	if (!ctx->calls.active) {
		ctx->calls.active = found;
		ctx->last_state = 0;
	}
	if (!ctx->calls.waiting)
		ctx->calls.waiting = waiting;
	if (!ctx->calls.held)
		ctx->calls.held = held;

	gui_call_enter();
}

static void _call_disconnected_done(Callscreen *ctx, const char *reason)
{
	_calls_update(ctx);

	if (!ctx->calls.list) {
		if (ctx->gui_activecall) {
			gui_activecall_set(NULL);
			evas_object_del(ctx->gui_activecall);
			ctx->gui_activecall = NULL;
		}
		gui_call_exit();
	} else {
		if (strcmp(reason, "local") == 0) {
			/* If there is a held call and active is
			 * hangup we're left with held but no active,
			 * which is strange.
			 *
			 * Just make the held active by calling
			 * SwapCalls.
			 */
			if (ctx->calls.active == ctx->disconnected.call &&
				ctx->calls.held != ctx->disconnected.call) {
				INF("User disconnected and left held call. "
					"Automatically activate it.");

				/* TODO: sound to notify user */
				ofono_swap_calls(NULL, NULL);
			}
		}
	}
	ctx->disconnected.call = NULL;
}

static void _popup_close(void *data, Evas_Object *o __UNUSED__, void *event __UNUSED__)
{
	Callscreen *ctx = data;

	evas_object_del(ctx->disconnected.popup);
	ctx->disconnected.popup = NULL;

	eina_stringshare_replace(&ctx->disconnected.number, NULL);

	_call_disconnected_done(ctx, "network");
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

	DBG("ctx=%p, active=%p, held=%p, waiting=%p, previous=%s, "
		"disconnected=%p (%s)",
		ctx, ctx->calls.active, ctx->calls.held, ctx->calls.waiting,
		ctx->disconnected.number, c, reason);

	if (ctx->calls.waiting == c) {
		ctx->calls.waiting = NULL;
		elm_object_part_text_set(ctx->self, "elm.text.waiting", "");
		elm_object_signal_emit(ctx->self, "hide,waiting", "call");
		goto done;
	}
	if (ctx->calls.held == c) {
		ctx->calls.held = NULL;
		elm_object_part_text_set(ctx->self, "elm.text.held", "");
		elm_object_signal_emit(ctx->self, "hide,held", "call");
		goto done;
	}

	if ((ctx->calls.active) && (ctx->calls.active != c))
		goto done;
	ctx->calls.active = NULL;
	ctx->last_state = 0;
	ctx->disconnected.call = c;

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

	/* TODO: sound to notify user */

	evas_object_show(p);

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
	DBG("ctx=%p, active=%p, held=%p, waiting=%p, signal: %s",
		ctx, ctx->calls.active, ctx->calls.held, ctx->calls.waiting,
		emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "pressed,"));
	emission += strlen("pressed,");

}

static void _on_released(void *data, Evas_Object *obj __UNUSED__,
				const char *emission,
				const char *source __UNUSED__)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, active=%p, held=%p, waiting=%p, signal: %s",
		ctx, ctx->calls.active, ctx->calls.held, ctx->calls.waiting,
		emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "released,"));
	emission += strlen("released,");

}

static void _on_clicked(void *data, Evas_Object *obj __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	Callscreen *ctx = data;
	const char *dtmf = NULL;
	DBG("ctx=%p, active=%p, held=%p, waiting=%p, signal: %s",
		ctx, ctx->calls.active, ctx->calls.held, ctx->calls.waiting,
		emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "clicked,"));
	emission += strlen("clicked,");

	if ((emission[0] >= '0') && (emission[0] <= '9'))
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
		if (ctx->calls.active) {
			OFono_Call *c = ctx->calls.active;
			if (ofono_call_multiparty_get(c))
				ofono_multiparty_hangup(NULL, NULL);
			else
				ofono_call_hangup(c, NULL, NULL);
		}
	} else if (strcmp(emission, "answer") == 0) {
		if (ctx->calls.active)
			ofono_call_answer(ctx->calls.active, NULL, NULL);
	} else if (strcmp(emission, "mute") == 0) {
		Eina_Bool val = !ofono_mute_get();
		ofono_mute_set(val, NULL, NULL);
	} else if (strcmp(emission, "speaker") == 0) {
		ERR("TODO - implement platform loudspeaker code");
	} else if (strcmp(emission, "contacts") == 0) {
		ERR("TODO - implement access to contacts");
	} else if (strcmp(emission, "add-call") == 0) {
		gui_call_exit();
	} else if (strcmp(emission, "merge") == 0) {
		if (ctx->calls.held)
			ofono_multiparty_create(NULL, NULL);
	} else if (strcmp(emission, "swap") == 0) {
		if (ctx->calls.held)
			ofono_swap_calls(NULL, NULL);
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

	if (!ctx->calls.list)
		return;

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
	DBG("ctx=%p, active=%p, held=%p, waiting=%p, added=%p",
		ctx, ctx->calls.active, ctx->calls.held, ctx->calls.waiting, c);

	ctx->calls.list = eina_list_append(ctx->calls.list, c);
	if (!ctx->calls.active) {
		_calls_update(ctx);
		gui_call_enter();
	}
}

static void _call_removed(void *data, OFono_Call *c)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, active=%p, held=%p, waiting=%p, removed=%p",
		ctx, ctx->calls.active, ctx->calls.held, ctx->calls.waiting, c);
	ctx->calls.list = eina_list_remove(ctx->calls.list, c);
	_call_disconnected_show(ctx, c, "local");
}

static Eina_Bool _on_elapsed_updater(void *data)
{
	Callscreen *ctx = data;
	double next, elapsed, start;
	Edje_Message_Float msgf;
	Evas_Object *ed, *activecall = ctx->gui_activecall;
	char buf[128];

	if (!ctx->calls.active)
		goto stop;

	start = ofono_call_start_time_get(ctx->calls.active);
	if (start < 0) {
		ERR("Unknown start time for call");
		goto stop;
	}

	elapsed = ecore_loop_time_get() - start;
	if (elapsed < 0) {
		ERR("Time rewinded? %f - %f = %f", ecore_loop_time_get(), start,
			elapsed);
		goto stop;
	}

	ed = elm_layout_edje_get(ctx->self);
	msgf.val = elapsed;
	edje_object_message_send(ed, EDJE_MESSAGE_FLOAT, 3, &msgf);

	if (elapsed > 3600)
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
				(int)elapsed / 3600,
				(int)elapsed % 3600 / 60,
				(int)elapsed % 60);
	else
		snprintf(buf, sizeof(buf), "%02d:%02d",
				(int)elapsed / 60,
				(int)elapsed % 60);
	elm_object_part_text_set(ctx->self, "elm.text.elapsed", buf);
	if (activecall)
		elm_object_part_text_set(activecall, "elm.text.elapsed", buf);

	next = 1.0 - (elapsed - (int)elapsed);
	ctx->elapsed_updater = ecore_timer_add(next, _on_elapsed_updater, ctx);
	return EINA_FALSE;

stop:
	if (activecall) {
		elm_object_part_text_set(activecall, "elm.text.elapsed", "");
		elm_object_signal_emit(activecall, "hide,elapsed", "call");
	}

	elm_object_part_text_set(ctx->self, "elm.text.elapsed", "");
	elm_object_signal_emit(ctx->self, "hide,elapsed", "call");
	ctx->elapsed_updater = NULL;
	return EINA_FALSE;
}

static char *_call_name_or_id(const OFono_Call *call)
{
	const char *s = ofono_call_name_get(call);
	if ((s) && (s[0] != '\0'))
		return strdup(s);
	return phone_format(ofono_call_line_id_get(call));
}

static char *_call_name_get(const Callscreen *ctx, const OFono_Call *call)
{
	Eina_Strbuf *buf;
	const Eina_List *n;
	Eina_Bool first = EINA_TRUE;
	char *s;

	if (!ofono_call_multiparty_get(call))
		return _call_name_or_id(call);

	buf = eina_strbuf_new();
	eina_strbuf_append(buf, "Conf: ");
	EINA_LIST_FOREACH(ctx->calls.list, n, call) {
		char *name;

		if (!ofono_call_multiparty_get(call))
			continue;

		name = _call_name_or_id(call);
		if (!first)
			eina_strbuf_append_printf(buf, ", %s", name);
		else {
			eina_strbuf_append(buf, name);
			first = EINA_FALSE;
		}
		free(name);
	}

	s = eina_strbuf_string_steal(buf);
	eina_strbuf_free(buf);
	return s;
}

static void _call_changed(void *data, OFono_Call *c)
{
	Callscreen *ctx = data;
	OFono_Call_State state;
	Eina_Bool was_waiting, was_held, is_held;
	const char *status, *sig = "hide,answer";
	char *contact;

	DBG("ctx=%p, active=%p, held=%p, waiting=%p, changed=%p",
		ctx, ctx->calls.active, ctx->calls.held, ctx->calls.waiting, c);

	was_waiting = !!ctx->calls.waiting;

	was_held = (ctx->calls.held) && (ctx->calls.held != ctx->calls.active);

	_calls_update(ctx);

	if ((ctx->calls.waiting) && (!was_waiting)) {
		char buf[256];
		contact = _call_name_get(ctx, ctx->calls.waiting);
		snprintf(buf, sizeof(buf), "%s is waiting...", contact);
		elm_object_part_text_set(ctx->self, "elm.text.waiting", buf);
		elm_object_signal_emit(ctx->self, "show,waiting", "call");
		free(contact);
	} else if ((!ctx->calls.waiting) && (was_waiting)) {
		elm_object_part_text_set(ctx->self, "elm.text.waiting", "");
		elm_object_signal_emit(ctx->self, "hide,waiting", "call");
	}

	is_held = (ctx->calls.held) && (ctx->calls.held != ctx->calls.active);

	if ((is_held) && (!was_held))
		elm_object_signal_emit(ctx->self, "show,held", "call");
	else if ((!is_held) && (was_held)) {
		elm_object_part_text_set(ctx->self, "elm.text.held", "");
		elm_object_signal_emit(ctx->self, "hide,held", "call");
	}

	c = ctx->calls.active;
	if (!c)
		return;

	contact = _call_name_get(ctx, c);

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
		sig = "show,answer";
		break;
	case OFONO_CALL_STATE_WAITING:
		status = "Waiting...";
		break;
	default:
		status = "?";
	}

	if (!ctx->gui_activecall) {
		ctx->gui_activecall = gui_layout_add(ctx->self, "activecall");
		elm_object_signal_callback_add(ctx->gui_activecall,
						"clicked", "call",
						_on_active_call_clicked, ctx);
		gui_activecall_set(ctx->gui_activecall);
	}
	elm_object_part_text_set(ctx->gui_activecall, "elm.text.name", contact);
	elm_object_part_text_set(ctx->gui_activecall, "elm.text.status",
					status);


	elm_object_part_text_set(ctx->self, "elm.text.name", contact);
	free(contact);

	elm_object_part_text_set(ctx->self, "elm.text.status", status);
	elm_object_signal_emit(ctx->self, sig, "call");

	sig = ofono_call_multiparty_get(c) ? "show,multiparty" :
		"hide,multiparty";
	elm_object_signal_emit(ctx->self, sig, "call");
	elm_object_signal_emit(ctx->gui_activecall, sig, "call");

	if (ctx->last_state != state) {
		Eina_Bool have_updater = !!ctx->elapsed_updater;
		Eina_Bool want_updater = EINA_FALSE;

		switch (state) {
		case OFONO_CALL_STATE_DISCONNECTED:
			sig = "state,disconnected";
			break;
		case OFONO_CALL_STATE_ACTIVE:
			sig = "state,active";
			want_updater = EINA_TRUE;
			break;
		case OFONO_CALL_STATE_HELD:
			sig = "state,held";
			break;
		case OFONO_CALL_STATE_DIALING:
			sig = "state,dialing";
			break;
		case OFONO_CALL_STATE_ALERTING:
			sig = "state,alerting";
			break;
		case OFONO_CALL_STATE_INCOMING:
			sig = "state,incoming";
			break;
		case OFONO_CALL_STATE_WAITING:
			sig = "state,waiting";
			break;
		default:
			sig = NULL;
		}
		if (sig) {
			elm_object_signal_emit(ctx->self, sig, "call");
			elm_object_signal_emit(ctx->gui_activecall,
						sig, "call");
		}
		ctx->last_state = state;

		sig = NULL;
		if (have_updater && !want_updater) {
			ecore_timer_del(ctx->elapsed_updater);
			ctx->elapsed_updater = NULL;
			sig = "hide,elapsed";
			elm_object_part_text_set(ctx->self, "elm.text.elapsed",
							"");
		} else if (want_updater) {
			if (!have_updater)
				sig = "show,elapsed";
			else {
				ecore_timer_del(ctx->elapsed_updater);
				ctx->elapsed_updater = NULL;
			}
			_on_elapsed_updater(ctx);
		}
		if (sig) {
			elm_object_signal_emit(ctx->self, sig, "call");
			elm_object_signal_emit(ctx->gui_activecall,
						sig, "call");
		}
	}

	if (is_held) {
		contact = _call_name_get(ctx, ctx->calls.held);
		elm_object_part_text_set(ctx->self, "elm.text.held", contact);
		free(contact);

		if (ofono_call_multiparty_get(ctx->calls.held))
			sig = "show,held,multiparty";
		else
			sig = "hide,held,multiparty";
		elm_object_signal_emit(ctx->self, sig, "call");
	}

	sig = is_held ? "enable,merge" : "disable,merge";
	elm_object_signal_emit(ctx->self, sig, "call");

	sig = ctx->calls.held ? "enable,swap" : "disable,swap";
	elm_object_signal_emit(ctx->self, sig, "call");

	if (state == OFONO_CALL_STATE_DISCONNECTED)
		_call_disconnected_show(ctx, c, "local");

	_ofono_changed(ctx);
}

static void _call_disconnected(void *data, OFono_Call *c, const char *reason)
{
	Callscreen *ctx = data;
	DBG("ctx=%p, active=%p, held=%p, waiting=%p, disconnected=%p (%s)",
		ctx, ctx->calls.active, ctx->calls.held, ctx->calls.waiting,
		c, reason);

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
	ofono_changed_cb_set(NULL, NULL);

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

Evas_Object *callscreen_add(Evas_Object *parent) {
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

	elm_object_part_text_set(obj, "elm.text.name", "");
	elm_object_part_text_set(obj, "elm.text.status", "");
	elm_object_part_text_set(obj, "elm.text.elapsed", "");

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

	ofono_call_added_cb_set(_call_added, ctx);
	ofono_call_removed_cb_set(_call_removed, ctx);
	ofono_call_changed_cb_set(_call_changed, ctx);
	ofono_call_disconnected_cb_set(_call_disconnected, ctx);
	ofono_changed_cb_set(_ofono_changed, ctx);

	return obj;
}
