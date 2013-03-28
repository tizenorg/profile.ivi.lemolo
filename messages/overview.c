#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <Elementary.h>
#include <Eet.h>
#include <Eina.h>
#include <stdio.h>
#include <Ecore.h>

#include "overview.h"
#include "log.h"
#include "util.h"
#include "gui.h"
#include "contacts-ofono-efl.h"

#define ALL_MESSAGES "all_messages"

#ifndef EET_COMPRESSION_DEFAULT
#define EET_COMPRESSION_DEFAULT 1
#endif

/* This struct hold the content of the messages that will be showed
 * in the main screen
 */
typedef struct _Messages_List
{
	Eina_List *list;
	Eina_Bool dirty; /* not in eet */
	Ecore_Poller *save_poller; /* not in eet */
} Messages_List;

struct _Message
{
	const char *content;
	unsigned char outgoing;
	unsigned char state;
	long long time;
	const char *phone; /* phone number -  not in eet */
	int refcount; /* not in eet */
	void *data; /* not in eet */
	Elm_Object_Item *it; /* not in eet */
};

typedef struct _Overview
{
	Eet_Data_Descriptor *edd_msg_info;
	Eet_Data_Descriptor *edd_msg_list;
	Eet_Data_Descriptor *edd_msg;
	Eet_Data_Descriptor *edd_c_msg;
	Messages_List *messages;
	/* Pending conversations, not saved in eet yet */
	Messages_List *p_conversations;
	Evas_Object *layout, *genlist;
	char *msg_path, *base_dir, *msg_bkp;
	Ecore_Poller *updater;
	double last_update;
	Elm_Genlist_Item_Class *itc;
	Eina_Hash *pending_sms;
} Overview;

/* Messages showed in the main screen */
typedef struct _Message_Info
{
	const char *sender, *last_msg;
	long long time;
	int count;
	Overview *ov; /*not in eet */
	Elm_Object_Item *it; /* not in eet */
} Message_Info;

static OFono_Callback_List_Incoming_SMS_Node *incoming_sms = NULL;
static OFono_Callback_List_Sent_SMS_Node *sent_sms = NULL;

static void _overview_messages_save(Overview *ov);
static Message_Info *_message_info_search(Overview *ov, const char *sender);
static void _message_info_del(Message_Info *m_info);
static void _overview_messages_save(Overview *ov);
static void _conversation_eet_update_save(Overview *ov, Message *msg);
static void _message_free(Message *msg);

void message_ref(Message *msg)
{
	EINA_SAFETY_ON_NULL_RETURN(msg);
	msg->refcount++;
}

static Eina_Bool _overview_messages_save_do(void *data)
{
	Overview *ov = data;
	Eet_File *efile;
	EINA_SAFETY_ON_NULL_RETURN_VAL(ov->messages, ECORE_CALLBACK_DONE);

	ov->messages->save_poller = NULL;
	ov->messages->dirty = EINA_FALSE;

	ecore_file_unlink(ov->msg_bkp);
	ecore_file_mv(ov->msg_path, ov->msg_bkp);
	efile = eet_open(ov->msg_path, EET_FILE_MODE_WRITE);
	EINA_SAFETY_ON_NULL_GOTO(efile, failed);

	if (!(eet_data_write(efile,
				ov->edd_msg_list, ALL_MESSAGES,
				ov->messages, EET_COMPRESSION_DEFAULT)))
		ERR("Could in the messages log file");

	eet_close(efile);
	return ECORE_CALLBACK_DONE;

failed:
	ecore_file_mv(ov->msg_bkp, ov->msg_path);
	return ECORE_CALLBACK_DONE;
}

void overview_genlist_update(Evas_Object *obj, Message *msg,
				const char *contact)
{
	Overview *ov;
	Message_Info *m_info;

	EINA_SAFETY_ON_NULL_RETURN(obj);
	ov = evas_object_data_get(obj, "overview.ctx");
	EINA_SAFETY_ON_NULL_RETURN(ov);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	m_info = _message_info_search(ov, contact);
	EINA_SAFETY_ON_NULL_RETURN(m_info);

	m_info->time = msg->time;
	eina_stringshare_replace(&m_info->last_msg, msg->content);
	_overview_messages_save(ov);
	elm_genlist_item_update(m_info->it);
}

void message_object_item_set(Message *msg, Elm_Object_Item *it)
{
	EINA_SAFETY_ON_NULL_RETURN(msg);
	msg->it = it;
}

