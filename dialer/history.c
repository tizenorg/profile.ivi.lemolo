#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <Eet.h>
#include <Eina.h>
#include <time.h>
#include <limits.h>
#include <string.h>

#include "ofono.h"
#include "log.h"
#include "util.h"
#include "gui.h"

#ifndef EET_COMPRESSION_DEFAULT
#define EET_COMPRESSION_DEFAULT 1
#endif

#define HISTORY_ENTRY "history"

typedef struct _Call_Info_List {
	Eina_List *list;
	Eina_Bool dirty;
} Call_Info_List;

typedef struct _History {
	char *path, *bkp;
	Eet_Data_Descriptor *edd;
	Eet_Data_Descriptor *edd_list;
	Call_Info_List *calls;
	Elm_Genlist_Item_Class *itc;
	Evas_Object *self;
	Evas_Object *clear_popup;
	Evas_Object *genlist_all, *genlist_missed;
	Ecore_Poller *updater;
	double last_update;
} History;

typedef struct _Call_Info {
	long long start_time;
	long long end_time;
	long long creation_time; /* not in edd */
	const char *line_id;
	const char *name;
	Eina_Bool completed;
	Eina_Bool incoming;
	const OFono_Call *call; /* not in edd */
	History *history;
	Elm_Object_Item *it_all; /*not in edd */
	Elm_Object_Item *it_missed; /*not in edd */
} Call_Info;

static OFono_Callback_List_Call_Node *callback_node_call_removed = NULL;
static OFono_Callback_List_Call_Node *callback_node_call_changed = NULL;

static Eina_Bool _history_time_updater(void *data)
{
	History *ctx = data;
	Elm_Object_Item *it;
	double now = ecore_loop_time_get();
	const double interval_threshold = 30.0;
	const long long update_threshold = time(NULL) - WEEK - DAY;

	/*
	 * NOTE ABOUT CONSTANTS:
	 *
	 * - interval_threshold: to avoid updating too often (window
	 *   lost and gained focus, object hidden or shown), we limit
	 *   updates to this minimum interval. The poller should run
	 *   every 60 seconds.
	 *
	 * - update_threshold: since we format strings over a week as
	 *   fixed string (often day-month-year, not relative to
	 *   today), we can stop flagging list items as updated. We
	 *   give it a day of slack so we can be sure to update every
	 *   item (for held and conferences, you may have items that
	 *   are close in time but slightly out of order as items are
	 *   prepended as the calls are removed from ofono, then
	 *   history is not strictly in 'time' order). We must
	 *   stop iterating after update_threshold so users that never
	 *   deleted history and have thousand items will not
	 *   uselessly update all the thousand items.
	 */

	if (!ctx->calls->list) {
		ctx->updater = NULL;
		return EINA_FALSE;
	}

	if (now - ctx->last_update < interval_threshold)
		return EINA_TRUE;
	ctx->last_update = now;

	it = elm_genlist_first_item_get(ctx->genlist_all);
	for (; it != NULL; it = elm_genlist_item_next_get(it)) {
		const Call_Info *call_info = elm_object_item_data_get(it);
		long long t = call_info->end_time;
		if (EINA_UNLIKELY(t == 0)) {
			t = call_info->start_time;
			if (EINA_UNLIKELY(t == 0))
				t = call_info->creation_time;
		}
		if (EINA_UNLIKELY(t < update_threshold))
			break;
		elm_genlist_item_update(it);
	}

	it = elm_genlist_first_item_get(ctx->genlist_missed);
	for (; it != NULL; it = elm_genlist_item_next_get(it)) {
		const Call_Info *call_info = elm_object_item_data_get(it);
		long long t = call_info->end_time;
		if (EINA_UNLIKELY(t == 0)) {
			t = call_info->start_time;
			if (EINA_UNLIKELY(t == 0))
				t = call_info->creation_time;
		}
		if (EINA_UNLIKELY(t < update_threshold))
			break;
		elm_genlist_item_update(it);
	}

	return EINA_TRUE;
}

