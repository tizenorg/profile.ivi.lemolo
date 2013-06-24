#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <Ecore.h>
#include <string.h>
#include <ctype.h>
#include "log.h"
#include "util.h"
#include "gui.h"
#include "overview.h"
#include "ofono.h"

typedef struct _Compose
{
	Evas_Object *layout;
	Evas_Object *entry_msg;
	Evas_Object *mb_entry;
	Evas_Object *genlist;
	Evas_Object *genlist_contacts;
	Elm_Genlist_Item_Class *itc_inc;
	Elm_Genlist_Item_Class *itc_out;
	Elm_Genlist_Item_Class *itc_c_name;
	Eina_Bool composing;
	Eina_List *current_thread;
	const char *number;
	Eina_List *composing_numbers;
	Ecore_Poller *updater;
	double last_update;
} Compose;

typedef struct _Contact_Genlist {
	const char *name;
	const char *number;
	const char *type;
	const char *picture;
	Compose *compose;
} Contact_Genlist;

static OFono_Callback_List_Incoming_SMS_Node *incoming_sms = NULL;
static OFono_Callback_List_Sent_SMS_Node *sent_sms = NULL;

static void _send_sms(Compose *compose);
static void _compose_timer_updater_start(Compose *compose);

static void _message_remove_from_genlist(Message *msg)
{
	Elm_Object_Item *it;

	it = message_object_item_get(msg);
	EINA_SAFETY_ON_NULL_RETURN(it);
	elm_object_item_del(it);
}

static void _message_from_file_delete(Compose *compose, Message *msg,
					const char *contact)
{
	Message *msg_aux;
	Eina_List *last;

	gui_message_from_file_delete(msg, contact);
	_message_remove_from_genlist(msg);

	last = eina_list_last(compose->current_thread);
	msg_aux = eina_list_data_get(last);

	if (msg_aux == msg) {
		msg_aux = eina_list_data_get(eina_list_prev(last));
		if (msg_aux != NULL)
			gui_overview_genlist_update(msg_aux, contact);
	}
	compose->current_thread = eina_list_remove(compose->current_thread,
							msg);
	message_del(msg);
}

static void _on_del(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Compose *compose = data;
	Message *msg;
	const char *number;

	ofono_incoming_sms_cb_del(incoming_sms);
	ofono_sent_sms_changed_cb_del(sent_sms);
	elm_genlist_item_class_free(compose->itc_inc);
	elm_genlist_item_class_free(compose->itc_out);
	elm_genlist_item_class_free(compose->itc_c_name);
	EINA_LIST_FREE(compose->current_thread, msg)
		message_del(msg);

	EINA_LIST_FREE(compose->composing_numbers, number)
		eina_stringshare_del(number);

	eina_stringshare_del(compose->number);
	free(compose);
}

static void _compose_exit(Compose *compose)
{
	Message *msg;
	const char *number;

	compose->composing = EINA_TRUE;
	eina_stringshare_replace(&(compose->number), NULL);
	elm_object_part_text_set(compose->layout, "elm.text.name",
					"New Message");
	elm_object_signal_emit(compose->layout, "hidden,genlist", "gui");
	elm_object_signal_emit(compose->layout, "composing", "gui");
	elm_genlist_clear(compose->genlist);
	elm_genlist_clear(compose->genlist_contacts);

	EINA_LIST_FREE(compose->current_thread, msg)
		message_del(msg);

	EINA_LIST_FREE(compose->composing_numbers, number)
		eina_stringshare_del(number);

	elm_object_part_text_set(compose->entry_msg, NULL, "");
	elm_genlist_decorate_mode_set(compose->genlist, EINA_FALSE);
	elm_multibuttonentry_clear(compose->mb_entry);
	elm_object_signal_emit(compose->layout, "contacts,hidden", "gui");
	elm_object_signal_emit(compose->layout, "names_count,hidden", "gui");
	gui_compose_exit();
}