Elm_Object_Item *message_object_item_get(const Message *msg)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, NULL);
	return msg->it;
}

void message_state_set(Message *msg, unsigned char state)
{
	EINA_SAFETY_ON_NULL_RETURN(msg);
	msg->state = state;
}

void *message_data_get(const Message *msg)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, NULL);
	return msg->data;
}

void message_data_set(Message *msg, void *data)
{
	EINA_SAFETY_ON_NULL_RETURN(msg);
	msg->data = data;
}

static Eina_Bool _message_is_equal(Message *m1, Message *m2)
{
	if (strcmp(m1->content, m2->content) != 0)
		return EINA_FALSE;

	if (m1->outgoing != m2->outgoing)
		return EINA_FALSE;

	if (m1->state != m2->state)
		return EINA_FALSE;

	if (m1->time != m2->time)
		return EINA_FALSE;

	return EINA_TRUE;
}

void overview_message_from_file_delete(Evas_Object *obj, Message *msg,
					const char *contact)
{
	Eet_File *efile;
	Overview *ov;
	char buf[PATH_MAX], bkp[PATH_MAX];
	Messages_List *messages = NULL;
	Eina_List *l;
	Message *msg_aux;

	EINA_SAFETY_ON_NULL_RETURN(obj);
	ov = evas_object_data_get(obj, "overview.ctx");
	EINA_SAFETY_ON_NULL_RETURN(ov);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	snprintf(buf, sizeof(buf), "%s/%s.eet", ov->base_dir, contact);
	snprintf(bkp, sizeof(bkp), "%s/%s.eet.bkp", ov->base_dir, contact);

	efile = eet_open(buf, EET_FILE_MODE_READ_WRITE);

	if (efile) {
		messages = eet_data_read(efile, ov->edd_c_msg, ALL_MESSAGES);
	}

	if (!messages) {
		efile = eet_open(bkp, EET_FILE_MODE_READ_WRITE);
		if (efile) {
			messages = eet_data_read(efile, ov->edd_c_msg,
							ALL_MESSAGES);
		}
	}

	if (!messages)
		return;

	EINA_LIST_FOREACH(messages->list, l, msg_aux) {
		if (_message_is_equal(msg, msg_aux)) {
			_message_free(msg_aux);
			messages->list = eina_list_remove_list(messages->list,
								l);
			break;
		}
	}
	Message_Info *m_info = _message_info_search(ov, contact);
	EINA_SAFETY_ON_NULL_RETURN(m_info);
	m_info->count--;
	if (messages->list && eina_list_count(messages->list) != 0) {
		eet_data_write(efile, ov->edd_c_msg, ALL_MESSAGES, messages,
				EET_COMPRESSION_DEFAULT);
		eet_close(efile);
	} else {
		eet_close(efile);
		_message_info_del(m_info);
	}

	EINA_LIST_FREE(messages->list, msg_aux)
		_message_free(msg_aux);
	free(messages);
}

unsigned char message_state_get(const Message *msg)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, '\0');
	return msg->state;
}

const char * message_content_get(const Message *msg)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, NULL);
	return msg->content;
}

const char * message_phone_get(const Message *msg)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, NULL);
	return msg->phone;
}

unsigned char message_outgoing_get(const Message *msg)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, '\0');
	return msg->outgoing;
}

long long message_time_get(const Message *msg)
{

	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, -1);
	return msg->time;
}

static Message_Info *_message_info_search(Overview *ov, const char *sender)
{
	Eina_List *l;
	Message_Info *m_info;

	EINA_SAFETY_ON_NULL_RETURN_VAL(ov->messages, NULL);

	EINA_LIST_FOREACH(ov->messages->list, l, m_info) {
		if (strcmp(sender, m_info->sender) == 0)
			return m_info;
	}

	return NULL;
}

static void _message_free(Message *msg)
{
	DBG("MSG=%p", msg);
	eina_stringshare_del(msg->content);
	eina_stringshare_del(msg->phone);
	free(msg);
}

void message_del(Message *msg)
{

	EINA_SAFETY_ON_NULL_RETURN(msg);
	DBG("MSG=%p, refcount=%d", msg, msg->refcount);
	EINA_SAFETY_ON_TRUE_RETURN(msg->refcount <= 0);
	msg->refcount--;

	if (msg->refcount == 0)
		_message_free(msg);
}

static void _message_info_free(Message_Info *m_info)
{
	eina_stringshare_del(m_info->sender);
	eina_stringshare_del(m_info->last_msg);
	free(m_info);
}