static void _history_time_updater_stop(History *history)
{
	Evas *e = evas_object_evas_get(history->self);
	Eina_Bool win_focused = evas_focus_state_get(e);
	Eina_Bool obj_visible = evas_object_visible_get(history->self);

	DBG("poller %p, win_focused=%hhu, obj_visible=%hhu",
		history->updater, win_focused, obj_visible);
	if (!history->updater)
		return;
	if (win_focused && obj_visible)
		return;

	DBG("delete poller %p", history->updater);
	ecore_poller_del(history->updater);
	history->updater = NULL;
}

static void _history_time_updater_start(History *history)
{
	Evas *e = evas_object_evas_get(history->self);
	Eina_Bool win_focused = evas_focus_state_get(e);
	Eina_Bool obj_visible = evas_object_visible_get(history->self);

	DBG("poller %p, win_focused=%hhu, obj_visible=%hhu",
		history->updater, win_focused, obj_visible);
	if (history->updater)
		return;
	if (!history->calls->list)
		return;
	if ((!win_focused) || (!obj_visible))
		return;

	DBG("start poller");
	/* ECORE_POLLER_CORE is 1/8th of second. */
	history->updater = ecore_poller_add(ECORE_POLLER_CORE, 8 * 60,
						_history_time_updater,
						history);
	_history_time_updater(history);
}

static Call_Info *_history_call_info_search(const History *history,
						const OFono_Call *call)
{
	Call_Info *call_info;
	Eina_List *l;
	long long t = ofono_call_full_start_time_get(call);
	const char *line_id = ofono_call_line_id_get(call); /* stringshare */

	EINA_LIST_FOREACH(history->calls->list, l, call_info) {
		if (call_info->call == call)
			return call_info;
		else if (!call_info->call) {
			if ((t > 0) && (call_info->start_time == t) &&
				(line_id == call_info->line_id)) {
				DBG("associated existing log %p %s (%lld) with "
					"call %p %s (%lld)",
					call_info,
					call_info->line_id,
					call_info->start_time,
					call, line_id, t);
				call_info->call = call;
				return call_info;
			}
		}
	}

	return NULL;
}

static Eina_Bool _history_call_info_update(Call_Info *call_info)
{
	OFono_Call_State state;

	EINA_SAFETY_ON_NULL_RETURN_VAL(call_info->call, EINA_FALSE);
	state = ofono_call_state_get(call_info->call);

	if (state == OFONO_CALL_STATE_INCOMING ||
		state == OFONO_CALL_STATE_WAITING) {
		if (!call_info->incoming) {
			call_info->incoming = EINA_TRUE;
			return EINA_TRUE;
		}
	} else if (state == OFONO_CALL_STATE_DIALING ||
			state == OFONO_CALL_STATE_ALERTING) {
		if (!call_info->incoming) {
			call_info->incoming = EINA_FALSE;
			return EINA_TRUE;
		}
	} else if (state == OFONO_CALL_STATE_ACTIVE ||
			state == OFONO_CALL_STATE_HELD) {
		if (!call_info->completed) {
			call_info->start_time = ofono_call_full_start_time_get
				(call_info->call);
			if (call_info->start_time == 0)
				call_info->start_time = call_info->creation_time;

			call_info->completed = EINA_TRUE;
			return EINA_TRUE;
		}
	}

	return EINA_FALSE;
}

static void _dial_reply(void *data, OFono_Error err,
			OFono_Call *call __UNUSED__)
{
	const char *number = data;

	if (err != OFONO_ERROR_NONE) {
		char buf[1024];
		snprintf(buf, sizeof(buf), "Could not call: %s", number);
		gui_simple_popup("Error", buf);
	}
}

static void _on_item_clicked(void *data, Evas_Object *obj __UNUSED__,
				void *event_info)
{
	Elm_Object_Item *it = event_info;
	const char *number = data;

	INF("call %s", number);
	ofono_dial(number, NULL, _dial_reply, number);
	elm_genlist_item_selected_set(it, EINA_FALSE);
}