static void _on_layout_clicked(void *data, Evas_Object *o,
			const char *emission, const char *source __UNUSED__)
{
	Compose *compose = data;
	DBG("signal: %s", emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "clicked,"));
	emission += strlen("clicked,");

	if (strcmp(emission, "back") == 0)
		_compose_exit(compose);
	else if (strcmp(emission, "send_msg") == 0)
		_send_sms(compose);
	else if (strcmp(emission, "edit") == 0) {
		elm_object_signal_emit(o, "toggle,on,edit", "gui");
		elm_genlist_decorate_mode_set(compose->genlist, EINA_TRUE);
	} else if (strcmp(emission, "edit,done") == 0) {
		elm_object_signal_emit(o, "toggle,off,edit", "gui");
		elm_genlist_decorate_mode_set(compose->genlist, EINA_FALSE);
	} else if (strcmp(emission, "clear") == 0) {
		const char *contact = compose->number;
		gui_overview_all_contact_messages_clear(contact);
		Message *msg;
		EINA_LIST_FREE(compose->current_thread, msg) {
			_message_remove_from_genlist(msg);
			message_del(msg);
		}
		elm_object_signal_emit(o, "toggle,off,edit", "gui");
	} else
		ERR("Unkown emission: %s", emission);
}

static void _send_sms_reply(void *data __UNUSED__, OFono_Error error,
				OFono_Sent_SMS *sms)
{
	if (error != OFONO_ERROR_NONE) {
		ERR("Error when trying to send a new message");
		return;
	}

	DBG("SMS Sent to: %s, message: %s", ofono_sent_sms_destination_get(sms),
		ofono_sent_sms_message_get(sms));
}

static void _send_sms(Compose *compose)
{
	const char *msg_content;
	char *msg_utf;
	Message *msg;
	Elm_Object_Item *it;
	Contact_Info *c_info;
	const char *to;
	Eina_List *l;

	if (elm_entry_is_empty(compose->entry_msg)) {
		DBG("Returning, empty message");
		return;
	}

	if (compose->composing && (!compose->composing_numbers)) {
		DBG("Returning, no contacts to send");
		return;
	}

	msg_content = elm_object_part_text_get(compose->entry_msg, NULL);

	msg_utf = elm_entry_markup_to_utf8(msg_content);

	msg = message_new(time(NULL), msg_utf,
				EINA_FALSE, OFONO_SENT_SMS_STATE_PENDING);
	EINA_SAFETY_ON_NULL_RETURN(msg);
	message_data_set(msg, compose);
	it = elm_genlist_item_append(compose->genlist, compose->itc_out,
					msg, NULL, ELM_GENLIST_ITEM_NONE, NULL,
					NULL);
	elm_genlist_item_show(it, ELM_GENLIST_ITEM_SCROLLTO_IN);
	message_object_item_set(msg, it);
	elm_object_signal_emit(compose->layout, "show,genlist", "gui");

	compose->current_thread =
		eina_list_append(compose->current_thread, msg);
	elm_object_part_text_set(compose->entry_msg, NULL, "");

	_compose_timer_updater_start(compose);

	if (!compose->composing) {
		ofono_sms_send(compose->number, msg_utf, _send_sms_reply, NULL);
		DBG("New Message to: %s content: %s", compose->number, msg_utf);
		c_info = gui_contact_search(compose->number, NULL);

		if (!c_info)
			elm_object_part_text_set(compose->layout,
							"elm.text.name",
							compose->number);
		else {
			to = contact_info_full_name_get(c_info);
			elm_object_part_text_set(compose->layout,
							"elm.text.name",
							to);
		}
	} else {
		const char *names = NULL, *name;
		char size[5];
		const Eina_List *items;
		int i = 0;
		items = elm_multibuttonentry_items_get(compose->mb_entry);
		EINA_SAFETY_ON_NULL_GOTO(items, exit);

		EINA_LIST_FOREACH(compose->composing_numbers, l, to) {
			ofono_sms_send(to, msg_utf, _send_sms_reply, NULL);
			DBG("New Message to: %s content: %s", to, msg_utf);
			it = eina_list_nth(items, i);
			name = elm_object_item_part_text_get(it, NULL);
			if (i == 0)
				names = eina_stringshare_add(name);
			else
				names = eina_stringshare_printf("%s, %s",
								names, name);
			i++;
		}

		elm_object_part_text_set(compose->layout,
						"elm.text.name", names);
		snprintf(size, sizeof(size), "%d",
				eina_list_count(compose->composing_numbers));
		elm_object_signal_emit(compose->layout, "names_count,show",
					"gui");
		elm_object_part_text_set(compose->layout,
						"elm.text.names_count",
						size);
		eina_stringshare_del(names);
	}
exit:
	free(msg_utf);
}