static void _messages_file_delete(char *dir, const char *sender)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s.eet", dir, sender);
	ecore_file_unlink(path);
	snprintf(path, sizeof(path), "%s/%s.eet.bkp", dir, sender);
	ecore_file_unlink(path);
}

static void _message_info_del(Message_Info *m_info)
{
	Overview *ctx = m_info->ov;
	Eina_List *l, *next;
	Message *msg;

	EINA_SAFETY_ON_NULL_RETURN(ctx);

	elm_object_item_del(m_info->it);

	ctx->messages->list = eina_list_remove(ctx->messages->list, m_info);
	ctx->messages->dirty = EINA_TRUE;

	/* Remove unsaved SMSs */
	EINA_LIST_FOREACH_SAFE(ctx->p_conversations->list, l, next, msg) {
		if (msg->phone == m_info->sender) {
			ctx->p_conversations->list =
				eina_list_remove(ctx->p_conversations->list,
							msg);
			message_del(msg);
		}
	}

	_messages_file_delete(ctx->base_dir, m_info->sender);
	_overview_messages_save(ctx);

	if ((!ctx->messages->list) && (ctx->updater)) {
		ecore_poller_del(ctx->updater);
		ctx->updater = NULL;
	}

	_message_info_free(m_info);
}

static void _on_del(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Overview *ov = data;
	Message_Info *m_info;
	Message *msg;

	ofono_incoming_sms_cb_del(incoming_sms);
	ofono_sent_sms_changed_cb_del(sent_sms);

	if (ov->messages->save_poller)
		ecore_poller_del(ov->messages->save_poller);

	if (ov->p_conversations->save_poller)
		ecore_poller_del(ov->p_conversations->save_poller);

	if (ov->messages->dirty)
		_overview_messages_save_do(ov);

	eina_hash_free(ov->pending_sms);

	if (!ov->p_conversations->dirty) {
		EINA_LIST_FREE(ov->p_conversations->list, msg)
			message_del(msg);
	} else {
		EINA_LIST_FREE(ov->p_conversations->list, msg) {
			_conversation_eet_update_save(ov, msg);
			message_del(msg);
		}
	}

	EINA_LIST_FREE(ov->messages->list, m_info)
		_message_info_free(m_info);

	eet_data_descriptor_free(ov->edd_msg_info);
	eet_data_descriptor_free(ov->edd_msg_list);
	eet_data_descriptor_free(ov->edd_msg);
	eet_data_descriptor_free(ov->edd_c_msg);

	elm_genlist_item_class_free(ov->itc);
	free(ov->messages);
	free(ov->msg_path);
	free(ov->base_dir);
	free(ov->msg_bkp);
	free(ov->p_conversations);
	free(ov);

	eet_shutdown();
	ecore_file_shutdown();
}

static void _eet_descriptors_init(Eet_Data_Descriptor **messages,
					Eet_Data_Descriptor **msg_info,
					Eet_Data_Descriptor **edd_msg,
					Eet_Data_Descriptor **edd_c_msg)
{
	Eet_Data_Descriptor_Class eddc;
	Eet_Data_Descriptor_Class eddcM;

	EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Message_Info);
	*msg_info = eet_data_descriptor_stream_new(&eddc);

	EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Messages_List);
	*messages = eet_data_descriptor_stream_new(&eddc);

	EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddcM, Message);
	*edd_msg = eet_data_descriptor_stream_new(&eddcM);

	EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddcM, Messages_List);
	*edd_c_msg = eet_data_descriptor_stream_new(&eddcM);

	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd_msg, Message,
					"content", content, EET_T_STRING);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd_msg, Message,
					"time", time, EET_T_LONG_LONG);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd_msg, Message,
					"outgoing", outgoing, EET_T_UCHAR);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd_msg, Message,
					"state", state, EET_T_UCHAR);

	EET_DATA_DESCRIPTOR_ADD_BASIC(*msg_info, Message_Info,
					"sender", sender, EET_T_STRING);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*msg_info, Message_Info,
					"last_msg", last_msg, EET_T_STRING);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*msg_info, Message_Info,
					"time", time, EET_T_LONG_LONG);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*msg_info, Message_Info,
					"count", count, EET_T_INT);

	EET_DATA_DESCRIPTOR_ADD_LIST(*messages, Messages_List, "list", list,
					*msg_info);

	EET_DATA_DESCRIPTOR_ADD_LIST(*edd_c_msg, Messages_List, "list", list,
					*edd_msg);

}