static void _history_call_changed(void *data, OFono_Call *call)
{
	History *history = data;
	const char *line_id = ofono_call_line_id_get(call);
	Call_Info *call_info;
	OFono_Call_State state = ofono_call_state_get(call);

	call_info = _history_call_info_search(history, call);
	DBG("call=%p, id=%s, state=%d, completed=%d, incoming=%d, info=%p",
		call, line_id, state,
		call_info ? call_info->completed : EINA_FALSE,
		call_info ? call_info->incoming : EINA_FALSE,
		call_info);

	if (call_info)
		goto end;

	call_info = calloc(1, sizeof(Call_Info));
	EINA_SAFETY_ON_NULL_RETURN(call_info);

	call_info->call = call;
	call_info->start_time = ofono_call_full_start_time_get(call);
	call_info->creation_time = time(NULL);
	if (call_info->start_time == 0)
		call_info->start_time = call_info->creation_time;
	call_info->line_id = eina_stringshare_add(line_id);
	call_info->name = eina_stringshare_add(ofono_call_name_get(call));
	history->calls->list =
		eina_list_prepend(history->calls->list, call_info);
	history->calls->dirty = EINA_TRUE;

end:
	if (_history_call_info_update(call_info))
		history->calls->dirty = EINA_TRUE;
}

static void _history_call_log_save(History *history)
{
	Eet_File *efile;

	EINA_SAFETY_ON_NULL_RETURN(history->calls);
	DBG("save history (%u calls, dirty: %d) to %s",
		eina_list_count(history->calls->list), history->calls->dirty,
		history->path);

	ecore_file_unlink(history->bkp);
	ecore_file_mv(history->path, history->bkp);
	efile = eet_open(history->path, EET_FILE_MODE_WRITE);
	EINA_SAFETY_ON_NULL_RETURN(efile);
	if (!(eet_data_write(efile,
				history->edd_list, HISTORY_ENTRY,
				history->calls, EET_COMPRESSION_DEFAULT)))
		ERR("Could in the history log file");

	eet_close(efile);
}

static void _history_call_removed(void *data, OFono_Call *call)
{
	Elm_Object_Item *it;
	History *history = data;
	const char *line_id = ofono_call_line_id_get(call);
	Call_Info *call_info;
	time_t start;
	char *tm;

	call_info = _history_call_info_search(history, call);
	DBG("call=%p, id=%s, info=%p", call, line_id, call_info);
	EINA_SAFETY_ON_NULL_RETURN(call_info);

	if (call_info->start_time == 0)
		call_info->start_time = call_info->creation_time;

	start = call_info->start_time;
	tm = ctime(&start);

	call_info->end_time = time(NULL);
	call_info->call = NULL;

	if (call_info->completed)
		INF("Call end:  %s at %s", line_id, tm);
	else {
		if (!call_info->incoming)
			INF("Not answered: %s at %s", line_id, tm);
		else {
			INF("Missed: %s at %s", line_id, tm);
			if (call_info->it_missed)
				elm_genlist_item_update(call_info->it_missed);
			else {
				it = elm_genlist_item_prepend
					(history->genlist_missed,
						history->itc,
						call_info, NULL,
						ELM_GENLIST_ITEM_NONE,
						_on_item_clicked,
						call_info->line_id);
				elm_genlist_item_show
					(it, ELM_GENLIST_ITEM_SCROLLTO_IN);
				call_info->it_missed = it;
				call_info->history = history;
				_history_time_updater_start(history);
			}
		}
	}

	history->calls->dirty = EINA_TRUE;
	_history_call_log_save(history);

	if (call_info->it_all)
		elm_genlist_item_update(call_info->it_all);
	else {
		it = elm_genlist_item_prepend(history->genlist_all,
						history->itc,
						call_info, NULL,
						ELM_GENLIST_ITEM_NONE,
						_on_item_clicked,
						call_info->line_id);
		elm_genlist_item_show(it, ELM_GENLIST_ITEM_SCROLLTO_IN);
		call_info->it_all = it;
		call_info->history = history;
		_history_time_updater_start(history);
	}
}

static void _call_info_free(Call_Info *call_info)
{
	eina_stringshare_del(call_info->line_id);
	eina_stringshare_del(call_info->name);
	free(call_info);
}