static char *_item_c_name_label_get(void *data, Evas_Object *obj __UNUSED__,
					const char *part)
{
	Contact_Genlist *c_genlist = data;

	if (strncmp(part, "text.contact.", strlen("text.contact.")))
		return NULL;

	part += strlen("text.contact.");

	if (strcmp(part, "name") == 0)
		return strdup(c_genlist->name);
	else if (strcmp(part, "number") == 0)
		return strdup(c_genlist->number);
	else if (strcmp(part, "type") == 0)
		return strdup(c_genlist->type);

	ERR("Unexpected text part: %s", part);
	return NULL;
}

static Evas_Object *_item_c_name_content_get(void *data, Evas_Object *obj,
						const char *part)
{
	Contact_Genlist *c_genlist = data;

	if (strncmp(part, "elm.swallow.", strlen("elm.swallow.")))
		return NULL;

	part += strlen("elm.swallow.");

	if (strcmp(part, "photo") == 0)
		return picture_icon_get(obj, c_genlist->picture);

	ERR("Unexpected part name: %s", part);
	return NULL;
}


static char *_item_label_get(void *data, Evas_Object *obj __UNUSED__,
				const char *part)
{
	Message *msg = data;

	if (strncmp(part, "text.msg.", strlen("text.msg.")))
		return NULL;

	part += strlen("text.msg.");

	if (strcmp(part, "content") == 0) {
		const char *content = message_content_get(msg);
		if (!content)
			return NULL;
		return elm_entry_utf8_to_markup(content);
	}

	if (strcmp(part, "time") == 0) {
		time_t t_msg = message_time_get(msg);
		if (t_msg == -1)
			return NULL;
		return date_format(t_msg);
	}

	ERR("Unexpected text part: %s", part);
	return NULL;
}

static Eina_Bool _item_state_get(void *data, Evas_Object *obj __UNUSED__,
		const char *part)
{
	Message *msg = data;

	if (strcmp(part, "sent") == 0) {
		if (message_state_get(msg) == OFONO_SENT_SMS_STATE_SENT)
			return EINA_TRUE;
		return EINA_FALSE;
	} else if (strcmp(part, "failed") == 0) {
		if (message_state_get(msg) ==  OFONO_SENT_SMS_STATE_FAILED)
			return EINA_TRUE;
		return EINA_FALSE;
	}

	ERR("Unexpected state part: %s", part);
	return EINA_FALSE;
}

static void _sms_size_calc(const char *str, int *size, int *max)
{
	Eina_Bool all_isprint = EINA_TRUE;
	const char *s;

        for (s = str; *s != '\0'; s++) {
                if (!isprint(*s)) {
                        all_isprint = EINA_FALSE;
                        break;
                }
        }

	*size = strlen(str);

        if (all_isprint) {
                if (*size <= 160)
                        *max = 160;
		else
			*max = ((*size / 153) + 1) * 153;
        } else {
                if (*size <= 70)
                        *max = 70;
		else
			*max = ((*size / 67) + 1) * 67;
        }
}

static void _on_text_changed(void *data, Evas_Object *obj,
			void *event_info __UNUSED__)
{
	Compose *compose = data;
	const char *msg = elm_object_part_text_get(obj, NULL);
	Evas_Object *ed;
	char *msg_utf8;
	int size, max;
	char buf[PATH_MAX];
	Edje_Message_Int_Set *ed_msg;

	if (!msg)
		return;

	msg_utf8 = elm_entry_markup_to_utf8(msg);
	_sms_size_calc(msg_utf8, &size, &max);
	snprintf(buf,sizeof(buf), "%d", size);
	elm_object_part_text_set(compose->layout, "elm.text.size", buf);
	snprintf(buf,sizeof(buf), "%d", max);
	elm_object_part_text_set(compose->layout, "elm.text.max_size", buf);
	free(msg_utf8);

	ed_msg = alloca(sizeof(Edje_Message_Float_Set) + sizeof(int));
	ed_msg->count = 2;
	ed_msg->val[0] = size;
	ed_msg->val[1] = max;
	ed = elm_layout_edje_get(compose->layout);
	edje_object_message_send(ed, EDJE_MESSAGE_INT_SET, 1, ed_msg);
}