static void _overview_messages_save(Overview *ov)
{
	if (ov->messages->save_poller)
		return;
	ov->messages->save_poller = ecore_poller_add(ECORE_POLLER_CORE, 32,
						     _overview_messages_save_do,
							ov);
}

static void _on_item_clicked(void *data, Evas_Object *obj __UNUSED__,
				void *event_info)
{
	Message_Info *m_info = data;
	Elm_Object_Item *it = event_info;
	char buf[PATH_MAX], bkp[PATH_MAX];
	Overview *ov = m_info->ov;
	Eet_File *efile;
	Messages_List *messages = NULL;

	elm_genlist_item_selected_set(it, EINA_FALSE);

	snprintf(buf, sizeof(buf), "%s/%s.eet", ov->base_dir, m_info->sender);
	snprintf(bkp, sizeof(bkp), "%s/%s.eet.bkp", ov->base_dir,
			m_info->sender);

	efile = eet_open(buf, EET_FILE_MODE_READ);

	if (efile) {
		messages = eet_data_read(efile, ov->edd_c_msg, ALL_MESSAGES);
		eet_close(efile);
	}

	if (!messages) {
		efile = eet_open(bkp, EET_FILE_MODE_READ);
		if (efile) {
			messages = eet_data_read(efile, ov->edd_c_msg,
							ALL_MESSAGES);
			eet_close(efile);
		}
	}
	if (messages) {
		gui_compose_messages_set(messages->list, m_info->sender);
		gui_compose_enter();
		/* Compose will free the messages->list for me */
		free(messages);
	} else
		INF("Could not read the messages list!");
}

static void _overview_messages_read(Overview *ov)
{
	Eet_File *efile;
	Messages_List *msg_list = NULL;
	Eina_List *l;
	Message_Info *m_info;
	Elm_Object_Item *it;

	efile = eet_open(ov->msg_path, EET_FILE_MODE_READ);

	if (efile) {
		msg_list = eet_data_read(efile, ov->edd_msg_list, ALL_MESSAGES);
		eet_close(efile);
	}

	if (!msg_list) {
		efile = eet_open(ov->msg_bkp, EET_FILE_MODE_READ);
		if (efile) {
			msg_list = eet_data_read(efile, ov->edd_msg_list,
						ALL_MESSAGES);
			eet_close(efile);
		}
	}

	if (!msg_list)
		msg_list = calloc(1, sizeof(Messages_List));

	ov->messages = msg_list;
	EINA_SAFETY_ON_NULL_RETURN(ov->messages);

	EINA_LIST_FOREACH(ov->messages->list, l, m_info) {
		it = elm_genlist_item_append(ov->genlist,
						ov->itc, m_info, NULL,
						ELM_GENLIST_ITEM_NONE,
						_on_item_clicked, m_info);
		m_info->ov = ov;
		m_info->it = it;
	}
}

static Eina_Bool _overview_time_updater(void *data)
{
	Overview *ctx = data;
	double now = ecore_loop_time_get();
	const double interval_threshold = 30.0;
	Elm_Object_Item *it;
	const long long update_threshold = time(NULL) - WEEK - DAY;

	if (!ctx->messages->list) {
		ctx->updater = NULL;
		return EINA_FALSE;
	}

	if (now - ctx->last_update < interval_threshold)
		return EINA_TRUE;
	ctx->last_update = now;

	it = elm_genlist_first_item_get(ctx->genlist);
	for (; it != NULL; it = elm_genlist_item_next_get(it)) {
		const Message_Info *m_info = elm_object_item_data_get(it);
		long long t = m_info->time;
		if (EINA_UNLIKELY(t == 0)) {
			t = m_info->time;
		}
		if (EINA_UNLIKELY(t < update_threshold))
			break;
		elm_genlist_item_update(it);
	}

	return EINA_TRUE;
}

static void _overview_timer_updater_start(Overview *ov)
{
	Evas *e = evas_object_evas_get(ov->layout);
	Eina_Bool win_focused = evas_focus_state_get(e);
	Eina_Bool obj_visible = evas_object_visible_get(ov->layout);

	DBG("poller %p, win_focused=%hhu, obj_visible=%hhu",
		ov->updater, win_focused, obj_visible);
	if (ov->updater)
		return;
	if (!ov->messages->list)
		return;
	if ((!win_focused) || (!obj_visible))
		return;

	DBG("start poller messages");
	/* ECORE_POLLER_CORE is 1/8th of second. */
	ov->updater = ecore_poller_add(ECORE_POLLER_CORE, 8 * 60,
						_overview_time_updater,
						ov);
	_overview_time_updater(ov);
}