static void _on_del(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	History *history = data;
	Call_Info *call_info;

	if (history->updater)
		ecore_poller_del(history->updater);

	if (history->calls->dirty)
		_history_call_log_save(history);

	ofono_call_removed_cb_del(callback_node_call_removed);
	ofono_call_changed_cb_del(callback_node_call_changed);
	eet_data_descriptor_free(history->edd);
	eet_data_descriptor_free(history->edd_list);
	EINA_LIST_FREE(history->calls->list, call_info)
		_call_info_free(call_info);
	free(history->calls);
	elm_genlist_item_class_free(history->itc);
	free(history->path);
	free(history->bkp);
	free(history);
	ecore_file_shutdown();
	eet_shutdown();
}

static void _on_hide(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	History *history = data;
	DBG("history became hidden");
	_history_time_updater_stop(history);
}

static void _on_show(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	History *history = data;
	DBG("history became visible");
	_history_time_updater_start(history);
}

static void _on_win_focus_out(void *data, Evas *e __UNUSED__,
				void *event_info __UNUSED__)
{
	History *history = data;
	DBG("window is unfocused");
	_history_time_updater_stop(history);
}

static void _on_win_focus_in(void *data, Evas *e __UNUSED__,
				void *event_info __UNUSED__)
{
	History *history = data;
	DBG("window is focused");
	_history_time_updater_start(history);
}

static void _history_call_info_descriptor_init(Eet_Data_Descriptor **edd,
						Eet_Data_Descriptor **edd_list)
{
	Eet_Data_Descriptor_Class eddc;

	EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Call_Info);
	*edd = eet_data_descriptor_stream_new(&eddc);

	EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Call_Info_List);
	*edd_list = eet_data_descriptor_stream_new(&eddc);

	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Call_Info,
					"completed", completed, EET_T_UCHAR);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Call_Info,
					"incoming", incoming, EET_T_UCHAR);

	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Call_Info,
					"start_time", start_time, EET_T_LONG_LONG);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Call_Info,
					"end_time", end_time, EET_T_LONG_LONG);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Call_Info,
					"line_id", line_id, EET_T_STRING);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Call_Info,
					"name", name, EET_T_STRING);

	EET_DATA_DESCRIPTOR_ADD_LIST(*edd_list, Call_Info_List, "list", list,
					*edd);
}

static void _history_call_log_read(History *history)
{
	Call_Info *call_info;
	Eina_List *l;
	Eet_File *efile;
	Call_Info_List *calls = NULL;
	Elm_Object_Item *it;

	efile = eet_open(history->path, EET_FILE_MODE_READ);

	if (efile) {
		calls = eet_data_read(efile, history->edd_list, HISTORY_ENTRY);
		eet_close(efile);
	}

	if (!calls) {
		efile = eet_open(history->bkp, EET_FILE_MODE_READ);
		if (efile) {
			calls = eet_data_read(efile, history->edd_list,
						HISTORY_ENTRY);
			eet_close(efile);
		}
	}

	if (!calls)
		calls = calloc(1, sizeof(Call_Info_List));

	history->calls = calls;
	EINA_SAFETY_ON_NULL_RETURN(history->calls);

	EINA_LIST_FOREACH(history->calls->list, l, call_info) {
		it = elm_genlist_item_append(history->genlist_all,
						history->itc,
						call_info, NULL,
						ELM_GENLIST_ITEM_NONE,
						_on_item_clicked,
						call_info->line_id);
		call_info->it_all = it;
		call_info->history = history;

		if (call_info->completed)
			continue;

		it = elm_genlist_item_append(history->genlist_missed,
						history->itc, call_info, NULL,
						ELM_GENLIST_ITEM_NONE,
						_on_item_clicked,
						call_info->line_id);
		call_info->it_missed = it;
		call_info->history = history;
	}

	it = elm_genlist_first_item_get(history->genlist_all);
	if (it)
		elm_genlist_item_show(it, ELM_GENLIST_ITEM_SCROLLTO_TOP);

	it = elm_genlist_first_item_get(history->genlist_missed);
	if (it)
		elm_genlist_item_show(it, ELM_GENLIST_ITEM_SCROLLTO_TOP);
}