static void _incoming_sms_cb(void *data, unsigned int sms_class,
				time_t timestamp, const char *sender,
				const char *message)
{
	Compose *compose = data;
	Message *msg;
	Elm_Object_Item *it;

	/* Users can only send class 1. This is OFono/GSM detail */
	if (sms_class != 1)
		return;

	if (compose->number && strcmp(compose->number, sender) != 0)
		return;

	msg = message_new(timestamp, message, EINA_FALSE,
				OFONO_SENT_SMS_STATE_SENT);

	EINA_SAFETY_ON_NULL_RETURN(msg);
	message_data_set(msg, compose);
	it = elm_genlist_item_append(compose->genlist, compose->itc_inc, msg,
					NULL, ELM_GENLIST_ITEM_NONE, NULL,
					NULL);
	elm_genlist_item_show(it, ELM_GENLIST_ITEM_SCROLLTO_TOP);
	message_object_item_set(msg, it);
	compose->current_thread =
		eina_list_append(compose->current_thread, msg);

	_compose_timer_updater_start(compose);
}

static Eina_Bool _compose_time_updater(void *data)
{
	Compose *ctx = data;
	double now = ecore_loop_time_get();
	const double interval_threshold = 30.0;
	Elm_Object_Item *it;
	const long long update_threshold = time(NULL) - WEEK - DAY;

	if (!ctx->current_thread) {
		ctx->updater = NULL;
		return EINA_FALSE;
	}

	if (now - ctx->last_update < interval_threshold)
		return EINA_TRUE;
	ctx->last_update = now;

	it = elm_genlist_first_item_get(ctx->genlist);
	for (; it != NULL; it = elm_genlist_item_next_get(it)) {
		Message *msg = elm_object_item_data_get(it);
		long long t = message_time_get(msg);
		if (EINA_UNLIKELY(t == 0)) {
			t = message_time_get(msg);
		}
		if (EINA_UNLIKELY(t < update_threshold))
			break;
		elm_genlist_item_update(it);
	}

	return EINA_TRUE;
}

static void _compose_timer_updater_start(Compose *compose)
{
	Evas *e = evas_object_evas_get(compose->layout);
	Eina_Bool win_focused = evas_focus_state_get(e);
	Eina_Bool obj_visible = evas_object_visible_get(compose->layout);

	DBG("poller %p, win_focused=%hhu, obj_visible=%hhu",
		compose->updater, win_focused, obj_visible);
	if (compose->updater)
		return;
	if (!compose->current_thread)
		return;
	if ((!win_focused) || (!obj_visible))
		return;

	DBG("start poller messages");
	/* ECORE_POLLER_CORE is 1/8th of second. */
	compose->updater = ecore_poller_add(ECORE_POLLER_CORE, 8 * 60,
						_compose_time_updater,
						compose);
	_compose_time_updater(compose);
}

static void _compose_time_updater_stop(Compose *compose)
{
	Evas *e = evas_object_evas_get(compose->layout);
	Eina_Bool win_focused = evas_focus_state_get(e);
	Eina_Bool obj_visible = evas_object_visible_get(compose->layout);

	DBG("poller %p, win_focused=%hhu, obj_visible=%hhu",
		compose->updater, win_focused, obj_visible);
	if (!compose->updater)
		return;
	if (win_focused && obj_visible)
		return;

	DBG("delete poller %p", compose->updater);
	ecore_poller_del(compose->updater);
	compose->updater = NULL;
}

static void _on_show(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Compose *compose = data;
	DBG("Overview became visible");
	_compose_timer_updater_start(compose);
}

static void _on_win_focus_in(void *data, Evas *e __UNUSED__,
				void *event_info __UNUSED__)
{
	Compose *compose = data;
	DBG("window is focused");
	_compose_timer_updater_start(compose);
}

static void _on_win_focus_out(void *data, Evas *e __UNUSED__,
				void *event_info __UNUSED__)
{
	Compose *compose = data;
	DBG("window is unfocused");
	_compose_time_updater_stop(compose);
}