static void _overview_time_updater_stop(Overview *ov)
{
	Evas *e = evas_object_evas_get(ov->layout);
	Eina_Bool win_focused = evas_focus_state_get(e);
	Eina_Bool obj_visible = evas_object_visible_get(ov->layout);

	DBG("poller %p, win_focused=%hhu, obj_visible=%hhu",
		ov->updater, win_focused, obj_visible);
	if (!ov->updater)
		return;
	if (win_focused && obj_visible)
		return;

	DBG("delete poller %p", ov->updater);
	ecore_poller_del(ov->updater);
	ov->updater = NULL;
}

static Message *_message_list_search(Eina_List *list, Message *msg)
{
	Eina_List *l;
	Message *m;

	EINA_LIST_FOREACH(list, l, m) {
		if ((m->content == msg->content &&
			m->time == msg->time) ||
			(msg->content == m->content && m->state != msg->state))
			return m;
	}

	return NULL;
}

static void _conversation_eet_update_save(Overview *ov, Message *msg)
{

	Eet_File *efile;
	char buf[PATH_MAX], bkp[PATH_MAX];
	Messages_List *messages = NULL;
	Message *msg_aux;

	snprintf(buf, sizeof(buf), "%s/%s.eet", ov->base_dir, msg->phone);
	snprintf(bkp, sizeof(bkp), "%s/%s.eet.bkp", ov->base_dir, msg->phone);

	efile = eet_open(buf, EET_FILE_MODE_READ);

	if (efile) {
		messages = eet_data_read(efile, ov->edd_c_msg, ALL_MESSAGES);
		eet_close(efile);
	}

	if (!messages) {
		efile = eet_open(bkp, EET_FILE_MODE_READ);
		if (efile) {
			messages = eet_data_read(efile, ov->edd_c_msg,
							ALL_MESSAGES);
			eet_close(efile);
		}
	}

	/* New file */
	if (!messages) {
		messages = calloc(1, sizeof(Messages_List));
		EINA_SAFETY_ON_NULL_RETURN(messages);
	}

	msg_aux = _message_list_search(messages->list, msg);

	if (!msg_aux) {
		messages->list = eina_list_append(messages->list, msg);
		msg->refcount++;
	} else
		msg_aux->state = msg->state;

	ecore_file_unlink(bkp);
	ecore_file_mv(buf, bkp);

	efile = eet_open(buf, EET_FILE_MODE_WRITE);
	EINA_SAFETY_ON_NULL_RETURN(efile);

	eet_data_write(efile, ov->edd_c_msg, ALL_MESSAGES, messages,
			EET_COMPRESSION_DEFAULT);

	eet_close(efile);
	EINA_LIST_FREE(messages->list, msg_aux) {
		if (msg_aux == msg)
			message_del(msg);
		else /* messages read from eet have no refcount */
			_message_free(msg_aux);
	}
	free(messages);
}

static Eina_Bool _conversation_eet_update_do(void *data)
{
	Overview *ov = data;
	Message *msg;

	ov->p_conversations->dirty = EINA_FALSE;
	ov->p_conversations->save_poller = NULL;

	EINA_LIST_FREE(ov->p_conversations->list, msg) {
		_conversation_eet_update_save(ov, msg);
		message_del(msg);
	}
	ov->p_conversations->list = NULL;
	return ECORE_CALLBACK_DONE;
}

static void _conversation_eet_update(Overview *ov)
{
	if (ov->p_conversations->save_poller)
		return;

	ov->p_conversations->save_poller = ecore_poller_add(ECORE_POLLER_CORE,
								32,
								_conversation_eet_update_do,
								ov);
}

static void _message_info_genlist_update(Overview *ov, time_t timestamp,
						const char *sender,
						const char *message)
{
	Message_Info *m_info;
	Elm_Object_Item *it;

	m_info = _message_info_search(ov, sender);

	if (!m_info) {
		m_info = calloc(1, sizeof(Message_Info));
		EINA_SAFETY_ON_NULL_RETURN(m_info);
		m_info->sender = eina_stringshare_add(sender);
		ov->messages->list = eina_list_prepend(ov->messages->list,
							m_info);
		m_info->ov = ov;
	} else {
		ov->messages->list = eina_list_remove(ov->messages->list,
							m_info);
		ov->messages->list = eina_list_prepend(ov->messages->list,
							m_info);
		if (m_info->it)
			elm_object_item_del(m_info->it);
	}

	m_info->count++;
	m_info->time = timestamp;
	eina_stringshare_replace(&m_info->last_msg, message);
	ov->messages->dirty = EINA_TRUE;

	it = elm_genlist_item_prepend(ov->genlist,
					ov->itc, m_info, NULL,
					ELM_GENLIST_ITEM_NONE,
					_on_item_clicked, m_info);
	m_info->it = it;

	elm_genlist_item_show(m_info->it, ELM_GENLIST_ITEM_SCROLLTO_TOP);
	_overview_timer_updater_start(ov);
	_overview_messages_save(ov);
}