static void _history_call_info_del(Call_Info *call_info)
{
	History *ctx = call_info->history;

	EINA_SAFETY_ON_NULL_RETURN(ctx);

	call_info->call = NULL;
	if (call_info->it_all)
		elm_object_item_del(call_info->it_all);
	if (call_info->it_missed)
		elm_object_item_del(call_info->it_missed);

	ctx->calls->list = eina_list_remove(ctx->calls->list, call_info);
	ctx->calls->dirty = EINA_TRUE;
	_history_call_log_save(ctx);

	if ((!ctx->calls->list) && (ctx->updater)) {
		ecore_poller_del(ctx->updater);
		ctx->updater = NULL;
	}

	_call_info_free(call_info);
}

static void _history_clear_do(void *data, Evas_Object *obj __UNUSED__,
				void *event_info __UNUSED__)
{
	History *ctx = data;
	Call_Info *call_info;

	DBG("ctx=%p, deleting %u entries",
		ctx, eina_list_count(ctx->calls->list));

	evas_object_del(ctx->clear_popup);
	ctx->clear_popup = NULL;

	elm_genlist_clear(ctx->genlist_all);
	elm_genlist_clear(ctx->genlist_missed);

	EINA_LIST_FREE(ctx->calls->list, call_info)
		_call_info_free(call_info);

	ctx->calls->dirty = EINA_TRUE;
	_history_call_log_save(ctx);

	if (ctx->updater) {
		ecore_poller_del(ctx->updater);
		ctx->updater = NULL;
	}

	elm_object_signal_emit(ctx->self, "toggle,off,edit", "gui");
}

static void _history_clear_cancel(void *data, Evas_Object *obj __UNUSED__,
					void *event_info __UNUSED__)
{
	History *ctx = data;

	DBG("ctx=%p", ctx);

	evas_object_del(ctx->clear_popup);
	ctx->clear_popup = NULL;
}