static void _on_hide(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Compose *compose = data;
	DBG("Overview became hidden");
	_compose_time_updater_stop(compose);
}

static void _on_del_clicked(void *data, Evas_Object *obj __UNUSED__,
				void *event_info __UNUSED__)
{
	Message *msg = data;
	Compose *compose = message_data_get(msg);

	EINA_SAFETY_ON_NULL_RETURN(compose);
	_message_from_file_delete(compose, msg, compose->number);
}

static void _on_resend_clicked(void *data, Evas_Object *obj __UNUSED__,
				void *event_info __UNUSED__)
{
	Message *msg = data;
	const char *content, *number;
	Compose *compose = message_data_get(msg);

	EINA_SAFETY_ON_NULL_RETURN(compose);

	content = message_content_get(msg);
	EINA_SAFETY_ON_NULL_RETURN(content);
	number = message_phone_get(msg);
	EINA_SAFETY_ON_NULL_RETURN(number);

	INF("%s %s", number, content);
	ofono_sms_send(number, content, _send_sms_reply, NULL);
}

static Evas_Object *_item_content_get(void *data, Evas_Object *obj,
				const char *part)
{
	Evas_Object *btn = NULL;
	Message *msg = data;

	if (strcmp(part, "msg.swallow.delete") == 0) {
		btn = elm_button_add(obj);
		EINA_SAFETY_ON_NULL_RETURN_VAL(btn, NULL);
		elm_object_style_set(btn, "history-delete");
		elm_object_text_set(btn, "delete");
		evas_object_smart_callback_add(btn, "clicked", _on_del_clicked,
						msg);
		evas_object_propagate_events_set(btn, EINA_FALSE);
	} else if (strcmp(part, "swallow.btn.resend") == 0) {
		btn = elm_button_add(obj);
		EINA_SAFETY_ON_NULL_RETURN_VAL(btn, NULL);
		elm_object_style_set(btn, "compose-resend");
		elm_object_text_set(btn, "Failed. Try Again?");
		evas_object_smart_callback_add(btn, "clicked", _on_resend_clicked,
						msg);
		evas_object_propagate_events_set(btn, EINA_FALSE);
	} else
		ERR("unknown content part '%s'", part);

	return btn;
}

static void _sent_sms_cb(void *data, OFono_Error error, OFono_Sent_SMS *sms)
{
	Compose *compose = data;
	Eina_List *l;
	Message *msg;

	if (error != OFONO_ERROR_NONE) {
		ERR("OFono error - Sending a SMS");
		return;
	}

	EINA_LIST_FOREACH(compose->current_thread, l, msg) {
		const char *m_sms, *m_msg;
		time_t t_msg, t_sms;
		unsigned char state;

		m_sms = ofono_sent_sms_message_get(sms);
		m_msg = message_content_get(msg);
		t_msg = message_time_get(msg);
		state = message_state_get(msg);
		t_sms = ofono_sent_sms_timestamp_get(sms);

		if ((m_sms == m_msg && t_msg == t_sms) ||
			(m_sms == m_msg && state == OFONO_SENT_SMS_STATE_FAILED)) {
			message_state_set(msg, ofono_sent_sms_state_get(sms));
			elm_genlist_item_update(message_object_item_get(msg));
			break;
		}
	}
}

static void _on_c_genlist_clicked(void *data, Evas_Object *obj __UNUSED__,
					void *event_info)
{
	Elm_Object_Item *it = event_info;
	Contact_Genlist *c_genlist = data;
	Compose *compose = c_genlist->compose;

	elm_genlist_item_selected_set(it, EINA_FALSE);
	/* It will be used in _item_filter_cb so we will know if this
	 * number was added already
	 */
	elm_multibuttonentry_item_prepend(compose->mb_entry, c_genlist->number,
						NULL, NULL);
}

Contact_Genlist *_contact_genlist_new(const char *name,
					const char *number,
					const char *type,
					const char *picture,
					Compose *compose)
{
	Contact_Genlist *c_genlist = calloc(1, sizeof(Contact_Genlist));
	EINA_SAFETY_ON_NULL_RETURN_VAL(c_genlist, NULL);
	c_genlist->name = eina_stringshare_add(name);
	c_genlist->number = eina_stringshare_add(number);
	c_genlist->type = eina_stringshare_add(type);
	c_genlist->picture = eina_stringshare_add(picture);
	c_genlist->compose = compose;
	return c_genlist;
}