Message *message_new(time_t timestamp, const char *content,
			Eina_Bool outgoing, OFono_Sent_SMS_State state)
{
	Message *msg;

	msg = calloc(1, sizeof(Message));
	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, NULL);
	msg->time = timestamp;
	msg->outgoing = outgoing;
	msg->content = eina_stringshare_add(content);
	msg->state = state;
	msg->refcount++;
	return msg;
}

static void _incoming_sms_cb(void *data, unsigned int sms_class,
				time_t timestamp, const char *sender,
				const char *message)
{
	Overview *ov = data;
	Message *msg;

	/* Users can only send class 1. This is OFono/GSM detail */
	if (sms_class != 1)
		return;

	msg = message_new(timestamp, message, EINA_FALSE,
				OFONO_SENT_SMS_STATE_SENT);
	EINA_SAFETY_ON_NULL_RETURN(msg);
	msg->phone = eina_stringshare_add(sender);

	_message_info_genlist_update(ov, timestamp, sender, message);

	ov->p_conversations->list = eina_list_append(ov->p_conversations->list,
							msg);
	ov->p_conversations->dirty = EINA_TRUE;
	_conversation_eet_update(ov);
}

static void _on_show(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Overview *ov = data;
	DBG("Overview became visible");
	_overview_timer_updater_start(ov);
}

static void _on_win_focus_in(void *data, Evas *e __UNUSED__,
				void *event_info __UNUSED__)
{
	Overview *ov = data;
	DBG("window is focused");
	_overview_timer_updater_start(ov);
}

static void _on_win_focus_out(void *data, Evas *e __UNUSED__,
				void *event_info __UNUSED__)
{
	Overview *ov = data;
	DBG("window is unfocused");
	_overview_time_updater_stop(ov);
}

static void _on_hide(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Overview *ov = data;
	DBG("Overview became hidden");
	_overview_time_updater_stop(ov);
}

static char *_item_label_get(void *data, Evas_Object *obj __UNUSED__,
				const char *part)
{
	Message_Info *m_info = data;

	if (strncmp(part, "elm.text.", strlen("elm.text.")))
		return NULL;
	part += strlen("elm.text.");

	if (strcmp(part, "name") == 0) {
		Contact_Info *c_info = gui_contact_search(m_info->sender, NULL);

		if (!c_info)
			return strdup(m_info->sender);
		else
			return strdup(contact_info_full_name_get(c_info));
	}

	if (strcmp(part, "content") == 0)
		return elm_entry_utf8_to_markup(m_info->last_msg);

	if (strcmp(part, "time") == 0) {
		time_t time_msg = m_info->time;
		return date_short_format(time_msg);
	}

	ERR("Unexpected text part: %s", part);
	return NULL;
}

static void _on_more_clicked(void *data __UNUSED__, Evas_Object *obj __UNUSED__,
				const char *emission __UNUSED__,
				const char *source __UNUSED__)
{
	DBG("TODO - ON MORE CLICKED");
}

static void _on_del_clicked(void *data, Evas_Object *obj __UNUSED__,
				void *event_info __UNUSED__)
{
	Message_Info *m_info = data;
	_message_info_del(m_info);
}