static void _history_clear(History *ctx)
{
	Evas_Object *p, *bt;

	EINA_SAFETY_ON_TRUE_RETURN(ctx->clear_popup != NULL);

	ctx->clear_popup = p = elm_popup_add(ctx->self);
	evas_object_size_hint_weight_set(p, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	elm_object_part_text_set(p, "title,text", "Clear History");
	elm_object_text_set(p, "Do you want to clear all history entries?");

	bt = elm_button_add(p);
	elm_object_text_set(bt, "No");
	elm_object_part_content_set(p, "button1", bt);
	evas_object_smart_callback_add(bt, "clicked",
					_history_clear_cancel, ctx);

	bt = elm_button_add(p);
	elm_object_text_set(bt, "Yes, Clear");
	elm_object_part_content_set(p, "button2", bt);
	evas_object_smart_callback_add(bt, "clicked", _history_clear_do, ctx);

	evas_object_show(p);
}

static char *_item_label_get(void *data, Evas_Object *obj __UNUSED__,
				const char *part)
{
	Call_Info *call_info = data;

	if (strncmp(part, "text.call", strlen("text.call")))
		return NULL;

	part += strlen("text.call.");

	if (!strcmp(part, "name")) {
		if (!call_info->name || call_info->name[0] == '\0')
			return phone_format(call_info->line_id);
		return strdup(call_info->name);
	}

	if (!strcmp(part, "time")) {
		if ((call_info->completed) && (call_info->end_time))
			return date_format(call_info->end_time);
		return date_format(call_info->start_time);
	}

	/* TODO: Fetch phone type from contacts information*/
	if (!strcmp(part, "type"))
		return strdup("TODO:TELEPHONE TYPE");

	ERR("Unexpected text part: %s", part);
	return NULL;
}


static Eina_Bool _item_state_get(void *data, Evas_Object *obj __UNUSED__,
					const char  *part)
{
	Call_Info *call_info = data;

	if (!strcmp(part, "missed"))
		return !call_info->completed;
	else if (!strcmp(part, "completed"))
		return call_info->completed;
	else if (!strcmp(part, "outgoing"))
		return !call_info->incoming;
	else if (!strcmp(part, "incoming"))
		return call_info->incoming;

	ERR("Unexpected state part: %s", part);
	return EINA_FALSE;
}

static void _on_clicked(void *data, Evas_Object *obj __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	History *ctx = data;

	EINA_SAFETY_ON_NULL_RETURN(emission);
	emission += strlen("clicked,");

	DBG("ctx=%p, signal: %s", ctx, emission);

	if (!strcmp(emission, "all"))
		elm_object_signal_emit(obj, "show,all", "gui");
	else if (!strcmp(emission, "missed"))
		elm_object_signal_emit(obj, "show,missed", "gui");
	else if (!strcmp(emission, "clear"))
		_history_clear(ctx);
	else if (!strcmp(emission, "edit")) {
		elm_object_signal_emit(obj, "toggle,on,edit", "gui");
		elm_genlist_decorate_mode_set(ctx->genlist_all, EINA_TRUE);
		elm_genlist_decorate_mode_set(ctx->genlist_missed, EINA_TRUE);
	} else if (!strcmp(emission, "edit,done")) {
		elm_object_signal_emit(obj, "toggle,off,edit", "gui");
		elm_genlist_decorate_mode_set(ctx->genlist_all, EINA_FALSE);
		elm_genlist_decorate_mode_set(ctx->genlist_missed, EINA_FALSE);
	}
}

static void _on_more_clicked(void *data __UNUSED__, Evas_Object *obj __UNUSED__,
				const char *emission __UNUSED__,
				const char *source __UNUSED__)
{
	DBG("TODO");
}

static void _on_del_clicked(void *data, Evas_Object *obj __UNUSED__,
				void *event_info __UNUSED__)
{
	Call_Info *call_info = data;
	DBG("call_info=%p, items all=%p missed=%p",
		call_info, call_info->it_all, call_info->it_missed);
	_history_call_info_del(call_info);
}

static Evas_Object *_item_content_get(void *data, Evas_Object *obj,
					const char *part)
{
	Evas_Object *btn = NULL;

	if (strcmp(part, "call.swallow.more") == 0) {
		btn = gui_layout_add(obj, "history/img");
		EINA_SAFETY_ON_NULL_RETURN_VAL(btn, NULL);
		elm_object_signal_callback_add(btn, "clicked,more", "gui",
						_on_more_clicked, NULL);
		evas_object_propagate_events_set(btn, EINA_FALSE);
	} else if (strcmp(part, "call.swallow.delete") == 0) {
		btn = elm_button_add(obj);
		EINA_SAFETY_ON_NULL_RETURN_VAL(btn, NULL);
		elm_object_style_set(btn, "history-delete");
		elm_object_text_set(btn, "delete");
		evas_object_smart_callback_add(btn, "clicked", _on_del_clicked,
						data);
		evas_object_propagate_events_set(btn, EINA_FALSE);
	} else
		ERR("unknown content part '%s'", part);

	return btn;
}

static void _on_list_slide_enter(void *data __UNUSED__,
					Evas_Object *obj,
					void *event_info)
{
	Elm_Object_Item *it = elm_genlist_decorated_item_get(obj);
	DBG("cancel decorated item=%p", it);
	if (it)
		elm_genlist_item_decorate_mode_set(it, "slide", EINA_FALSE);

	it = event_info;
	EINA_SAFETY_ON_NULL_RETURN(it);
	DBG("it=%p", it);
	elm_genlist_item_decorate_mode_set(it, "slide", EINA_TRUE);
}

static void _on_list_slide_cancel(void *data __UNUSED__,
					Evas_Object *obj,
					void *event_info __UNUSED__)
{
	Elm_Object_Item *it = elm_genlist_decorated_item_get(obj);
	DBG("it=%p", it);
	if (it)
		elm_genlist_item_decorate_mode_set(it, "slide", EINA_FALSE);
}

Evas_Object *history_add(Evas_Object *parent)
{
	int r;
	History *history;
	Evas *e;
	const char *config_path;
	char *path, base_dir[PATH_MAX];
	Elm_Genlist_Item_Class *itc;
	Evas_Object *obj, *genlist_all, *genlist_missed;

	eet_init();
	ecore_file_init();
	history = calloc(1, sizeof(History));
	EINA_SAFETY_ON_NULL_RETURN_VAL(history, NULL);

	history->self = obj = gui_layout_add(parent, "history_bg");
	EINA_SAFETY_ON_NULL_GOTO(obj, err_layout);

	genlist_all = elm_genlist_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(genlist_all, err_object_new);
	elm_object_style_set(genlist_all, "history");

	/* TODO: */
	evas_object_smart_callback_add(genlist_all, "drag,start,right",
					_on_list_slide_enter, history);
	evas_object_smart_callback_add(genlist_all, "drag,start,left",
					_on_list_slide_cancel, history);
	evas_object_smart_callback_add(genlist_all, "drag,start,down",
					_on_list_slide_cancel, history);
	evas_object_smart_callback_add(genlist_all, "drag,start,up",
					_on_list_slide_cancel, history);

	genlist_missed = elm_genlist_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(genlist_missed, err_object_new);
	elm_object_style_set(genlist_missed, "history");

	evas_object_smart_callback_add(genlist_missed, "drag,start,right",
					_on_list_slide_enter, history);
	evas_object_smart_callback_add(genlist_missed, "drag,start,left",
					_on_list_slide_cancel, history);
	evas_object_smart_callback_add(genlist_missed, "drag,start,down",
					_on_list_slide_cancel, history);
	evas_object_smart_callback_add(genlist_missed, "drag,start,up",
					_on_list_slide_cancel, history);

	itc = elm_genlist_item_class_new();
	EINA_SAFETY_ON_NULL_GOTO(itc, err_object_new);
	itc->item_style = "history";
	itc->func.text_get = _item_label_get;
	itc->func.content_get = _item_content_get;
	itc->func.state_get = _item_state_get;
	itc->func.del = NULL;
	itc->decorate_all_item_style = "history-delete";
	itc->decorate_item_style = "history-delete";
	history->genlist_all = genlist_all;
	history->genlist_missed = genlist_missed;
	history->itc = itc;

	elm_object_part_content_set(obj, "elm.swallow.all", genlist_all);
	elm_object_part_content_set(obj, "elm.swallow.missed", genlist_missed);
	elm_object_signal_emit(obj, "show,all", "gui");
	elm_object_signal_callback_add(obj, "clicked,*", "gui",
					_on_clicked, history);

	config_path = efreet_config_home_get();
	snprintf(base_dir, sizeof(base_dir), "%s/%s", config_path,
			PACKAGE_NAME);
	ecore_file_mkpath(base_dir);
	r = asprintf(&path,  "%s/%s/history.eet", config_path, PACKAGE_NAME);

	if (r < 0)
		goto err_item_class;

	history->path = path;
	r = asprintf(&path,  "%s/%s/history.eet.bkp", config_path,
			PACKAGE_NAME);

	if (r < 0)
		goto err_path;

	history->bkp = path;

	_history_call_info_descriptor_init(&history->edd, &history->edd_list);
	_history_call_log_read(history);
	EINA_SAFETY_ON_NULL_GOTO(history->calls, err_log_read);
	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _on_del,
					history);
	evas_object_event_callback_add(obj, EVAS_CALLBACK_HIDE, _on_hide,
					history);
	evas_object_event_callback_add(obj, EVAS_CALLBACK_SHOW, _on_show,
					history);

	e = evas_object_evas_get(obj);
	evas_event_callback_add(e, EVAS_CALLBACK_CANVAS_FOCUS_OUT,
				_on_win_focus_out, history);
	evas_event_callback_add(e, EVAS_CALLBACK_CANVAS_FOCUS_IN,
				_on_win_focus_in, history);

	callback_node_call_changed =
		ofono_call_changed_cb_add(_history_call_changed, history);
	callback_node_call_removed =
		ofono_call_removed_cb_add(_history_call_removed, history);

	return obj;

err_log_read:
	free(history->bkp);
	eet_data_descriptor_free(history->edd);
	eet_data_descriptor_free(history->edd_list);
err_path:
	free(history->path);
err_item_class:
	elm_genlist_item_class_free(itc);
err_object_new:
	free(obj);
err_layout:
	free(history);
	ecore_file_shutdown();
	eet_shutdown();
	return NULL;
}