static void _on_text_mb_entry_changed(void *data, Evas_Object *obj,
			void *event_info __UNUSED__)
{
	Compose *compose = data;
	Contact_Partial_Match *pm;
	Eina_List *result, *l;
	const char *query;

	query = elm_object_part_text_get(obj, NULL);

	elm_genlist_clear(compose->genlist_contacts);

	if ((!query) || (*query == '\0')) {
		elm_object_signal_emit(compose->layout,
					"contacts,hidden", "gui");
		return;
	}

	DBG("Searching for contact with number: %s", query);

	result = gui_contact_partial_match_search(query);
	if (!result) {
		DBG("No contact match: '%s'", query);
		elm_object_signal_emit(compose->layout,
					"contacts,hidden", "gui");
		return;
	}

	EINA_LIST_FOREACH(result, l, pm) {
		const Contact_Info *c_info;
		const char *type, *name, *number, *picture;
		Contact_Genlist *c_genlist;

		type = contact_partial_match_type_get(pm);
		c_info = contact_partial_match_info_get(pm);
		name = contact_info_full_name_get(c_info);
		number = contact_info_detail_get(c_info, type);
		picture = contact_info_picture_get(c_info);

		c_genlist = _contact_genlist_new(name, number, type,
							picture, compose);
		EINA_SAFETY_ON_NULL_RETURN(c_genlist);
		elm_genlist_item_append(compose->genlist_contacts,
					compose->itc_c_name,
					c_genlist, NULL,
					ELM_GENLIST_ITEM_NONE,
					_on_c_genlist_clicked,
					c_genlist);
	}
	contact_partial_match_search_free(result);

	elm_object_signal_emit(compose->layout, "contacts,show", "gui");
}

static void _on_item_added(void *data, Evas_Object *obj __UNUSED__,
					void *event_info)
{
	Compose *compose = data;
	const char *number, *name;
	Contact_Info *c_info;
	Elm_Object_Item *it = event_info;

	number = elm_object_item_part_text_get(it, NULL);
	DBG("Item added number = %s", number);

	elm_object_signal_emit(compose->layout, "contacts,hidden", "gui");

	if (!number)
		return;

	c_info = gui_contact_search(number, NULL);

	if (!c_info) {
		DBG("No contact found");
		return;
	}

	name = contact_info_full_name_get(c_info);
	DBG("Number=%s Name=%s", number, name);
	elm_object_item_part_text_set(it, NULL, name);
}

static Eina_Bool entry_is_number(const char *str)
{
	int i;

	for(i = 0; str[i] != '\0'; i++) {
		if (isdigit(str[i]) == 0)
			return EINA_FALSE;
	}

	return EINA_TRUE;
}

static Eina_Bool _item_filter_cb(Evas_Object *obj, const char* item_label,
					void *item_data __UNUSED__,
					void *data)
{
	Evas_Object *entry;
	Compose *compose = data;
	const char *number;
	Eina_List *l;

	/* Did the user entered a phone number? */
	if (entry_is_number(item_label)) {
		EINA_LIST_FOREACH(compose->composing_numbers, l, number) {
			DBG("current= %s, number= %s", number, item_label);
			if (strcmp(number, item_label) == 0) {
				entry = elm_multibuttonentry_entry_get(obj);
				elm_object_part_text_set(entry, NULL, "");
				DBG("Filtered, number already in.");
				return EINA_FALSE;
			}
		}
		number =  eina_stringshare_add(item_label);
		compose->composing_numbers =
			eina_list_append(compose->composing_numbers, number);
		return EINA_TRUE;
	}

	return EINA_FALSE;
}

static void _item_c_genlist_del(void *data, Evas_Object *obj __UNUSED__)
{
	Contact_Genlist *c_genlist = data;

	DBG("Deleting item: %p name: %s number: %s", c_genlist,
		c_genlist->name, c_genlist->number);
	eina_stringshare_del(c_genlist->name);
	eina_stringshare_del(c_genlist->number);
	eina_stringshare_del(c_genlist->type);
	eina_stringshare_del(c_genlist->picture);
	free(c_genlist);
}

