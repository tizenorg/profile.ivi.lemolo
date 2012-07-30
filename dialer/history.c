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

#define HISTORY_ENTRY "history"

typedef struct _Call_Info_List {
	Eina_List *list;
} Call_Info_List;

typedef struct _History {
	Eet_File *log;
	Eet_Data_Descriptor *edd;
	Eet_Data_Descriptor *edd_list;
	Call_Info_List *calls;
	Elm_Genlist_Item_Class *itc;
	Evas_Object *genlist_all, *genlist_missed;
	Elm_Object_Item *all, *missed;
} History;

typedef struct _Call_Info {
	int state;
	long long start_time;
	long long end_time;
	const char *line_id;
	const char *name;
} Call_Info;

static OFono_Callback_List_Call_Node *callback_node_call_removed = NULL;
static OFono_Callback_List_Call_Node *callback_node_call_changed = NULL;

static Call_Info *_history_call_info_list_search(Eina_List *list,
							const char *line_id)
{
	Call_Info *call_info;
	Eina_List *l;

	EINA_LIST_FOREACH(list, l, call_info)
		if (!strcmp(call_info->line_id, line_id))
			return call_info;

	return NULL;
}

static void _history_call_changed(void *data, OFono_Call *call)
{
	History *history = data;
	const char *line_id = ofono_call_line_id_get(call);
	Call_Info *call_info;
	OFono_Call_State state = ofono_call_state_get(call);

	call_info =
		_history_call_info_list_search(history->calls->list, line_id);

	if (call_info) {
		/* Otherwise I missed the call or the person didn't
		 * awnser my call!
		 */
		if ((call_info->state == OFONO_CALL_STATE_INCOMING ||
			call_info->state == OFONO_CALL_STATE_DIALING) &&
			state != OFONO_CALL_STATE_DISCONNECTED)
			call_info->state = state;
		return;
	}
	call_info = calloc(1, sizeof(Call_Info));
	EINA_SAFETY_ON_NULL_RETURN(call_info);

	call_info->state = state;
	call_info->start_time = time(NULL);
	call_info->line_id = eina_stringshare_add(line_id);
	call_info->name = eina_stringshare_add(ofono_call_name_get(call));
	history->calls->list =
		eina_list_prepend(history->calls->list, call_info);
}

static void _history_call_log_save(History *history)
{
	if (!(eet_data_write(history->log,
				history->edd_list, HISTORY_ENTRY,
				history->calls, EET_COMPRESSION_DEFAULT)))
		ERR("Could in the history log file");
}

static void _history_call_removed(void *data, OFono_Call *call)
{
	History *history = data;
	const char *line_id = ofono_call_line_id_get(call);
	Call_Info *call_info;
	time_t start;
	char *tm;

	call_info =
		_history_call_info_list_search(history->calls->list, line_id);
	EINA_SAFETY_ON_NULL_RETURN(call_info);
	start = call_info->start_time;
	tm = ctime(&start);

	if (call_info->state == OFONO_CALL_STATE_INCOMING) {
		INF("Missed call - Id: %s - time: %s", line_id, tm);
		elm_genlist_item_append(history->genlist_missed, history->itc,
					call_info, NULL, ELM_GENLIST_ITEM_NONE,
					NULL, NULL);
	} else if (call_info->state == OFONO_CALL_STATE_DIALING)
		INF("Call not answered - Id: %s - time: %s", line_id, tm);
	else
		INF("A call has ended - Id: %s - time: %s", line_id, tm);

	call_info->end_time = time(NULL);
	_history_call_log_save(history);
	elm_genlist_item_append(history->genlist_all, history->itc, call_info,
				NULL, ELM_GENLIST_ITEM_NONE, NULL, NULL);
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
	ofono_call_removed_cb_del(callback_node_call_removed);
	ofono_call_changed_cb_del(callback_node_call_changed);
	eet_close(history->log);
	eet_data_descriptor_free(history->edd);
	eet_data_descriptor_free(history->edd_list);
	EINA_LIST_FREE(history->calls->list, call_info)
		_call_info_free(call_info);
	free(history->calls);
	elm_genlist_item_class_free(history->itc);
	free(history);
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
					"state", state, EET_T_INT);
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

	history->calls = eet_data_read(history->log, history->edd_list,
							HISTORY_ENTRY);

	if (!history->calls) {
		history->calls = calloc(1, sizeof(Call_Info_List));
		return;
	}

	EINA_LIST_FOREACH(history->calls->list, l, call_info) {
		if (!call_info)
			continue;
		elm_genlist_item_append(history->genlist_all, history->itc,
					call_info, NULL, ELM_GENLIST_ITEM_NONE,
					NULL, NULL);
		if (call_info->state == OFONO_CALL_STATE_INCOMING)
			elm_genlist_item_append(history->genlist_missed,
						history->itc, call_info, NULL,
						ELM_GENLIST_ITEM_NONE,
						NULL, NULL);
	}
}