static Evas_Object *_item_content_get(void *data, Evas_Object *obj,
				const char *part)
{
	Evas_Object *btn = NULL;
	Message_Info *msg = data;

	if (strncmp(part, "elm.swallow.", strlen("elm.swallow.")))
		return NULL;
	part += strlen("elm.swallow.");

	if (strcmp(part, "more") == 0) {
		/* We can use the same button here */
		btn = layout_add(obj, "history/img");
		EINA_SAFETY_ON_NULL_RETURN_VAL(btn, NULL);
		elm_object_signal_callback_add(btn, "clicked,more", "gui",
						_on_more_clicked, msg);
	} else if (strcmp(part, "delete") == 0) {
		btn = elm_button_add(obj);
		EINA_SAFETY_ON_NULL_RETURN_VAL(btn, NULL);
		elm_object_style_set(btn, "history-delete");
		elm_object_text_set(btn, "delete");
		evas_object_smart_callback_add(btn, "clicked", _on_del_clicked,
						msg);
		evas_object_propagate_events_set(btn, EINA_FALSE);
	}
	else
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

void _overview_messages_clear(Overview *ov)
{
	Message_Info *m_info;
	Eina_List *l, *l_next;

	EINA_LIST_FOREACH_SAFE(ov->messages->list, l, l_next, m_info)
		_message_info_del(m_info);
}

static void _on_layout_clicked(void *data, Evas_Object *o,
			const char *emission, const char *source __UNUSED__)
{
	Overview *ov = data;
	DBG("signal: %s", emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "clicked,"));
	emission += strlen("clicked,");

	if (strcmp(emission, "edit") == 0) {
		elm_object_signal_emit(o, "toggle,on,edit", "gui");
		elm_genlist_decorate_mode_set(ov->genlist, EINA_TRUE);
	} else if (strcmp(emission, "compose") == 0) {
		elm_object_signal_emit(o, "toggle,on,compose", "gui");
		elm_object_signal_emit(o, "toggle,off,view", "gui");
		gui_compose_enter();
		elm_object_signal_emit(o, "toggle,off,compose", "gui");
		elm_object_signal_emit(o, "toggle,on,view", "gui");
	} else if (strcmp(emission, "view") == 0) {
		elm_object_signal_emit(o, "toggle,off,compose", "gui");
		elm_object_signal_emit(o, "toggle,on,view", "gui");
	} else if (strcmp(emission, "edit,done") == 0) {
		elm_object_signal_emit(o, "toggle,off,edit", "gui");
		elm_genlist_decorate_mode_set(ov->genlist, EINA_FALSE);
	} else if (strcmp(emission, "clear") == 0) {
		_overview_messages_clear(ov);
		elm_object_signal_emit(o, "toggle,off,edit", "gui");
	} else
		ERR("Unkown emission: %s", emission);
}

static void _sent_sms_cb(void *data, OFono_Error error, OFono_Sent_SMS *sms)
{
	Overview *ov = data;
	Message *msg;
	OFono_Sent_SMS_State state;
	time_t timestamp;
	const char *dest, *message;

	if (error != OFONO_ERROR_NONE) {
		ERR("OFono error - Sending a SMS");
		return;
	}

	state = ofono_sent_sms_state_get(sms);
	dest = ofono_sent_sms_destination_get(sms);
	message = ofono_sent_sms_message_get(sms);
	msg = eina_hash_find(ov->pending_sms, sms);
	timestamp = ofono_sent_sms_timestamp_get(sms);

	DBG("SMS Sent to: %s, message: %s, time: %ld", dest, message,
		timestamp);
	/* New SMS */
	if (!msg) {
		msg = message_new(timestamp, message, EINA_TRUE, state);
		EINA_SAFETY_ON_NULL_RETURN(msg);
		msg->phone = eina_stringshare_add(dest);

		_message_info_genlist_update(ov, timestamp, dest, message);
	} else
		msg->state = state;

	if (state == OFONO_SENT_SMS_STATE_FAILED ||
		state == OFONO_SENT_SMS_STATE_SENT)
		eina_hash_del_by_key(ov->pending_sms, sms);
	else if (state == OFONO_SENT_SMS_STATE_PENDING) {
		msg->refcount++;
		eina_hash_add(ov->pending_sms, sms, msg);
		ov->p_conversations->list = eina_list_append(ov->p_conversations->list,
								msg);
		DBG("Added to pending conversations - msg:%p - to: %s - msg: %s",
			msg, dest, message);
		ov->p_conversations->dirty = EINA_TRUE;
	}

	_conversation_eet_update(ov);
}