Evas_Object *compose_add(Evas_Object *parent)
{
	Compose *compose;
	Evas_Object *obj, *entry, *genlist, *mb_entry;
	Evas *e;

	compose = calloc(1, sizeof(Compose));
	EINA_SAFETY_ON_NULL_RETURN_VAL(compose, NULL);

	compose->layout = obj = layout_add(parent, "compose");
	EINA_SAFETY_ON_NULL_GOTO(obj, err_obj);

	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _on_del,
					compose);

	elm_object_signal_callback_add(obj, "clicked,*", "gui",
					_on_layout_clicked, compose);

	evas_object_data_set(obj, "compose.ctx", compose);

	entry = elm_entry_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(entry, err_entry);
	elm_object_part_content_set(obj, "elm.swallow.message", entry);
	evas_object_smart_callback_add(entry, "changed", _on_text_changed,
					compose);
	elm_object_style_set(entry, "compose");
	elm_entry_editable_set(entry, EINA_TRUE);
	elm_entry_scrollable_set(entry, EINA_TRUE);
	compose->entry_msg = entry;

	mb_entry = elm_multibuttonentry_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(mb_entry, err_entry);
	elm_object_part_content_set(obj, "elm.swallow.destination", mb_entry);
	elm_object_style_set(mb_entry, "compose");
	elm_object_text_set(mb_entry, "To: ");
	elm_object_part_text_set(mb_entry, "guide", "Tap to add recipient");
	compose->mb_entry = mb_entry;

	evas_object_smart_callback_add(mb_entry, "item,added",
					_on_item_added, compose);
	/* Don't let equal numbers be added */
	elm_multibuttonentry_item_filter_append(mb_entry, _item_filter_cb,
						compose);

	entry = elm_multibuttonentry_entry_get(mb_entry);
	EINA_SAFETY_ON_NULL_GOTO(entry, err_entry);
	evas_object_smart_callback_add(entry, "changed",
					_on_text_mb_entry_changed,
					compose);

	genlist = elm_genlist_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(genlist, err_genlist);
	elm_genlist_mode_set(genlist, ELM_LIST_COMPRESS);
	elm_object_style_set(genlist, "compose");
	elm_object_part_content_set(obj, "elm.swallow.genlist", genlist);
	compose->genlist = genlist;

	genlist = elm_genlist_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(genlist, err_genlist);
	elm_object_style_set(genlist, "compose");
	elm_object_part_content_set(obj, "elm.swallow.genlist.contacts",
					genlist);
	compose->genlist_contacts = genlist;
	elm_object_focus_allow_set(genlist, EINA_FALSE);

	elm_scroller_policy_set(compose->genlist,
				ELM_SCROLLER_POLICY_OFF,
				ELM_SCROLLER_POLICY_AUTO);
	elm_scroller_policy_set(compose->genlist_contacts,
				ELM_SCROLLER_POLICY_OFF,
				ELM_SCROLLER_POLICY_AUTO);

	compose->itc_inc = elm_genlist_item_class_new();
	EINA_SAFETY_ON_NULL_GOTO(compose->itc_inc, err_itc);
	compose->itc_inc->item_style = "messages-incoming";
	compose->itc_inc->func.text_get = _item_label_get;
	compose->itc_inc->func.content_get = _item_content_get;
	compose->itc_inc->func.state_get = NULL;
	compose->itc_inc->func.del = NULL;
	compose->itc_inc->decorate_all_item_style = "incoming-delete";
	compose->itc_inc->decorate_item_style = "incoming-delete";

	compose->itc_out = elm_genlist_item_class_new();
	EINA_SAFETY_ON_NULL_GOTO(compose->itc_out, err_itc_out);
	compose->itc_out->item_style = "messages-outgoing";
	compose->itc_out->func.text_get = _item_label_get;
	compose->itc_out->func.content_get = _item_content_get;
	compose->itc_out->func.state_get = _item_state_get;
	compose->itc_out->func.del = NULL;
	compose->itc_out->decorate_all_item_style = "outgoing-delete";
	compose->itc_out->decorate_item_style = "outgoing-delete";

	compose->itc_c_name = elm_genlist_item_class_new();
	EINA_SAFETY_ON_NULL_GOTO(compose->itc_c_name, err_names);
	compose->itc_c_name->item_style = "contacts-compose";
	compose->itc_c_name->func.text_get = _item_c_name_label_get;
	compose->itc_c_name->func.content_get = _item_c_name_content_get;
	compose->itc_c_name->func.state_get = NULL;
	compose->itc_c_name->func.del = _item_c_genlist_del;

	elm_object_part_text_set(compose->layout, "elm.text.name",
					"New Message");
	elm_object_signal_emit(compose->layout, "hide,genlist", "gui");
	compose->composing = EINA_TRUE;
	elm_object_signal_emit(compose->layout, "composing", "gui");

	incoming_sms = ofono_incoming_sms_cb_add(_incoming_sms_cb, compose);
	sent_sms = ofono_sent_sms_changed_cb_add(_sent_sms_cb, compose);

	evas_object_event_callback_add(obj, EVAS_CALLBACK_HIDE, _on_hide,
					compose);
	evas_object_event_callback_add(obj, EVAS_CALLBACK_SHOW, _on_show,
					compose);

	e = evas_object_evas_get(obj);
	evas_event_callback_add(e, EVAS_CALLBACK_CANVAS_FOCUS_OUT,
				_on_win_focus_out, compose);
	evas_event_callback_add(e, EVAS_CALLBACK_CANVAS_FOCUS_IN,
				_on_win_focus_in, compose);

	return obj;

