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
	Evas_Object *genlist_all, *genlist_missed;
} History;

typedef struct _Call_Info {
	long long start_time;
	long long end_time;
	const char *line_id;
	const char *name;
	Eina_Bool completed;
	Eina_Bool incoming;
	const OFono_Call *call; /* not in edd */
} Call_Info;

static OFono_Callback_List_Call_Node *callback_node_call_removed = NULL;
static OFono_Callback_List_Call_Node *callback_node_call_changed = NULL;

static Call_Info *_history_call_info_search(const History *history,
						const OFono_Call *call)
{
	Call_Info *call_info;
	Eina_List *l;

	EINA_LIST_FOREACH(history->calls->list, l, call_info)
		if (call_info->call == call)
			return call_info;

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
			call_info->completed = EINA_TRUE;
			return EINA_TRUE;
		}
	}

	return EINA_FALSE;
}

static void _on_item_clicked(void *data, Evas_Object *obj __UNUSED__,
				void *event_inf __UNUSED__)
{
	const char *number = data;
	gui_number_set(number, EINA_TRUE);
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
	call_info->start_time = time(NULL);
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
	History *history = data;
	const char *line_id = ofono_call_line_id_get(call);
	Call_Info *call_info;
	time_t start;
	char *tm;

	call_info = _history_call_info_search(history, call);
	DBG("call=%p, id=%s, info=%p", call, line_id, call_info);
	EINA_SAFETY_ON_NULL_RETURN(call_info);
	start = call_info->start_time;
	tm = ctime(&start);

	if (call_info->completed)
		INF("Call end:  %s at %s", line_id, tm);
	else {
		if (!call_info->incoming)
			INF("Not answered: %s at %s", line_id, tm);
		else {
			INF("Missed: %s at %s", line_id, tm);
			elm_genlist_item_prepend(history->genlist_missed,
							history->itc,
							call_info, NULL,
							ELM_GENLIST_ITEM_NONE,
							_on_item_clicked,
							call_info->line_id);
		}
	}

	call_info->end_time = time(NULL);
	call_info->call = NULL;
	history->calls->dirty = EINA_TRUE;
	_history_call_log_save(history);

	elm_genlist_item_prepend(history->genlist_all, history->itc, call_info,
					NULL, ELM_GENLIST_ITEM_NONE,
					_on_item_clicked, call_info->line_id);
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
		elm_genlist_item_append(history->genlist_all, history->itc,
					call_info, NULL, ELM_GENLIST_ITEM_NONE,
					_on_item_clicked, call_info->line_id);
		if (!call_info->completed)
			elm_genlist_item_append(history->genlist_missed,
						history->itc, call_info, NULL,
						ELM_GENLIST_ITEM_NONE,
						_on_item_clicked,
						call_info->line_id);
	}
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

	if (!strcmp(part, "time"))
		return date_format(call_info->end_time);

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

static void _on_clicked(void *data __UNUSED__, Evas_Object *obj __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	EINA_SAFETY_ON_NULL_RETURN(emission);
	emission += strlen("clicked,");

	if (!strcmp(emission, "all"))
		elm_object_signal_emit(obj, "show,all", "gui");
	else
		elm_object_signal_emit(obj, "show,missed", "gui");
}

static void _on_more_clicked(void *data __UNUSED__, Evas_Object *obj __UNUSED__,
				const char *emission __UNUSED__,
				const char *source __UNUSED__)
{
	DBG("TODO");
}

static Evas_Object *_item_content_get(void *data __UNUSED__, Evas_Object *obj,
					const char *part __UNUSED__)
{
	Evas_Object *btn;

	btn = gui_layout_add(obj, "history/img");
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);
	elm_object_signal_callback_add(btn, "clicked,more", "gui",
					_on_more_clicked, NULL);
	evas_object_propagate_events_set(btn, EINA_FALSE);

	return btn;
}

Evas_Object *history_add(Evas_Object *parent)
{
	int r;
	History *history;
	const char *config_path;
	char *path, base_dir[PATH_MAX];
	Elm_Genlist_Item_Class *itc;
	Evas_Object *obj, *genlist_all, *genlist_missed;

	eet_init();
	ecore_file_init();
	history = calloc(1, sizeof(History));
	EINA_SAFETY_ON_NULL_RETURN_VAL(history, NULL);

	obj = gui_layout_add(parent, "history_bg");
	EINA_SAFETY_ON_NULL_GOTO(obj, err_layout);

	genlist_all = elm_genlist_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(genlist_all, err_object_new);
	elm_object_style_set(genlist_all, "history");

	genlist_missed = elm_genlist_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(genlist_missed, err_object_new);
	elm_object_style_set(genlist_missed, "history");

	itc = elm_genlist_item_class_new();
	EINA_SAFETY_ON_NULL_GOTO(itc, err_object_new);
	itc->item_style = "history";
	itc->func.text_get = _item_label_get;
	itc->func.content_get = _item_content_get;
	itc->func.state_get = _item_state_get;
	itc->func.del = NULL;
	history->genlist_all = genlist_all;
	history->genlist_missed = genlist_missed;
	history->itc = itc;

	elm_object_part_content_set(obj, "elm.swallow.all", genlist_all);
	elm_object_part_content_set(obj, "elm.swallow.missed", genlist_missed);
	elm_object_signal_emit(obj, "show,all", "gui");
	elm_object_signal_callback_add(obj, "clicked,*", "gui",
					_on_clicked, NULL);

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