Evas_Object *overview_add(Evas_Object *parent)
{
	Evas_Object *obj, *genlist;
	Overview *ov;
	const char *config_path;
	int r;
	Evas *e;
	Elm_Genlist_Item_Class *itc;

	eet_init();
	ecore_file_init();

	ov = calloc(1, sizeof(Overview));
	EINA_SAFETY_ON_NULL_RETURN_VAL(ov, NULL);

	ov->p_conversations = calloc(1, sizeof(Messages_List));
	EINA_SAFETY_ON_NULL_GOTO(ov->p_conversations, err_conversations);

	ov->layout = obj = layout_add(parent, "messages-overview");
	EINA_SAFETY_ON_NULL_GOTO(obj, err_obj);

	elm_object_signal_callback_add(obj, "clicked,*", "gui",
					_on_layout_clicked, ov);

	genlist = elm_genlist_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(genlist, err_genlist);
	elm_object_style_set(genlist, "messages-overview");
	elm_genlist_homogeneous_set(genlist, EINA_TRUE);

	elm_scroller_policy_set(genlist, ELM_SCROLLER_POLICY_OFF,
				ELM_SCROLLER_POLICY_AUTO);

	itc = elm_genlist_item_class_new();
	EINA_SAFETY_ON_NULL_GOTO(itc, err_itc);
	itc->item_style = "messages-overview";
	itc->func.text_get = _item_label_get;
	itc->func.content_get = _item_content_get;
	itc->func.state_get = NULL;
	itc->func.del = NULL;
	itc->decorate_all_item_style = "messages-overview-delete";
	itc->decorate_item_style = "messages-overview-delete";
	ov->genlist = genlist;
	ov->itc = itc;

	evas_object_smart_callback_add(genlist, "drag,start,right",
					_on_list_slide_enter, ov);
	evas_object_smart_callback_add(genlist, "drag,start,left",
					_on_list_slide_cancel, ov);
	evas_object_smart_callback_add(genlist, "drag,start,down",
					_on_list_slide_cancel, ov);
	evas_object_smart_callback_add(genlist, "drag,start,up",
					_on_list_slide_cancel, ov);

	elm_object_part_content_set(obj, "elm.swallow.genlist", genlist);

	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _on_del,
					ov);
	evas_object_event_callback_add(obj, EVAS_CALLBACK_HIDE, _on_hide,
					ov);
	evas_object_event_callback_add(obj, EVAS_CALLBACK_SHOW, _on_show,
					ov);

	e = evas_object_evas_get(obj);
	evas_event_callback_add(e, EVAS_CALLBACK_CANVAS_FOCUS_OUT,
				_on_win_focus_out, ov);
	evas_event_callback_add(e, EVAS_CALLBACK_CANVAS_FOCUS_IN,
				_on_win_focus_in, ov);

	evas_object_data_set(obj, "overview.ctx", ov);

	config_path = efreet_config_home_get();

	r = asprintf(&ov->base_dir, "%s/%s/messages", config_path,
			PACKAGE_NAME);

	if (r < 0)
		goto err_base_dir;

	ecore_file_mkpath(ov->base_dir);

	r = asprintf(&ov->msg_path,  "%s/%s/messages/messages.eet", config_path,
			PACKAGE_NAME);

	if (r < 0)
		goto err_path;

	r = asprintf(&ov->msg_bkp,  "%s/%s/messages/messages.eet.bkp",
			config_path, PACKAGE_NAME);

	if (r < 0)
		goto err_bkp;

	_eet_descriptors_init(&ov->edd_msg_list, &ov->edd_msg_info,
				&ov->edd_msg, &ov->edd_c_msg);
	_overview_messages_read(ov);

	ov->pending_sms = eina_hash_pointer_new(EINA_FREE_CB(message_del));
	EINA_SAFETY_ON_NULL_GOTO(ov->pending_sms, err_hash);

	incoming_sms = ofono_incoming_sms_cb_add(_incoming_sms_cb, ov);
	sent_sms = ofono_sent_sms_changed_cb_add(_sent_sms_cb, ov);
	elm_object_signal_emit(ov->layout, "toggle,on,view", "gui");

	return obj;

err_hash:
	free(ov->msg_bkp);
err_bkp:
	free(ov->msg_path);
err_path:
	free(ov->base_dir);
err_base_dir:
	elm_genlist_item_class_free(itc);
err_itc:
	evas_object_del(genlist);
err_genlist:
	evas_object_del(obj);
err_obj:
	free(ov->p_conversations);
err_conversations:
	free(ov);
	ecore_file_shutdown();
	eet_shutdown();
	return NULL;
}

void overview_all_contact_messages_clear(Evas_Object *obj, const char *contact)
{
	Overview *ov;
	Message_Info *m_info;

	EINA_SAFETY_ON_NULL_RETURN(obj);
	ov = evas_object_data_get(obj, "overview.ctx");
	EINA_SAFETY_ON_NULL_RETURN(ov);

	m_info = _message_info_search(ov, contact);
	EINA_SAFETY_ON_NULL_RETURN(m_info);
	_message_info_del(m_info);
}