err_names:
	elm_genlist_item_class_free(compose->itc_out);
err_itc_out:
	elm_genlist_item_class_free(compose->itc_inc);
err_itc:
	evas_object_del(genlist);
err_genlist:
	evas_object_del(entry);
err_entry:
	evas_object_del(obj);
err_obj:
	free(compose);
	return NULL;
}

void compose_set(Evas_Object *obj, const char *number, const char *message,
			Eina_Bool do_auto)
{
	Compose *compose;

	EINA_SAFETY_ON_NULL_RETURN(obj);
	EINA_SAFETY_ON_NULL_RETURN(number);
	EINA_SAFETY_ON_NULL_RETURN(message);

	compose = evas_object_data_get(obj, "compose.ctx");
	EINA_SAFETY_ON_NULL_RETURN(compose);

	ERR("TODO '%s' '%s' %d", number, message, do_auto);
}

void compose_messages_set(Evas_Object *obj, Eina_List *list, const char *number)
{
	Compose *compose;
	Message *msg;
	Eina_List *l;
	Elm_Genlist_Item_Class *itc;
	Elm_Object_Item *it = NULL;
	Contact_Info *c_info;

	EINA_SAFETY_ON_NULL_RETURN(obj);
	compose = evas_object_data_get(obj, "compose.ctx");
	EINA_SAFETY_ON_NULL_RETURN(compose);

	eina_stringshare_replace(&(compose->number), number);

	elm_genlist_clear(compose->genlist);

	EINA_LIST_FOREACH(list, l, msg) {
		Eina_Bool outgoing = message_outgoing_get(msg);

		if (outgoing)
			itc = compose->itc_out;
		else
			itc = compose->itc_inc;
		message_data_set(msg, compose);
		it = elm_genlist_item_append(compose->genlist, itc, msg, NULL,
						ELM_GENLIST_ITEM_NONE, NULL,
						NULL);
		message_object_item_set(msg, it);
		message_ref(msg);
	}
	if (it)
		elm_genlist_item_show(it, ELM_GENLIST_ITEM_SCROLLTO_IN);

	compose->current_thread = list;
	elm_object_signal_emit(compose->layout, "show,genlist", "gui");

	c_info = gui_contact_search(number, NULL);

	if (!c_info)
		elm_object_part_text_set(compose->layout, "elm.text.name",
						number);
	else
		elm_object_part_text_set(compose->layout, "elm.text.name",
					    contact_info_full_name_get(c_info));

	compose->composing = EINA_FALSE;
	_compose_timer_updater_start(compose);
	elm_object_signal_emit(compose->layout, "viewing", "gui");
}