static char *_item_label_get(void *data, Evas_Object *obj __UNUSED__,
				const char *part __UNUSED__)
{
	Call_Info *call_info = data;
	char *buf;
	const char *name, *call_state;
	char *t;

	name = call_info->name;

	if (!call_info->name || call_info->name[0] == '\0')
		name = call_info->line_id;

	if (call_info->state == OFONO_CALL_STATE_INCOMING)
		call_state = "Missed";
	else if (call_info->state == OFONO_CALL_STATE_DIALING)
		call_state = "Not Awnsered";
	else
		call_state = "Completed";

	t = date_format(call_info->end_time);

	if (asprintf(&buf, "%s-%s-%s", name, call_state, t) < 0)
		buf = strdup("");

	free(t);
	return buf;
}

static void _btn_naviframe_next_click(void *data, Evas_Object *obj __UNUSED__,
					void *event_inf __UNUSED__)
{
	History *history = data;
	elm_naviframe_item_promote(history->missed);
}

static void _btn_naviframe_prev_click(void *data, Evas_Object *obj __UNUSED__,
					void *event_inf __UNUSED__)
{
	History *history = data;
	elm_naviframe_item_promote(history->all);
}

Evas_Object *history_add(Evas_Object *parent)
{
	History *history;
	const char *config_path;
	char path[PATH_MAX];
	Elm_Genlist_Item_Class *itc;
	Evas_Object *obj, *genlist_all, *genlist_missed, *btn;

	eet_init();
	history = calloc(1, sizeof(History));
	EINA_SAFETY_ON_NULL_RETURN_VAL(history, NULL);

	obj = elm_naviframe_add(parent);
	EINA_SAFETY_ON_NULL_GOTO(obj, err_naviframe);
	elm_naviframe_prev_btn_auto_pushed_set(obj, EINA_FALSE);

	genlist_all = elm_genlist_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(genlist_all, err_object_new);

	genlist_missed = elm_genlist_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(genlist_missed, err_object_new);

	itc = elm_genlist_item_class_new();
	EINA_SAFETY_ON_NULL_GOTO(itc, err_object_new);
	itc->item_style = "default";
	itc->func.text_get = _item_label_get;
	itc->func.content_get = NULL;
	itc->func.state_get = NULL;
	itc->func.del = NULL;
	history->genlist_all = genlist_all;
	history->genlist_missed = genlist_missed;

	btn = elm_button_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(btn, err_item_class);
	elm_object_text_set(btn, "Missed");
	evas_object_smart_callback_add(btn, "clicked",
					_btn_naviframe_next_click, history);

	Elm_Object_Item *all =
		elm_naviframe_item_push(obj, "All", NULL, btn, genlist_all,
					NULL);
	EINA_SAFETY_ON_NULL_GOTO(all, err_item_class);

	btn = elm_button_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(btn, err_item_class);
	elm_object_text_set(btn, "All");
	evas_object_smart_callback_add(btn, "clicked",
					_btn_naviframe_prev_click, history);

	Elm_Object_Item *missed =
		elm_naviframe_item_push(obj, "Missed", btn, NULL,
					genlist_missed, NULL);
	EINA_SAFETY_ON_NULL_GOTO(missed, err_item_class);
	elm_naviframe_item_promote(all);

	history->all = all;
	history->missed = missed;
	history->itc = itc;

	config_path = efreet_config_home_get();
	snprintf(path, sizeof(path), "%s/%s", config_path, PACKAGE_NAME);
	ecore_file_mkpath(path);
	snprintf(path, sizeof(path), "%s/%s/history.eet", config_path,
			PACKAGE_NAME);
	history->log = eet_open(path, EET_FILE_MODE_READ_WRITE);
	EINA_SAFETY_ON_NULL_RETURN_VAL(history->log, NULL);

	_history_call_info_descriptor_init(&history->edd, &history->edd_list);
	_history_call_log_read(history);
	EINA_SAFETY_ON_NULL_RETURN_VAL(history->calls, NULL);
	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _on_del,
					history);
	callback_node_call_changed =
		ofono_call_changed_cb_add(_history_call_changed, history);
	callback_node_call_removed =
		ofono_call_removed_cb_add(_history_call_removed, history);
	return obj;

err_item_class:
	elm_genlist_item_class_free(itc);
err_object_new:
	free(obj);
err_naviframe:
	free(history);
	return NULL;
}
