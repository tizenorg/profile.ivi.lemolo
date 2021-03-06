#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "ofono.h"
#include "log.h"

#include <time.h>

typedef struct _OFono_Modem OFono_Modem;
typedef struct _OFono_Bus_Object OFono_Bus_Object;

static const char bus_name[] = "org.ofono";

static const char *known_modem_types[] = {"hfp", "sap", "hardware", NULL};

static E_DBus_Connection *bus_conn = NULL;
static char *bus_id = NULL;
static Eina_Hash *modems = NULL;
static OFono_Modem *modem_selected = NULL;
static const char *modem_path_wanted = NULL;
static unsigned int modem_api_mask = 0;
static Eina_List *modem_types = NULL;
static E_DBus_Signal_Handler *sig_modem_added = NULL;
static E_DBus_Signal_Handler *sig_modem_removed = NULL;
static E_DBus_Signal_Handler *sig_modem_prop_changed = NULL;
static DBusPendingCall *pc_get_modems = NULL;

static void _ofono_call_volume_properties_get(OFono_Modem *m);
static void _ofono_msg_waiting_properties_get(OFono_Modem *m);
static void _ofono_suppl_serv_properties_get(OFono_Modem *m);
static void _ofono_msg_properties_get(OFono_Modem *m);

static OFono_Pending *_ofono_simple_do(OFono_API api, const char *method,
					OFono_Simple_Cb cb, const void *data);

struct _OFono_Callback_List_Modem_Node
{
	EINA_INLIST;
	void (*cb)(void *data);
	const void *cb_data;
};

struct _OFono_Callback_List_Call_Node
{
	EINA_INLIST;
	void (*cb)(void *data, OFono_Call *call);
	const void *cb_data;
};

struct _OFono_Callback_List_Call_Disconnected_Node
{
	EINA_INLIST;
	void (*cb)(void *data, OFono_Call *call, const char *reason);
	const void *cb_data;
};

struct _OFono_Callback_List_USSD_Notify_Node
{
	EINA_INLIST;
	void (*cb)(void *data, Eina_Bool needs_reply, const char *msg);
	const void *cb_data;
};

struct _OFono_Callback_List_Sent_SMS_Node
{
	EINA_INLIST;
	OFono_Sent_SMS_Cb cb;
	const void *cb_data;
};

struct _OFono_Callback_List_Incoming_SMS_Node
{
	EINA_INLIST;
	OFono_Incoming_SMS_Cb cb;
	const void *cb_data;
};

static Eina_Inlist *cbs_modem_changed = NULL;
static Eina_Inlist *cbs_modem_connected = NULL;
static Eina_Inlist *cbs_modem_disconnected = NULL;
static Eina_Inlist *cbs_ussd_notify = NULL;

static Eina_Inlist *cbs_call_changed = NULL;
static Eina_Inlist *cbs_call_added = NULL;
static Eina_Inlist *cbs_call_disconnected = NULL;
static Eina_Inlist *cbs_call_removed = NULL;

static Eina_Inlist *cbs_sent_sms_changed = NULL;
static Eina_Inlist *cbs_incoming_sms = NULL;

#define OFONO_SERVICE			"org.ofono"

#define OFONO_PREFIX			OFONO_SERVICE "."
#define OFONO_PREFIX_ERROR		OFONO_SERVICE ".Error."
#define OFONO_MODEM_IFACE		"Modem"
#define OFONO_MANAGER_IFACE		"Manager"
#define OFONO_SIM_IFACE			"SimManager"
#define OFONO_NETREG_IFACE		"NetworkRegistration"
#define OFONO_VOICE_IFACE		"VoiceCallManager"
#define OFONO_MSG_IFACE			"MessageManager"
#define OFONO_MSG_WAITING_IFACE		"MessageWaiting"
#define OFONO_SMART_MSG_IFACE		"SmartMessaging"
#define OFONO_STK_IFACE			"SimToolkit"

#define OFONO_CALL_FW_IFACE		"CallForwarding"
#define OFONO_CALL_VOL_IFACE		"CallVolume"
#define OFONO_CALL_METER_IFACE		"CallMeter"
#define OFONO_CALL_SET_IFACE		"CallSettings"
#define OFONO_CALL_BAR_IFACE		"CallBarring"
#define OFONO_SUPPL_SERV_IFACE		"SupplementaryServices"
#define OFONO_TXT_TEL_IFACE		"TextTelephony"
#define OFONO_CELL_BROAD_IFACE		"CellBroadcast"
#define OFONO_CONNMAN_IFACE		"ConnectionManager"
#define OFONO_PUSH_NOTIF_IFACE		"PushNotification"
#define OFONO_PHONEBOOK_IFACE		"Phonebook"
#define OFONO_ASN_IFACE			"AssistedSatelliteNavigation"

static const struct API_Interface_Map {
	unsigned int bit;
	const char *name;
	size_t namelen;
} api_iface_map[] = {
#define MAP(bit, name) {bit, name, sizeof(name) - 1}
	MAP(OFONO_API_SIM, OFONO_SIM_IFACE),
	MAP(OFONO_API_NETREG, OFONO_NETREG_IFACE),
	MAP(OFONO_API_VOICE, OFONO_VOICE_IFACE),
	MAP(OFONO_API_MSG, OFONO_MSG_IFACE),
	MAP(OFONO_API_MSG_WAITING, OFONO_MSG_WAITING_IFACE),
	MAP(OFONO_API_SMART_MSG, OFONO_SMART_MSG_IFACE),
	MAP(OFONO_API_STK, OFONO_STK_IFACE),
	MAP(OFONO_API_CALL_FW, OFONO_CALL_FW_IFACE),
	MAP(OFONO_API_CALL_VOL, OFONO_CALL_VOL_IFACE),
	MAP(OFONO_API_CALL_METER, OFONO_CALL_METER_IFACE),
	MAP(OFONO_API_CALL_SET, OFONO_CALL_SET_IFACE),
	MAP(OFONO_API_CALL_BAR, OFONO_CALL_BAR_IFACE),
	MAP(OFONO_API_SUPPL_SERV, OFONO_SUPPL_SERV_IFACE),
	MAP(OFONO_API_TXT_TEL, OFONO_TXT_TEL_IFACE),
	MAP(OFONO_API_CELL_BROAD, OFONO_CELL_BROAD_IFACE),
	MAP(OFONO_API_CONNMAN, OFONO_CONNMAN_IFACE),
	MAP(OFONO_API_PUSH_NOTIF, OFONO_PUSH_NOTIF_IFACE),
	MAP(OFONO_API_PHONEBOOK, OFONO_PHONEBOOK_IFACE),
	MAP(OFONO_API_ASN, OFONO_ASN_IFACE),
#undef MAP
	{0, NULL, 0}
};

static Eina_Bool _dbus_bool_get(DBusMessageIter *itr)
{
	dbus_bool_t val;
	dbus_message_iter_get_basic(itr, &val);
	return val;
}

static const struct Error_Map {
	OFono_Error id;
	const char *name;
	const char *message;
	size_t namelen;
} error_map[] = {
#define MAP(id, name, msg) {id, name, msg, sizeof(name) - 1}
	MAP(OFONO_ERROR_FAILED, "Failed", "Failed"),
	MAP(OFONO_ERROR_DOES_NOT_EXIST, "DoesNotExist", "Does not exist"),
	MAP(OFONO_ERROR_IN_PROGRESS, "InProgress", "Operation in progress"),
	MAP(OFONO_ERROR_IN_USE, "InUse", "Already in use"),
	MAP(OFONO_ERROR_INVALID_ARGS, "InvalidArguments", "Invalid arguments"),
	MAP(OFONO_ERROR_INVALID_FORMAT, "InvalidFormat", "Invalid format"),
	MAP(OFONO_ERROR_ACCESS_DENIED, "AccessDenied", "Access Denied"),
	MAP(OFONO_ERROR_ATTACH_IN_PROGRESS, "AttachInProgress",
		"Attach is already in progress"),
	MAP(OFONO_ERROR_INCORRECT_PASSWORD, "IncorrectPassword",
		"Incorrect password"),
	MAP(OFONO_ERROR_NOT_ACTIVE, "NotActive", "Not active"),
	MAP(OFONO_ERROR_NOT_ALLOWED, "NotAllowed", "Not allowed"),
	MAP(OFONO_ERROR_NOT_ATTACHED, "NotAttached", "Not attached"),
	MAP(OFONO_ERROR_NOT_AVAILABLE, "NotAvailable", "Not available"),
	MAP(OFONO_ERROR_NOT_FOUND, "NotFound", "Not found"),
	MAP(OFONO_ERROR_NOT_IMPLEMENTED, "NotImplemented", "Not implemented"),
	MAP(OFONO_ERROR_NOT_RECOGNIZED, "NotRecognized", "Not recognized"),
	MAP(OFONO_ERROR_NOT_REGISTERED, "NotRegistered", "Not registered"),
	MAP(OFONO_ERROR_NOT_SUPPORTED, "NotSupported", "Not supported"),
	MAP(OFONO_ERROR_SIM_NOT_READY, "SimNotReady", "SIM not ready"),
	MAP(OFONO_ERROR_STK, "SimToolkit", "SIM Toolkit Failed"),
	MAP(OFONO_ERROR_TIMEDOUT, "Timedout", "Timed out"),
#undef MAP
	{0, NULL, NULL, 0}
};

const char *ofono_error_message_get(OFono_Error e)
{
	const struct Error_Map *itr;

	if (e == OFONO_ERROR_NONE)
		return "No error";

	for (itr = error_map; itr->name != NULL; itr++)
		if (itr->id == e)
			return itr->message;

	return "Unknown error";
}

static OFono_Error _ofono_error_parse(const char *name)
{
	size_t namelen, prefixlen = sizeof(OFONO_PREFIX_ERROR) - 1;
	const struct Error_Map *itr;

	/* whenever interfaces are not there due modem being offline */
	if (strcmp(name, "org.freedesktop.DBus.Error.UnknownMethod") == 0)
		return OFONO_ERROR_OFFLINE;

	if (strncmp(name, OFONO_PREFIX_ERROR, prefixlen) != 0)
		return OFONO_ERROR_FAILED;

	name += prefixlen;
	namelen = strlen(name);
	for (itr = error_map; itr->name != NULL; itr++)
		if ((itr->namelen == namelen) &&
			(memcmp(name, itr->name, namelen) == 0))
			return itr->id;

	return OFONO_ERROR_FAILED;
}

typedef struct _OFono_Simple_Cb_Context
{
	OFono_Simple_Cb cb;
	const void *data;
} OFono_Simple_Cb_Context;

static void _ofono_simple_reply(void *data, DBusMessage *msg __UNUSED__,
				DBusError *err)
{
	OFono_Simple_Cb_Context *ctx = data;
	OFono_Error e = OFONO_ERROR_NONE;

	if (dbus_error_is_set(err)) {
		DBG("%s: %s", err->name, err->message);
		e = _ofono_error_parse(err->name);
	}

	if (ctx) {
		ctx->cb((void *)ctx->data, e);
		free(ctx);
	}
}

typedef struct _OFono_String_Cb_Context
{
	OFono_String_Cb cb;
	const void *data;
	const char *name;
	char *(*convert)(DBusMessage *msg);
} OFono_String_Cb_Context;

static void _ofono_string_reply(void *data, DBusMessage *msg, DBusError *err)
{
	OFono_String_Cb_Context *ctx = data;
	OFono_Error e = OFONO_ERROR_NONE;
	char *str = NULL;

	if (dbus_error_is_set(err)) {
		DBG("%s: %s", err->name, err->message);
		e = _ofono_error_parse(err->name);
	} else {
		str = ctx->convert(msg);
		if (!str)
			e = OFONO_ERROR_NOT_SUPPORTED;
	}

	if (ctx->cb)
		ctx->cb((void *)ctx->data, e, str);
	else
		DBG("%s %s", ctx->name, str);

	free(str);
	free(ctx);
}

struct _OFono_Pending
{
	EINA_INLIST;
	DBusPendingCall *pending;
	E_DBus_Method_Return_Cb cb;
	void *data;
	void *owner;
};

struct _OFono_Bus_Object
{
	const char *path;
	Eina_Inlist *dbus_pending; /* of OFono_Pending */
	Eina_List *dbus_signals; /* of E_DBus_Signal_Handler */
};

static void _notify_ofono_callbacks_call_list(Eina_Inlist *list,
						OFono_Call *call)
{
	OFono_Callback_List_Call_Node *node;

	EINA_INLIST_FOREACH(list, node)
		node->cb((void *) node->cb_data, call);
}

static void _notify_ofono_callbacks_call_disconnected_list(Eina_Inlist *list,
								OFono_Call *call,
								const char *reason)
{
	OFono_Callback_List_Call_Disconnected_Node *node;

	DBG("call=%p, reason=%s", call, reason);

	EINA_INLIST_FOREACH(list, node)
		node->cb((void *) node->cb_data, call, reason);
}

static void _notify_ofono_callbacks_ussd_notify_list(Eina_Inlist *list,
							Eina_Bool needs_reply,
							const char *msg)
{
	OFono_Callback_List_USSD_Notify_Node *node;

	DBG("needs_reply=%hhu, msg=%s", needs_reply, msg);

	EINA_INLIST_FOREACH(list, node)
		node->cb((void *) node->cb_data, needs_reply, msg);
}

static void _bus_object_free(OFono_Bus_Object *o)
{
	E_DBus_Signal_Handler *sh;

	eina_stringshare_del(o->path);

	while (o->dbus_pending) {
		ofono_pending_cancel(
			EINA_INLIST_CONTAINER_GET(o->dbus_pending,
							OFono_Pending));
	}

	EINA_LIST_FREE(o->dbus_signals, sh)
		e_dbus_signal_handler_del(bus_conn, sh);

	free(o);
}

static void _bus_object_message_send_reply(void *data, DBusMessage *reply,
						DBusError *err)
{
	OFono_Pending *p = data;
	OFono_Bus_Object *o = p->owner;

	if (p->cb)
		p->cb(p->data, reply, err);

	o->dbus_pending = eina_inlist_remove(o->dbus_pending,
						EINA_INLIST_GET(p));
	free(p);
}

static OFono_Pending *_bus_object_message_send(OFono_Bus_Object *o,
						DBusMessage *msg,
						E_DBus_Method_Return_Cb cb,
						void *data)
{
	OFono_Pending *p;
	EINA_SAFETY_ON_NULL_GOTO(o, error);
	EINA_SAFETY_ON_NULL_GOTO(msg, error);

	p = calloc(1, sizeof(OFono_Pending));
	EINA_SAFETY_ON_NULL_GOTO(p, error);

	p->owner = o;
	p->cb = cb;
	p->data = data;

	p->pending = e_dbus_message_send(
		bus_conn, msg, _bus_object_message_send_reply, -1, p);
	EINA_SAFETY_ON_NULL_GOTO(p->pending, error_send);

	o->dbus_pending = eina_inlist_append(o->dbus_pending,
						EINA_INLIST_GET(p));
	dbus_message_unref(msg);
	return p;

error_send:
	free(p);
error:
	if (cb) {
		DBusError err;
		dbus_error_init(&err);
		dbus_set_error(&err, "Failed", "call setup failed.");
		cb(data, NULL, &err);
	}
	dbus_message_unref(msg);
	return NULL;
}

void ofono_pending_cancel(OFono_Pending *p)
{
	OFono_Bus_Object *o;

	EINA_SAFETY_ON_NULL_RETURN(p);

	o = p->owner;
	o->dbus_pending = eina_inlist_remove(o->dbus_pending,
						EINA_INLIST_GET(p));

	if (p->cb) {
		DBusError err;

		dbus_error_init(&err);
		dbus_set_error(&err, "Canceled",
				"Pending method call was canceled.");
		p->cb(p->data, NULL, &err);
	}
	dbus_pending_call_cancel(p->pending);
	free(p);
}

static void _bus_object_signal_listen(OFono_Bus_Object *o, const char *iface,
					const char *name, E_DBus_Signal_Cb cb,
					void *data)
{
	E_DBus_Signal_Handler *sh = e_dbus_signal_handler_add(
		bus_conn, bus_id, o->path, iface, name, cb, data);
	EINA_SAFETY_ON_NULL_RETURN(sh);

	o->dbus_signals = eina_list_append(o->dbus_signals, sh);
}

typedef struct _OFono_Call_Cb_Context
{
	OFono_Call_Cb cb;
	OFono_Modem *modem;
	const void *data;
	const char *name;
} OFono_Call_Cb_Context;

struct _OFono_Call
{
	OFono_Bus_Object base;
	const char *line_id;
	const char *incoming_line;
	const char *name;
	double start_time;
	time_t full_start_time;
	OFono_Call_State state;
	Eina_Bool multiparty : 1;
	Eina_Bool emergency : 1;
	OFono_Call_Cb_Context *pending_dial;
};

typedef struct _OFono_Sent_SMS_Cb_Context
{
	OFono_Sent_SMS_Cb cb;
	OFono_Modem *modem;
	const void *data;
	const char *destination;
	const char *message;
} OFono_Sent_SMS_Cb_Context;

struct _OFono_Sent_SMS
{
	OFono_Bus_Object base;
	OFono_Sent_SMS_State state;
	OFono_Sent_SMS_Cb_Context *pending_send;
	const char *destination;
	const char *message;
	time_t timestamp;
};

struct _OFono_Modem
{
	OFono_Bus_Object base;
	const char *name;
	const char *serial;
	const char *voicemail_number;
	const char *serv_center_addr;
	const char *msg_bearer;
	const char *msg_alphabet;
	Eina_Hash *calls;
	Eina_Hash *sent_sms;
	unsigned int interfaces;
	unsigned char strength;
	unsigned char data_strength;
	unsigned char speaker_volume;
	unsigned char microphone_volume;
	unsigned char voicemail_count;
	OFono_USSD_State ussd_state;
	Eina_Bool ignored : 1;
	Eina_Bool powered : 1;
	Eina_Bool online : 1;
	Eina_Bool roaming : 1;
	Eina_Bool muted : 1;
	Eina_Bool voicemail_waiting : 1;
	Eina_Bool use_delivery_reports : 1;
};

static OFono_Call *_call_new(const char *path)
{
	OFono_Call *c = calloc(1, sizeof(OFono_Call));
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);

	c->base.path = eina_stringshare_add(path);
	EINA_SAFETY_ON_NULL_GOTO(c->base.path, error_path);

	c->start_time = -1.0;

	return c;

error_path:
	free(c);
	return NULL;
}

static void _call_free(OFono_Call *c)
{
	DBG("c=%p %s", c, c->base.path);

	_notify_ofono_callbacks_call_list(cbs_call_removed, c);

	eina_stringshare_del(c->line_id);
	eina_stringshare_del(c->incoming_line);
	eina_stringshare_del(c->name);

	_bus_object_free(&c->base);
}

static OFono_Call_State _call_state_parse(const char *str)
{
	if (strcmp(str, "active") == 0)
		return OFONO_CALL_STATE_ACTIVE;
	else if (strcmp(str, "held") == 0)
		return OFONO_CALL_STATE_HELD;
	else if (strcmp(str, "dialing") == 0)
		return OFONO_CALL_STATE_DIALING;
	else if (strcmp(str, "alerting") == 0)
		return OFONO_CALL_STATE_ALERTING;
	else if (strcmp(str, "incoming") == 0)
		return OFONO_CALL_STATE_INCOMING;
	else if (strcmp(str, "waiting") == 0)
		return OFONO_CALL_STATE_WAITING;
	else if (strcmp(str, "disconnected") == 0)
		return OFONO_CALL_STATE_DISCONNECTED;

	ERR("unknown call state: %s", str);
	return OFONO_CALL_STATE_DISCONNECTED;
}

static time_t _ofono_time_parse(const char *str)
{
	struct tm tm;
	time_t zonediff;

	memset(&tm, 0, sizeof(tm));

	strptime(str, "%Y-%m-%dT%H:%M:%S%z", &tm);
	zonediff = tm.tm_gmtoff; /* mktime resets it */

        return mktime(&tm) - zonediff - timezone;
}

static void _call_property_update(OFono_Call *c, const char *key,
					DBusMessageIter *value)
{
	if (strcmp(key, "LineIdentification") == 0) {
		const char *str;
		dbus_message_iter_get_basic(value, &str);
		DBG("%s LineIdentification %s", c->base.path, str);
		eina_stringshare_replace(&c->line_id, str);
	} else if (strcmp(key, "IncomingLine") == 0) {
		const char *str;
		dbus_message_iter_get_basic(value, &str);
		DBG("%s IncomingLine %s", c->base.path, str);
		eina_stringshare_replace(&c->incoming_line, str);
	} else if (strcmp(key, "State") == 0) {
		const char *str;
		OFono_Call_State state;
		dbus_message_iter_get_basic(value, &str);
		state = _call_state_parse(str);
		DBG("%s State %s (%d)", c->base.path, str, state);
		c->state = state;
		if (state == OFONO_CALL_STATE_ACTIVE) {
			if (c->start_time < 0.0)
				c->start_time = ecore_loop_time_get();
			if (c->full_start_time == 0)
				c->full_start_time = time(NULL);
		}
	} else if (strcmp(key, "Name") == 0) {
		const char *str;
		dbus_message_iter_get_basic(value, &str);
		DBG("%s Name %s", c->base.path, str);
		eina_stringshare_replace(&c->name, str);
	} else if (strcmp(key, "Multiparty") == 0) {
		dbus_bool_t v;
		dbus_message_iter_get_basic(value, &v);
		DBG("%s Multiparty %d", c->base.path, v);
		c->multiparty = v;
	} else if (strcmp(key, "Emergency") == 0) {
		dbus_bool_t v;
		dbus_message_iter_get_basic(value, &v);
		DBG("%s Emergency %d", c->base.path, v);
		c->emergency = v;
	} else if (strcmp(key, "StartTime") == 0) {
		const char *ts = NULL;
		time_t st, ut;
		double lt;
		dbus_message_iter_get_basic(value, &ts);

		st = _ofono_time_parse(ts);
		ut = time(NULL);
		lt = ecore_loop_time_get();
		c->start_time = st - ut + lt;
		c->full_start_time = st;
		DBG("%s StartTime %f (%s)", c->base.path, c->start_time, ts);
	} else
		DBG("%s %s (unused property)", c->base.path, key);
}

static void _call_property_changed(void *data, DBusMessage *msg)
{
	OFono_Call *c = data;
	DBusMessageIter iter, value;
	const char *key;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	DBG("path=%s", c->base.path);

	dbus_message_iter_get_basic(&iter, &key);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &value);
	_call_property_update(c, key, &value);

	_notify_ofono_callbacks_call_list(cbs_call_changed, c);
}

static void _call_disconnect_reason(void *data, DBusMessage *msg)
{
	OFono_Call *c = data;
	DBusError err;
	const char *reason;

	if (!msg) {
		ERR("No message");
		return;
	}

	DBG("path=%s", c->base.path);
	c->state = OFONO_CALL_STATE_DISCONNECTED;

	dbus_error_init(&err);
	if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &reason,
					DBUS_TYPE_INVALID)) {
		ERR("Could not get DisconnectReason arguments: %s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		return;
	}

	_notify_ofono_callbacks_call_disconnected_list(cbs_call_disconnected,
							c, reason);
}

static OFono_Call *_call_common_add(OFono_Modem *m, const char *path)
{
	OFono_Call *c = _call_new(path);
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	eina_hash_add(m->calls, c->base.path, c);

	_bus_object_signal_listen(&c->base,
					OFONO_PREFIX "VoiceCall",
					"DisconnectReason",
					_call_disconnect_reason, c);
	_bus_object_signal_listen(&c->base,
					OFONO_PREFIX "VoiceCall",
					"PropertyChanged",
					_call_property_changed, c);
	return c;
}

static OFono_Call *_call_pending_add(OFono_Modem *m, const char *path,
					OFono_Call_Cb_Context *ctx)
{
	OFono_Call *c;

	DBG("path=%s, ctx=%p", path, ctx);

	c = _call_common_add(m, path);
	c->pending_dial = ctx;
	return c;
}

static void _call_add(OFono_Modem *m, const char *path, DBusMessageIter *prop)
{
	OFono_Call *c;
	Eina_Bool needs_cb_added;

	DBG("path=%s, prop=%p", path, prop);

	c = eina_hash_find(m->calls, path);
	needs_cb_added = !c;
	if (c)
		DBG("Call already exists %p (%s)", c, path);
	else {
		c = _call_common_add(m, path);
		EINA_SAFETY_ON_NULL_RETURN(c);
	}

	for (; dbus_message_iter_get_arg_type(prop) == DBUS_TYPE_DICT_ENTRY;
			dbus_message_iter_next(prop)) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(prop, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		_call_property_update(c, key, &value);
	}

	if (c->pending_dial) {
		OFono_Call_Cb_Context *ctx = c->pending_dial;
		if (ctx->cb)
			ctx->cb((void *)ctx->data, OFONO_ERROR_NONE, c);
		free(ctx);
		c->pending_dial = NULL;
		needs_cb_added = EINA_TRUE;
	}

	if (needs_cb_added)
		_notify_ofono_callbacks_call_list(cbs_call_added, c);

	_notify_ofono_callbacks_call_list(cbs_call_changed, c);
}

static void _call_remove(OFono_Modem *m, const char *path)
{
	DBG("path=%s", path);
	eina_hash_del_by_key(m->calls, path);
}

static void _call_added(void *data, DBusMessage *msg)
{
	OFono_Modem *m = data;
	DBusMessageIter iter, properties;
	const char *path;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_message_iter_get_basic(&iter, &path);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &properties);

	_call_add(m, path, &properties);
}

static void _call_removed(void *data, DBusMessage *msg)
{
	OFono_Modem *m = data;
	DBusError err;
	const char *path;

	if (!msg) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_error_init(&err);
	if (!dbus_message_get_args(msg, &err, DBUS_TYPE_OBJECT_PATH,
					&path, NULL)) {
		ERR("Could not get CallRemoved arguments: %s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		return;
	}

	_call_remove(m, path);
}

static OFono_Modem *_modem_selected_get(void)
{
	OFono_Modem *found_path = NULL, *found_api = NULL, *m;
	unsigned int online = 0, powered = 0;
	Eina_Iterator *itr;

	if (modem_selected)
		return modem_selected;

	itr = eina_hash_iterator_data_new(modems);
	EINA_ITERATOR_FOREACH(itr, m) {
		if (m->ignored)
			continue;

		if (m->online)
			online++;
		if (m->powered)
			powered++;

		if ((modem_path_wanted) && (!found_path)) {
			DBG("m=%s, wanted=%s", m->base.path, modem_path_wanted);
			if (m->base.path == modem_path_wanted) {
				found_path = m;
				break;
			}
		}

		if ((!found_api) || ((!found_api->online) ||
					(!found_api->powered))) {
			DBG("m=%#x, mask=%#x, previous=%s "
				"(online=%d, powered=%d)",
				m->interfaces, modem_api_mask,
				found_api ? found_api->base.path : "",
				found_api ? found_api->online : 0,
				found_api ? found_api->powered : 0);
			if ((m->interfaces & modem_api_mask) == modem_api_mask)
				found_api = m;
		}
	}
	eina_iterator_free(itr);

	INF("found_path=%s, found_api=%s, wanted_path=%s, api_mask=%#x",
		found_path ? found_path->base.path : "",
		found_api ? found_api->base.path: "",
		modem_path_wanted ? modem_path_wanted : "",
		modem_api_mask);

	if (!powered)
		ERR("No modems powered! Run connman or test/enable-modem");
	if (!online)
		WRN("No modems online! Run connman or test/online-modem");

	modem_selected = found_path ? found_path : found_api;
	return modem_selected;
}

static OFono_Pending *_ofono_multiparty(const char *method,
					OFono_Simple_Cb cb, const void *data)
{

	OFono_Pending *p;
	DBusMessage *msg;
	OFono_Simple_Cb_Context *ctx = NULL;
	OFono_Modem *m = _modem_selected_get();

	EINA_SAFETY_ON_NULL_GOTO(m, error_no_message);

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error_no_message);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(
				bus_id, m->base.path,
				OFONO_PREFIX OFONO_VOICE_IFACE,
				method);

	if (!msg)
		goto error_no_message;

	if (!dbus_message_append_args(msg, DBUS_TYPE_INVALID))
		goto error_message_append;

	INF("%s()", method);
	p = _bus_object_message_send(&m->base, msg, _ofono_simple_reply, ctx);

	return p;

error_message_append:
	dbus_message_unref(msg);
error_no_message:
	if (cb)
		cb((void *)data, OFONO_ERROR_FAILED);
	free(ctx);
	return NULL;
}

OFono_Pending *ofono_call_hangup(OFono_Call *c, OFono_Simple_Cb cb,
					const void *data)
{
	OFono_Simple_Cb_Context *ctx = NULL;
	OFono_Pending *p;
	DBusMessage *msg;

	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(
		bus_id, c->base.path, OFONO_PREFIX "VoiceCall", "Hangup");
	if (!msg)
		goto error;

	INF("Hangup(%s)", c->base.path);
	p = _bus_object_message_send(&c->base, msg, _ofono_simple_reply, ctx);
	return p;

error:
	if (cb)
		cb((void *)data, OFONO_ERROR_FAILED);
	free(ctx);
	return NULL;
}

OFono_Pending *ofono_call_answer(OFono_Call *c, OFono_Simple_Cb cb,
					const void *data)
{
	OFono_Simple_Cb_Context *ctx = NULL;
	OFono_Pending *p;
	DBusMessage *msg;

	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(
		bus_id, c->base.path, OFONO_PREFIX "VoiceCall", "Answer");
	if (!msg)
		goto error;

	INF("Answer(%s)", c->base.path);
	p = _bus_object_message_send(&c->base, msg, _ofono_simple_reply, ctx);
	return p;

error:
	if (cb)
		cb((void *)data, OFONO_ERROR_FAILED);
	free(ctx);
	return NULL;
}

OFono_Call_State ofono_call_state_get(const OFono_Call *c)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, OFONO_CALL_STATE_DISCONNECTED);
	return c->state;
}

const char *ofono_call_name_get(const OFono_Call *c)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	return c->name;
}

const char *ofono_call_line_id_get(const OFono_Call *c)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	return c->line_id;
}

Eina_Bool ofono_call_multiparty_get(const OFono_Call *c)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, EINA_FALSE);
	return c->multiparty;
}

double ofono_call_start_time_get(const OFono_Call *c)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, -1.0);
	return c->start_time;
}

time_t ofono_call_full_start_time_get(const OFono_Call *c)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, 0);
	return c->full_start_time;
}

static void _ofono_calls_get_reply(void *data, DBusMessage *msg,
					DBusError *err)
{
	OFono_Modem *m = data;
	DBusMessageIter array, dict;

	if (!msg) {
		if (err)
			ERR("%s: %s", err->name, err->message);
		else
			ERR("No message");
		return;
	}

	eina_hash_free_buckets(m->calls);

	if (!dbus_message_iter_init(msg, &array)) {
		ERR("Could not get calls");
		return;
	}

	dbus_message_iter_recurse(&array, &dict);
	for (; dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_STRUCT;
			dbus_message_iter_next(&dict)) {
		DBusMessageIter value, properties;
		const char *path;

		dbus_message_iter_recurse(&dict, &value);
		dbus_message_iter_get_basic(&value, &path);

		dbus_message_iter_next(&value);
		dbus_message_iter_recurse(&value, &properties);

		_call_add(m, path, &properties);
	}
}

static void _modem_calls_load(OFono_Modem *m)
{
	DBusMessage *msg = dbus_message_new_method_call(
		bus_id, m->base.path, OFONO_PREFIX OFONO_VOICE_IFACE,
		"GetCalls");

	DBG("Get calls of %s", m->base.path);
	_bus_object_message_send(&m->base, msg, _ofono_calls_get_reply, m);
}

static OFono_Sent_SMS *_sent_sms_new(const char *path)
{
	OFono_Sent_SMS *sms = calloc(1, sizeof(OFono_Sent_SMS));
	EINA_SAFETY_ON_NULL_RETURN_VAL(sms, NULL);

	sms->base.path = eina_stringshare_add(path);
	EINA_SAFETY_ON_NULL_GOTO(sms->base.path, error_path);

	return sms;

error_path:
	free(sms);
	return NULL;
}

static void _sent_sms_free(OFono_Sent_SMS *sms)
{
	DBG("sms=%p %s", sms, sms->base.path);
	eina_stringshare_del(sms->destination);
	eina_stringshare_del(sms->message);
	_bus_object_free(&sms->base);
}

static OFono_Modem *_modem_new(const char *path)
{
	OFono_Modem *m = calloc(1, sizeof(OFono_Modem));
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, NULL);

	m->base.path = eina_stringshare_add(path);
	EINA_SAFETY_ON_NULL_GOTO(m->base.path, error_path);

	m->calls = eina_hash_string_small_new(EINA_FREE_CB(_call_free));
	EINA_SAFETY_ON_NULL_GOTO(m->calls, error_calls);

	m->sent_sms = eina_hash_string_small_new(EINA_FREE_CB(_sent_sms_free));
	EINA_SAFETY_ON_NULL_GOTO(m->sent_sms, error_sent_sms);

	return m;

error_sent_sms:
	eina_hash_free(m->calls);
error_calls:
	eina_stringshare_del(m->base.path);
error_path:
	free(m);
	return NULL;
}

static void _modem_free(OFono_Modem *m)
{
	DBG("m=%p %s", m, m->base.path);

	if (modem_selected == m)
		modem_selected = NULL;

	eina_stringshare_del(m->name);
	eina_stringshare_del(m->serial);
	eina_stringshare_del(m->voicemail_number);
	eina_stringshare_del(m->serv_center_addr);
	eina_stringshare_del(m->msg_bearer);
	eina_stringshare_del(m->msg_alphabet);

	eina_hash_free(m->calls);
	eina_hash_free(m->sent_sms);

	_bus_object_free(&m->base);
}


static void _call_volume_property_update(OFono_Modem *m, const char *prop_name,
						DBusMessageIter *iter)
{

	if (strcmp(prop_name, "Muted") == 0) {
		m->muted = _dbus_bool_get(iter);
		DBG("%s Muted %d", m->base.path, m->muted);
	} else if (strcmp(prop_name, "SpeakerVolume") == 0) {
		dbus_message_iter_get_basic(iter, &m->speaker_volume);
		DBG("%s Speaker Volume %hhu", m->base.path, m->speaker_volume);
	} else if (strcmp(prop_name, "MicrophoneVolume") == 0) {
		dbus_message_iter_get_basic(iter, &m->microphone_volume);
		DBG("%s Microphone Volume %hhu", m->base.path, m->speaker_volume);
	} else
		DBG("%s %s (unused property)", m->base.path, prop_name);
}

static void _msg_waiting_property_update(OFono_Modem *m, const char *prop_name,
						DBusMessageIter *iter)
{

	if (strcmp(prop_name, "VoicemailWaiting") == 0) {
		m->voicemail_waiting = _dbus_bool_get(iter);
		DBG("%s VoicemailWaiting %d",
			m->base.path, m->voicemail_waiting);
	} else if (strcmp(prop_name, "VoicemailMessageCount") == 0) {
		dbus_message_iter_get_basic(iter, &m->voicemail_count);
		DBG("%s VoicemailMessageCount %hhu",
			m->base.path, m->voicemail_count);
	} else if (strcmp(prop_name, "VoicemailMailboxNumber") == 0) {
		const char *s;
		dbus_message_iter_get_basic(iter, &s);
		eina_stringshare_replace(&(m->voicemail_number), s);
		DBG("%s VoicemailMailboxNumber %s",
			m->base.path, m->voicemail_number);
	} else
		DBG("%s %s (unused property)", m->base.path, prop_name);
}

static OFono_USSD_State _suppl_serv_state_parse(const char *s)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(s, OFONO_USSD_STATE_IDLE);
	if (strcmp(s, "idle") == 0)
		return OFONO_USSD_STATE_IDLE;
	else if (strcmp(s, "active") == 0)
		return OFONO_USSD_STATE_ACTIVE;
	else if (strcmp(s, "user-response") == 0)
		return OFONO_USSD_STATE_USER_RESPONSE;

	ERR("unknown state: %s", s);
	return OFONO_USSD_STATE_IDLE;
}

static void _suppl_serv_property_update(OFono_Modem *m, const char *prop_name,
					DBusMessageIter *iter)
{

	if (strcmp(prop_name, "State") == 0) {
		const char *s;
		dbus_message_iter_get_basic(iter, &s);
		m->ussd_state = _suppl_serv_state_parse(s);
		DBG("%s USSD.State %d",	m->base.path, m->ussd_state);
	} else
		DBG("%s %s (unused property)", m->base.path, prop_name);
}

static void _msg_property_update(OFono_Modem *m, const char *prop_name,
					DBusMessageIter *iter)
{

	if (strcmp(prop_name, "ServiceCenterAddress") == 0) {
		const char *str;
		dbus_message_iter_get_basic(iter, &str);
		DBG("%s ServiceCenterAddress %s", m->base.path, str);
		eina_stringshare_replace(&(m->serv_center_addr), str);
	} else if (strcmp(prop_name, "UseDeliveryReports") == 0) {
		m->use_delivery_reports = _dbus_bool_get(iter);
		DBG("%s UseDeliveryReports %hhu", m->base.path,
			m->use_delivery_reports);
	} else if (strcmp(prop_name, "Bearer") == 0) {
		const char *str;
		dbus_message_iter_get_basic(iter, &str);
		DBG("%s Bearer %s", m->base.path, str);
		eina_stringshare_replace(&(m->msg_bearer), str);
	} else if (strcmp(prop_name, "Alphabet") == 0) {
		const char *str;
		dbus_message_iter_get_basic(iter, &str);
		DBG("%s Alphabet %s", m->base.path, str);
		eina_stringshare_replace(&(m->msg_alphabet), str);
	} else
		DBG("%s %s (unused property)", m->base.path, prop_name);
}

static void _notify_ofono_callbacks_modem_list(Eina_Inlist *list)
{
	OFono_Callback_List_Modem_Node *node;

	EINA_INLIST_FOREACH(list, node)
		node->cb((void *) node->cb_data);
}

static void _call_volume_property_changed(void *data, DBusMessage *msg)
{
	OFono_Modem *m = data;
	DBusMessageIter iter, variant_iter;
	const char *prop_name;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_message_iter_get_basic(&iter, &prop_name);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &variant_iter);
	_call_volume_property_update(m, prop_name, &variant_iter);

	_notify_ofono_callbacks_modem_list(cbs_modem_changed);
}

static void _msg_waiting_property_changed(void *data, DBusMessage *msg)
{
	OFono_Modem *m = data;
	DBusMessageIter iter, variant_iter;
	const char *prop_name;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_message_iter_get_basic(&iter, &prop_name);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &variant_iter);
	_msg_waiting_property_update(m, prop_name, &variant_iter);

	_notify_ofono_callbacks_modem_list(cbs_modem_changed);
}

static void _suppl_serv_property_changed(void *data, DBusMessage *msg)
{
	OFono_Modem *m = data;
	DBusMessageIter iter, variant_iter;
	const char *prop_name;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_message_iter_get_basic(&iter, &prop_name);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &variant_iter);
	_suppl_serv_property_update(m, prop_name, &variant_iter);

	_notify_ofono_callbacks_modem_list(cbs_modem_changed);
}

static void _suppl_serv_notification_recv(void *data __UNUSED__,
						DBusMessage *msg)
{
	DBusMessageIter iter;
	const char *s;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_message_iter_get_basic(&iter, &s);

	_notify_ofono_callbacks_ussd_notify_list(
		cbs_ussd_notify, EINA_FALSE, s);
}

static void _suppl_serv_request_recv(void *data __UNUSED__, DBusMessage *msg)
{
	DBusMessageIter iter;
	const char *s;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_message_iter_get_basic(&iter, &s);

	_notify_ofono_callbacks_ussd_notify_list(cbs_ussd_notify, EINA_TRUE, s);
}

static void _msg_property_changed(void *data, DBusMessage *msg)
{
	OFono_Modem *m = data;
	DBusMessageIter iter, variant_iter;
	const char *prop_name;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_message_iter_get_basic(&iter, &prop_name);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &variant_iter);
	_msg_property_update(m, prop_name, &variant_iter);

	_notify_ofono_callbacks_modem_list(cbs_modem_changed);
}

static OFono_Sent_SMS_State _sent_sms_state_parse(const char *str)
{
	if (strcmp(str, "pending") == 0)
		return OFONO_SENT_SMS_STATE_PENDING;
	else if (strcmp(str, "failed") == 0)
		return OFONO_SENT_SMS_STATE_FAILED;
	else if (strcmp(str, "sent") == 0)
		return OFONO_SENT_SMS_STATE_SENT;

	ERR("unknown message state: %s", str);
	return OFONO_SENT_SMS_STATE_FAILED;
}

static void _sent_sms_property_update(OFono_Sent_SMS *sms, const char *key,
					DBusMessageIter *value)
{
	if (strcmp(key, "State") == 0) {
		const char *str;
		OFono_Sent_SMS_State state;
		dbus_message_iter_get_basic(value, &str);
		state = _sent_sms_state_parse(str);
		DBG("%s State %d %s", sms->base.path, state, str);
		sms->state = state;
	} else
		DBG("%s %s (unused property)", sms->base.path, key);
}

static void _notify_ofono_callbacks_sent_sms(OFono_Error err,
						OFono_Sent_SMS *sms)
{
	OFono_Callback_List_Sent_SMS_Node *node;

	EINA_INLIST_FOREACH(cbs_sent_sms_changed, node)
		node->cb((void *) node->cb_data, err, sms);
}

static void _sent_sms_property_changed(void *data, DBusMessage *msg)
{
	OFono_Sent_SMS *sms = data;
	DBusMessageIter iter, value;
	const char *key;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	DBG("path=%s", sms->base.path);

	dbus_message_iter_get_basic(&iter, &key);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &value);
	_sent_sms_property_update(sms, key, &value);

	_notify_ofono_callbacks_sent_sms(OFONO_ERROR_NONE, sms);
}

static void _notify_ofono_callbacks_incoming_sms(unsigned int sms_class,
							time_t timestamp,
							const char *sender,
							const char *message)
{
	OFono_Callback_List_Incoming_SMS_Node *node;

	EINA_INLIST_FOREACH(cbs_incoming_sms, node) {
		node->cb((void *) node->cb_data, sms_class, timestamp, sender,
			message);
	}
}

static void _msg_notify(unsigned int sms_class, DBusMessageIter *iter)
{
	DBusMessageIter info;
	const char *message = NULL;
	const char *sender = NULL;
	const char *orig_timestamp = NULL;
	const char *local_timestamp = NULL;
	time_t timestamp;

	dbus_message_iter_get_basic(iter, &message);
	EINA_SAFETY_ON_NULL_RETURN(message);
	DBG("Message '%s'", message);

	dbus_message_iter_next(iter);
	dbus_message_iter_recurse(iter, &info);
	for (; dbus_message_iter_get_arg_type(&info) == DBUS_TYPE_DICT_ENTRY;
			dbus_message_iter_next(&info)) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(&info, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		if (strcmp(key, "Sender") == 0) {
			dbus_message_iter_get_basic(&value, &sender);
			DBG("Sender %s", sender);
		} else if (strcmp(key, "SentTime") == 0) {
			dbus_message_iter_get_basic(&value, &orig_timestamp);
			DBG("SentTime %s", orig_timestamp);
		} else if (strcmp(key, "LocalSentTime") == 0) {
			dbus_message_iter_get_basic(&value, &local_timestamp);
			DBG("LocalSentTime %s", local_timestamp);
		} else
			DBG("%s (unused property)", key);
	}

	EINA_SAFETY_ON_NULL_RETURN(sender);
	EINA_SAFETY_ON_NULL_RETURN(local_timestamp);
	timestamp = _ofono_time_parse(local_timestamp);

	_notify_ofono_callbacks_incoming_sms(sms_class, timestamp, sender,
						message);
}

static void _msg_immediate(void *data, DBusMessage *msg)
{
	OFono_Modem *m = data;
	DBusMessageIter iter;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	DBG("path=%s", m->base.path);
	_msg_notify(0, &iter);
}

static void _msg_incoming(void *data, DBusMessage *msg)
{
	OFono_Modem *m = data;
	DBusMessageIter iter;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	DBG("path=%s", m->base.path);
	_msg_notify(1, &iter);
}

static OFono_Sent_SMS *_sent_sms_common_add(OFono_Modem *m, const char *path)
{
	OFono_Sent_SMS *sms = _sent_sms_new(path);
	EINA_SAFETY_ON_NULL_RETURN_VAL(sms, NULL);
	eina_hash_add(m->sent_sms, sms->base.path, sms);

	_bus_object_signal_listen(&sms->base,
					OFONO_PREFIX "Message",
					"PropertyChanged",
					_sent_sms_property_changed, sms);
	return sms;
}

static OFono_Sent_SMS *_sent_sms_pending_add(OFono_Modem *m, const char *path,
						OFono_Sent_SMS_Cb_Context *ctx)
{
	OFono_Sent_SMS *sms;

	DBG("path=%s, ctx=%p", path, ctx);

	sms = _sent_sms_common_add(m, path);
	sms->pending_send = ctx;
	return sms;
}

static void _msg_add(OFono_Modem *m, const char *path, DBusMessageIter *prop)
{
	OFono_Sent_SMS *sms;

	DBG("path=%s, prop=%p", path, prop);

	sms = eina_hash_find(m->sent_sms, path);
	if (sms)
		DBG("SMS already exists %p (%s)", sms, path);
	else {
		sms = _sent_sms_common_add(m, path);
		EINA_SAFETY_ON_NULL_RETURN(sms);
	}

	for (; dbus_message_iter_get_arg_type(prop) == DBUS_TYPE_DICT_ENTRY;
			dbus_message_iter_next(prop)) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(prop, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		_sent_sms_property_update(sms, key, &value);
	}

	if (sms->pending_send) {
		OFono_Sent_SMS_Cb_Context *ctx = sms->pending_send;
		sms->destination = ctx->destination;
		sms->message = ctx->message;
		sms->timestamp = time(NULL);
		if (ctx->cb)
			ctx->cb((void *)ctx->data, OFONO_ERROR_NONE, sms);
		free(ctx);
		sms->pending_send = NULL;
	}

	_notify_ofono_callbacks_sent_sms(OFONO_ERROR_NONE, sms);
}

static void _msg_added(void *data, DBusMessage *msg)
{
	OFono_Modem *m = data;
	DBusMessageIter iter, properties;
	const char *path;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_message_iter_get_basic(&iter, &path);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &properties);

	_msg_add(m, path, &properties);
}

static void _msg_remove(OFono_Modem *m, const char *path)
{
	DBG("path=%s", path);
	eina_hash_del_by_key(m->sent_sms, path);
}

static void _msg_removed(void *data, DBusMessage *msg)
{
	OFono_Modem *m = data;
	DBusError err;
	const char *path;

	if (!msg) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_error_init(&err);
	if (!dbus_message_get_args(msg, &err, DBUS_TYPE_OBJECT_PATH,
					&path, NULL)) {
		ERR("Could not get MessageRemoved arguments: %s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		return;
	}

	_msg_remove(m, path);
}

static unsigned int _modem_interfaces_extract(DBusMessageIter *array)
{
	DBusMessageIter entry;
	unsigned int interfaces = 0;

	dbus_message_iter_recurse(array, &entry);
	for (; dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING;
			dbus_message_iter_next(&entry)) {
		const struct API_Interface_Map *itr;
		const char *name;
		size_t namelen;

		dbus_message_iter_get_basic(&entry, &name);

		if (strncmp(name, OFONO_PREFIX, strlen(OFONO_PREFIX)) != 0)
			continue;

		name += strlen(OFONO_PREFIX);
		namelen = strlen(name);

		DBG("interface: %s", name);
		for (itr = api_iface_map; itr->name != NULL; itr++) {
			if ((itr->namelen == namelen) &&
				(memcmp(itr->name, name, namelen) == 0)) {
				interfaces |= itr->bit;
				break;
			}
		}
		if (itr->name == NULL)
			WRN("ignored %s", name);
	}

	return interfaces;
}

static void _modem_update_interfaces(OFono_Modem *m, unsigned int ifaces)
{

	if (((m->interfaces & OFONO_API_CALL_VOL) == 0) &&
		(ifaces & OFONO_API_CALL_VOL) == OFONO_API_CALL_VOL)
		_ofono_call_volume_properties_get(m);

	if (((m->interfaces & OFONO_API_MSG_WAITING) == 0) &&
		(ifaces & OFONO_API_MSG_WAITING) == OFONO_API_MSG_WAITING)
		_ofono_msg_waiting_properties_get(m);

	if (((m->interfaces & OFONO_API_SUPPL_SERV) == 0) &&
		(ifaces & OFONO_API_SUPPL_SERV) == OFONO_API_SUPPL_SERV)
		_ofono_suppl_serv_properties_get(m);

	if (((m->interfaces & OFONO_API_MSG) == 0) &&
		(ifaces & OFONO_API_MSG) == OFONO_API_MSG)
		_ofono_msg_properties_get(m);
}

static void _modem_property_update(OFono_Modem *m, const char *key,
					DBusMessageIter *value)
{
	if (strcmp(key, "Powered") == 0) {
		m->powered = _dbus_bool_get(value);
		DBG("%s Powered %d", m->base.path, m->powered);
	} else if (strcmp(key, "Online") == 0) {
		m->online = _dbus_bool_get(value);
		DBG("%s Online %d", m->base.path, m->online);
	} else if (strcmp(key, "Interfaces") == 0) {
		unsigned int ifaces = _modem_interfaces_extract(value);
		DBG("%s Interfaces 0x%02x", m->base.path, ifaces);
		if (m->interfaces != ifaces) {
			_modem_update_interfaces(m, ifaces);
			m->interfaces = ifaces;

			if (modem_selected && modem_path_wanted &&
				modem_selected->base.path != modem_path_wanted)
				modem_selected = NULL;
		}
	} else if (strcmp(key, "Serial") == 0) {
		const char *serial;
		dbus_message_iter_get_basic(value, &serial);
		DBG("%s Serial %s", m->base.path, serial);
		eina_stringshare_replace(&m->serial, serial);
	} else if (strcmp(key, "Type") == 0) {
		const char *type;
		dbus_message_iter_get_basic(value, &type);
		DBG("%s Type %s", m->base.path, type);

		if (!modem_types)
			m->ignored = EINA_FALSE;
		else {
			const Eina_List *n;
			const char *t;
			m->ignored = EINA_TRUE;
			EINA_LIST_FOREACH(modem_types, n, t) {
				if (strcmp(t, type) == 0) {
					m->ignored = EINA_FALSE;
					break;
				}
			}
			if (m->ignored)
				INF("Modem %s type %s is ignored",
					m->base.path, type);
		}
	} else
		DBG("%s %s (unused property)", m->base.path, key);
}

static void _ofono_call_volume_properties_get_reply(void *data,
							DBusMessage *msg,
							DBusError *err)
{
	OFono_Modem *m = data;
	DBusMessageIter iter, prop;

	if (dbus_error_is_set(err)) {
		DBG("%s: %s", err->name, err->message);
		return;
	}

	dbus_message_iter_init(msg, &iter);
	dbus_message_iter_recurse(&iter, &prop);

	DBG("m=%s", m->base.path);
	for (; dbus_message_iter_get_arg_type(&prop) == DBUS_TYPE_DICT_ENTRY;
	     dbus_message_iter_next(&prop)) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(&prop, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);
		_call_volume_property_update(m, key, &value);
	}

	_notify_ofono_callbacks_modem_list(cbs_modem_changed);
}

static void _ofono_call_volume_properties_get(OFono_Modem *m)
{
	DBusMessage *msg;
	msg = dbus_message_new_method_call(bus_id, m->base.path,
						OFONO_PREFIX
						OFONO_CALL_VOL_IFACE,
						"GetProperties");
	DBG("m=%s", m->base.path);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	_bus_object_message_send(&m->base, msg,
				_ofono_call_volume_properties_get_reply, m);
}

static void _ofono_msg_waiting_properties_get_reply(void *data,
							DBusMessage *msg,
							DBusError *err)
{
	OFono_Modem *m = data;
	DBusMessageIter iter, prop;

	if (dbus_error_is_set(err)) {
		DBG("%s: %s", err->name, err->message);
		return;
	}

	dbus_message_iter_init(msg, &iter);
	dbus_message_iter_recurse(&iter, &prop);

	DBG("m=%s", m->base.path);
	for (; dbus_message_iter_get_arg_type(&prop) == DBUS_TYPE_DICT_ENTRY;
	     dbus_message_iter_next(&prop)) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(&prop, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);
		_msg_waiting_property_update(m, key, &value);
	}

	_notify_ofono_callbacks_modem_list(cbs_modem_changed);
}

static void _ofono_msg_waiting_properties_get(OFono_Modem *m)
{
	DBusMessage *msg;
	msg = dbus_message_new_method_call(bus_id, m->base.path,
						OFONO_PREFIX
						OFONO_MSG_WAITING_IFACE,
						"GetProperties");
	DBG("m=%s", m->base.path);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	_bus_object_message_send(&m->base, msg,
				_ofono_msg_waiting_properties_get_reply, m);
}

static void _ofono_suppl_serv_properties_get_reply(void *data,
							DBusMessage *msg,
							DBusError *err)
{
	OFono_Modem *m = data;
	DBusMessageIter iter, prop;

	if (dbus_error_is_set(err)) {
		DBG("%s: %s", err->name, err->message);
		return;
	}

	dbus_message_iter_init(msg, &iter);
	dbus_message_iter_recurse(&iter, &prop);

	DBG("m=%s", m->base.path);
	for (; dbus_message_iter_get_arg_type(&prop) == DBUS_TYPE_DICT_ENTRY;
	     dbus_message_iter_next(&prop)) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(&prop, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);
		_suppl_serv_property_update(m, key, &value);
	}

	_notify_ofono_callbacks_modem_list(cbs_modem_changed);
}

static void _ofono_suppl_serv_properties_get(OFono_Modem *m)
{
	DBusMessage *msg;
	msg = dbus_message_new_method_call(bus_id, m->base.path,
						OFONO_PREFIX
						OFONO_SUPPL_SERV_IFACE,
						"GetProperties");
	DBG("m=%s", m->base.path);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	_bus_object_message_send(&m->base, msg,
				_ofono_suppl_serv_properties_get_reply, m);
}


static void _ofono_msg_properties_get_reply(void *data,	DBusMessage *msg,
						DBusError *err)
{
	OFono_Modem *m = data;
	DBusMessageIter iter, prop;

	if (dbus_error_is_set(err)) {
		DBG("%s: %s", err->name, err->message);
		return;
	}

	dbus_message_iter_init(msg, &iter);
	dbus_message_iter_recurse(&iter, &prop);

	DBG("m=%s", m->base.path);
	for (; dbus_message_iter_get_arg_type(&prop) == DBUS_TYPE_DICT_ENTRY;
	     dbus_message_iter_next(&prop)) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(&prop, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);
		_msg_property_update(m, key, &value);
	}

	_notify_ofono_callbacks_modem_list(cbs_modem_changed);
}

static void _ofono_msg_properties_get(OFono_Modem *m)
{
	DBusMessage *msg;
	msg = dbus_message_new_method_call(bus_id, m->base.path,
						OFONO_PREFIX
						OFONO_MSG_IFACE,
						"GetProperties");
	DBG("m=%s", m->base.path);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	_bus_object_message_send(&m->base, msg,
				_ofono_msg_properties_get_reply, m);
}

static void _modem_add(const char *path, DBusMessageIter *prop)
{
	OFono_Modem *m;

	DBG("path=%s", path);

	m = eina_hash_find(modems, path);
	if (m) {
		DBG("Modem already exists %p (%s)", m, path);
		goto update_properties;
	}

	m = _modem_new(path);
	EINA_SAFETY_ON_NULL_RETURN(m);
	eina_hash_add(modems, m->base.path, m);

	_bus_object_signal_listen(&m->base, OFONO_PREFIX OFONO_VOICE_IFACE,
					"CallAdded", _call_added, m);
	_bus_object_signal_listen(&m->base, OFONO_PREFIX OFONO_VOICE_IFACE,
					"CallRemoved", _call_removed, m);
	_bus_object_signal_listen(&m->base, OFONO_PREFIX OFONO_CALL_VOL_IFACE,
					"PropertyChanged",
					_call_volume_property_changed, m);
	_bus_object_signal_listen(&m->base,
					OFONO_PREFIX OFONO_MSG_WAITING_IFACE,
					"PropertyChanged",
					_msg_waiting_property_changed, m);
	_bus_object_signal_listen(&m->base,
					OFONO_PREFIX OFONO_SUPPL_SERV_IFACE,
					"PropertyChanged",
					_suppl_serv_property_changed, m);
	_bus_object_signal_listen(&m->base,
					OFONO_PREFIX OFONO_SUPPL_SERV_IFACE,
					"NotificationReceived",
					_suppl_serv_notification_recv, m);
	_bus_object_signal_listen(&m->base,
					OFONO_PREFIX OFONO_SUPPL_SERV_IFACE,
					"RequestReceived",
					_suppl_serv_request_recv, m);

	_bus_object_signal_listen(&m->base, OFONO_PREFIX OFONO_MSG_IFACE,
					"PropertyChanged",
					_msg_property_changed, m);
	_bus_object_signal_listen(&m->base, OFONO_PREFIX OFONO_MSG_IFACE,
					"ImmediateMessage", _msg_immediate, m);
	_bus_object_signal_listen(&m->base, OFONO_PREFIX OFONO_MSG_IFACE,
					"IncomingMessage", _msg_incoming, m);
	_bus_object_signal_listen(&m->base, OFONO_PREFIX OFONO_MSG_IFACE,
					"MessageAdded", _msg_added, m);
	_bus_object_signal_listen(&m->base, OFONO_PREFIX OFONO_MSG_IFACE,
					"MessageRemoved", _msg_removed, m);

	/* TODO: do we need to listen to BarringActive or Forwarded? */

	if (modem_selected && modem_path_wanted &&
		modem_selected->base.path != modem_path_wanted)
		modem_selected = NULL;

update_properties:
	if (!prop)
		return;
	for (; dbus_message_iter_get_arg_type(prop) == DBUS_TYPE_DICT_ENTRY;
			dbus_message_iter_next(prop)) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(prop, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		_modem_property_update(m, key, &value);
	}

	_notify_ofono_callbacks_modem_list(cbs_modem_changed);

	if (m->interfaces & OFONO_API_VOICE)
		_modem_calls_load(m);
}

static void _modem_remove(const char *path)
{
	DBG("path=%s", path);
	eina_hash_del_by_key(modems, path);
}

static void _ofono_modems_get_reply(void *data __UNUSED__, DBusMessage *msg,
					DBusError *err)
{
	DBusMessageIter array, dict;

	pc_get_modems = NULL;

	if (!msg) {
		if (err)
			ERR("%s: %s", err->name, err->message);
		else
			ERR("No message");
		return;
	}

	EINA_SAFETY_ON_NULL_RETURN(modems);
	eina_hash_free_buckets(modems);

	if (!dbus_message_iter_init(msg, &array)) {
		ERR("Could not get modems");
		return;
	}

	dbus_message_iter_recurse(&array, &dict);
	for (; dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_STRUCT;
			dbus_message_iter_next(&dict)) {
		DBusMessageIter value, properties;
		const char *path;

		dbus_message_iter_recurse(&dict, &value);
		dbus_message_iter_get_basic(&value, &path);

		dbus_message_iter_next(&value);
		dbus_message_iter_recurse(&value, &properties);

		_modem_add(path, &properties);
	}

	if (!ofono_voice_is_online()) {
		ofono_powered_set(EINA_TRUE, NULL, NULL);
		return;
	}
}

static void _modem_added(void *data __UNUSED__, DBusMessage *msg)
{
	DBusMessageIter iter, properties;
	const char *path;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_message_iter_get_basic(&iter, &path);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &properties);

	_modem_add(path, &properties);
}

static void _modem_removed(void *data __UNUSED__, DBusMessage *msg)
{
	DBusError err;
	const char *path;

	if (!msg) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_error_init(&err);
	if (!dbus_message_get_args(msg, &err, DBUS_TYPE_OBJECT_PATH,
					&path, NULL)) {
		ERR("Could not get ModemRemoved arguments: %s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		return;
	}

	_modem_remove(path);
}

static void _modem_property_changed(void *data __UNUSED__, DBusMessage *msg)
{
	const char *path;
	OFono_Modem *m;
	DBusMessageIter iter, value;
	const char *key;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	path = dbus_message_get_path(msg);
	DBG("path=%s", path);

	m = eina_hash_find(modems, path);
	if (!m) {
		DBG("Modem is unknown (%s)", path);
		return;
	}

	dbus_message_iter_get_basic(&iter, &key);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &value);
	_modem_property_update(m, key, &value);

	_notify_ofono_callbacks_modem_list(cbs_modem_changed);
}

static void _modems_load(void)
{
	DBusMessage *msg = dbus_message_new_method_call(
		bus_id, "/", OFONO_PREFIX OFONO_MANAGER_IFACE, "GetModems");

	if (pc_get_modems)
		dbus_pending_call_cancel(pc_get_modems);

	DBG("Get modems");
	pc_get_modems = e_dbus_message_send(
		bus_conn, msg, _ofono_modems_get_reply, -1, NULL);
	dbus_message_unref(msg);
}

static void _ofono_connected(const char *id)
{
	free(bus_id);
	bus_id = strdup(id);

	sig_modem_added = e_dbus_signal_handler_add(
		bus_conn, bus_id, "/",
		OFONO_PREFIX OFONO_MANAGER_IFACE,
		"ModemAdded",
		_modem_added, NULL);

	sig_modem_removed = e_dbus_signal_handler_add(
		bus_conn, bus_id, "/",
		OFONO_PREFIX OFONO_MANAGER_IFACE,
		"ModemRemoved",
		_modem_removed, NULL);

	sig_modem_prop_changed = e_dbus_signal_handler_add(
		bus_conn, bus_id, NULL,
		OFONO_PREFIX OFONO_MODEM_IFACE,
		"PropertyChanged",
		_modem_property_changed, NULL);

	_modems_load();

	_notify_ofono_callbacks_modem_list(cbs_modem_connected);
}

static void _ofono_disconnected(void)
{
	eina_hash_free_buckets(modems);

	if (sig_modem_added) {
		e_dbus_signal_handler_del(bus_conn, sig_modem_added);
		sig_modem_added = NULL;
	}

	if (sig_modem_removed) {
		e_dbus_signal_handler_del(bus_conn, sig_modem_removed);
		sig_modem_removed = NULL;
	}

	if (sig_modem_prop_changed) {
		e_dbus_signal_handler_del(bus_conn, sig_modem_prop_changed);
		sig_modem_prop_changed = NULL;
	}

	if (bus_id) {
		_notify_ofono_callbacks_modem_list(cbs_modem_disconnected);
		free(bus_id);
		bus_id = NULL;
	}
}

static void _name_owner_changed(void *data __UNUSED__, DBusMessage *msg)
{
	DBusError err;
	const char *name, *from, *to;

	dbus_error_init(&err);
	if (!dbus_message_get_args(msg, &err,
					DBUS_TYPE_STRING, &name,
					DBUS_TYPE_STRING, &from,
					DBUS_TYPE_STRING, &to,
					DBUS_TYPE_INVALID)) {
		ERR("Could not get NameOwnerChanged arguments: %s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		return;
	}

	if (strcmp(name, bus_name) != 0)
		return;

	DBG("NameOwnerChanged %s from=%s to=%s", name, from, to);

	if (from[0] == '\0' && to[0] != '\0') {
		INF("oFono appeared as %s", to);
		_ofono_connected(to);
	} else if (from[0] != '\0' && to[0] == '\0') {
		INF("oFono disappeared from %s", from);
		_ofono_disconnected();
	}
}

static void _ofono_get_name_owner(void *data __UNUSED__, DBusMessage *msg, DBusError *err)
{
	DBusMessageIter itr;
	const char *id;

	if (!msg) {
		if (err)
			ERR("%s: %s", err->name, err->message);
		else
			ERR("No message");
		return;
	}

	dbus_message_iter_init(msg, &itr);
	dbus_message_iter_get_basic(&itr, &id);
	if (!id || id[0] == '\0') {
		ERR("No name owner fo %s!", bus_name);
		return;
	}

	INF("oFono bus id: %s", id);
	_ofono_connected(id);
}

OFono_Pending *ofono_modem_change_pin(const char *what, const char *old,
					const char *new, OFono_Simple_Cb cb,
					const void *data)
{
	OFono_Simple_Cb_Context *ctx = NULL;
	OFono_Error err = OFONO_ERROR_OFFLINE;
	OFono_Pending *p;
	DBusMessage *msg;
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_GOTO(m, error);
	EINA_SAFETY_ON_NULL_GOTO(what, error);
	EINA_SAFETY_ON_NULL_GOTO(old, error);
	EINA_SAFETY_ON_NULL_GOTO(new, error);

	if ((m->interfaces & OFONO_API_SIM) == 0)
		goto error;
	err = OFONO_ERROR_FAILED;

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(
		bus_id, m->base.path, OFONO_PREFIX OFONO_SIM_IFACE,
		"ChangePin");
	if (!msg)
		goto error;

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &what,
					DBUS_TYPE_STRING, &old,
					DBUS_TYPE_STRING, &new,
					DBUS_TYPE_INVALID))
		goto error_message;

	INF("ChangePin(%s, %s, %s)", what, old, new);
	p = _bus_object_message_send(&m->base, msg, _ofono_simple_reply, ctx);
	return p;

error_message:
	dbus_message_unref(msg);
error:
	if (cb)
		cb((void *)data, err);
	free(ctx);
	return NULL;
}

OFono_Pending *ofono_modem_reset_pin(const char *what, const char *puk,
					const char *new, OFono_Simple_Cb cb,
					const void *data)
{
	OFono_Simple_Cb_Context *ctx = NULL;
	OFono_Error err = OFONO_ERROR_OFFLINE;
	OFono_Pending *p;
	DBusMessage *msg;
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_GOTO(m, error);
	EINA_SAFETY_ON_NULL_GOTO(what, error);
	EINA_SAFETY_ON_NULL_GOTO(puk, error);
	EINA_SAFETY_ON_NULL_GOTO(new, error);

	if ((m->interfaces & OFONO_API_SIM) == 0)
		goto error;
	err = OFONO_ERROR_FAILED;

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(
		bus_id, m->base.path, OFONO_PREFIX OFONO_SIM_IFACE, "ResetPin");
	if (!msg)
		goto error;

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &what,
					DBUS_TYPE_STRING, &puk,
					DBUS_TYPE_STRING, &new,
					DBUS_TYPE_INVALID))
		goto error_message;

	INF("ResetPin(%s, %s, %s)", what, puk, new);
	p = _bus_object_message_send(&m->base, msg, _ofono_simple_reply, ctx);
	return p;

error_message:
	dbus_message_unref(msg);
error:
	if (cb)
		cb((void *)data, err);
	free(ctx);
	return NULL;
}

static OFono_Pending *_ofono_modem_property_set(char *property,
                                                int type, void *value,
                                                OFono_Simple_Cb cb,
                                                const void *data)
{
	OFono_Pending *p;
	OFono_Simple_Cb_Context *ctx = NULL;
	DBusMessage *msg;
	DBusMessageIter iter, variant;
	OFono_Modem *found_path = NULL, *found_hfp = NULL, *m;
	Eina_Iterator *itr;

	if (modem_selected)
		m = modem_selected;
	else {
		itr = eina_hash_iterator_data_new(modems);
		EINA_ITERATOR_FOREACH(itr, m) {
			if (m->ignored)
				continue;

		    if ((modem_path_wanted) && (!found_path)) {
				DBG("m=%s, wanted=%s", m->base.path, modem_path_wanted);
				if (m->base.path == modem_path_wanted) {
					found_path = m;
					break;
				}
			}

			if (!found_hfp) {
				DBG("m=%#x, mask=%#x, previous=%s "
					"(online=%d, powered=%d)",
					m->interfaces, modem_api_mask,
					found_hfp ? found_hfp->base.path : "",
					found_hfp ? found_hfp->online : 0,
					found_hfp ? found_hfp->powered : 0);
					if (strncmp(m->base.path, "/hfp", 4) == 0)
						found_hfp = m;
			}
		}
		eina_iterator_free(itr);
		m = found_path ? found_path : found_hfp;
	}

	if (!m)
		return;

	char type_to_send[2] = { type , DBUS_TYPE_INVALID };

	EINA_SAFETY_ON_NULL_GOTO(m, error_no_dbus_message);

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error_no_dbus_message);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(bus_id, m->base.path,
										OFONO_PREFIX OFONO_MODEM_IFACE,
										"SetProperty");
	if (!msg)
		goto error_no_dbus_message;

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &property,
									DBUS_TYPE_INVALID))
		goto error_message_args;

	dbus_message_iter_init_append(msg, &iter);

	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT,
											type_to_send, &variant))
		goto error_message_args;

	if (!dbus_message_iter_append_basic(&variant, type, value) ||
			!dbus_message_iter_close_container(&iter, &variant)) {
		dbus_message_iter_abandon_container(&iter, &variant);
		goto error_message_args;
	}

 	INF("%s.SetProperty(%s)", OFONO_MODEM_IFACE, property);
	p = _bus_object_message_send(&m->base, msg, _ofono_simple_reply, ctx);
	return p;

error_message_args:
	dbus_message_unref(msg);

error_no_dbus_message:
	if (cb)
		cb((void *)data, OFONO_ERROR_FAILED);
	free(ctx);
	return NULL;
}

OFono_Pending *ofono_powered_set(Eina_Bool powered, OFono_Simple_Cb cb,
                                const void *data)
{
	dbus_bool_t dbus_powered = !!powered;

	return  _ofono_modem_property_set("Powered", DBUS_TYPE_BOOLEAN,
		&dbus_powered, cb, data);
}

Eina_Bool ofono_powered_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, EINA_FALSE);
	return m->powered;
}

static char *_ss_initiate_convert_ussd(const char *type __UNUSED__,
					DBusMessageIter *itr)
{
	const char *ussd_response;

	if (dbus_message_iter_get_arg_type(itr) != DBUS_TYPE_STRING) {
		ERR("Invalid type: %c (expected: %c)",
			dbus_message_iter_get_arg_type(itr), DBUS_TYPE_STRING);
		return NULL;
	}
	dbus_message_iter_get_basic(itr, &ussd_response);
	EINA_SAFETY_ON_NULL_RETURN_VAL(ussd_response, NULL);
	return strdup(ussd_response);
}

static void _ss_initiate_cb_dict_convert(Eina_Strbuf *buf,
						DBusMessageIter *dict)
{
	for (; dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY;
			dbus_message_iter_next(dict)) {
		DBusMessageIter e, v;
		const char *key, *value;

		dbus_message_iter_recurse(dict, &e);
		dbus_message_iter_get_basic(&e, &key);

		dbus_message_iter_next(&e);
		dbus_message_iter_recurse(&e, &v);
		dbus_message_iter_get_basic(&v, &value);

		eina_strbuf_append_printf(buf, "&nbsp;&nbsp;&nbsp;%s=%s<br>",
						key, value);
	}
}

static char *_ss_initiate_convert_call1(const char *type, DBusMessageIter *itr)
{
	DBusMessageIter array, dict;
	const char *ss_op, *service;
	Eina_Strbuf *buf;
	char *str;

	dbus_message_iter_recurse(itr, &array);

	if (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_STRING) {
		ERR("Invalid type: %c (expected: %c)",
			dbus_message_iter_get_arg_type(&array),
			DBUS_TYPE_STRING);
		return NULL;
	}
	dbus_message_iter_get_basic(&array, &ss_op);
	EINA_SAFETY_ON_NULL_RETURN_VAL(ss_op, NULL);

	if (!dbus_message_iter_next(&array)) {
		ERR("Missing %s service", type);
		return NULL;
	}

	if (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_STRING) {
		ERR("Invalid type: %c (expected: %c)",
			dbus_message_iter_get_arg_type(&array),
			DBUS_TYPE_STRING);
		return NULL;
	}
	dbus_message_iter_get_basic(&array, &service);
	EINA_SAFETY_ON_NULL_RETURN_VAL(service, NULL);

	if (!dbus_message_iter_next(&array)) {
		ERR("Missing %s information", type);
		return NULL;
	}

	buf = eina_strbuf_new();
	eina_strbuf_append_printf(buf, "<b>%s %s=%s</b><br><br>",
					type, ss_op, service);

	dbus_message_iter_recurse(&array, &dict);
	_ss_initiate_cb_dict_convert(buf, &dict);

	str = eina_strbuf_string_steal(buf);
	eina_strbuf_free(buf);
	return str;
}

static char *_ss_initiate_convert_call_waiting(const char *type,
						DBusMessageIter *itr)
{
	DBusMessageIter array, dict;
	const char *ss_op;
	Eina_Strbuf *buf;
	char *str;

	dbus_message_iter_recurse(itr, &array);

	if (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_STRING) {
		ERR("Invalid type: %c (expected: %c)",
			dbus_message_iter_get_arg_type(&array),
			DBUS_TYPE_STRING);
		return NULL;
	}
	dbus_message_iter_get_basic(&array, &ss_op);
	EINA_SAFETY_ON_NULL_RETURN_VAL(ss_op, NULL);

	if (!dbus_message_iter_next(&array)) {
		ERR("Missing %s information", type);
		return NULL;
	}

	buf = eina_strbuf_new();
	eina_strbuf_append_printf(buf, "<b>%s %s</b><br><br>",
					type, ss_op);

	dbus_message_iter_recurse(&array, &dict);
	_ss_initiate_cb_dict_convert(buf, &dict);

	str = eina_strbuf_string_steal(buf);
	eina_strbuf_free(buf);
	return str;
}

static char *_ss_initiate_convert_call2(const char *type,
						DBusMessageIter *itr)
{
	DBusMessageIter array;
	const char *ss_op, *status;
	Eina_Strbuf *buf;
	char *str;

	dbus_message_iter_recurse(itr, &array);

	if (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_STRING) {
		ERR("Invalid type: %c (expected: %c)",
			dbus_message_iter_get_arg_type(&array),
			DBUS_TYPE_STRING);
		return NULL;
	}
	dbus_message_iter_get_basic(&array, &ss_op);
	EINA_SAFETY_ON_NULL_RETURN_VAL(ss_op, NULL);

	if (!dbus_message_iter_next(&array)) {
		ERR("Missing %s status", type);
		return NULL;
	}

	if (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_STRING) {
		ERR("Invalid type: %c (expected: %c)",
			dbus_message_iter_get_arg_type(&array),
			DBUS_TYPE_STRING);
		return NULL;
	}
	dbus_message_iter_get_basic(&array, &status);
	EINA_SAFETY_ON_NULL_RETURN_VAL(status, NULL);

	buf = eina_strbuf_new();
	eina_strbuf_append_printf(buf, "<b>%s:</b><br><br>%s=%s",
					type, ss_op, status);

	str = eina_strbuf_string_steal(buf);
	eina_strbuf_free(buf);
	return str;
}

static const struct SS_Initiate_Convert_Map {
	const char *type;
	size_t typelen;
	char *(*convert)(const char *type, DBusMessageIter *itr);
} ss_initiate_convert_map[] = {
#define MAP(type, conv) {type, sizeof(type) - 1, conv}
	MAP("USSD", _ss_initiate_convert_ussd),
	MAP("CallBarring", _ss_initiate_convert_call1),
	MAP("CallForwarding", _ss_initiate_convert_call1),
	MAP("CallWaiting", _ss_initiate_convert_call_waiting),
	MAP("CallingLinePresentation", _ss_initiate_convert_call2),
	MAP("ConnectedLinePresentation", _ss_initiate_convert_call2),
	MAP("CallingLineRestriction", _ss_initiate_convert_call2),
	MAP("ConnectedLineRestriction", _ss_initiate_convert_call2),
#undef MAP
	{NULL, 0, NULL}
};

static char *_ss_initiate_convert(DBusMessage *msg)
{
	DBusMessageIter array, variant;
	const struct SS_Initiate_Convert_Map *citr;
	const char *type = NULL;
	size_t typelen;

	if (!dbus_message_iter_init(msg, &array))
		goto error;

	if (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_STRING) {
		ERR("Invalid type for first argument: %c (expected: %c)",
			dbus_message_iter_get_arg_type(&array),
			DBUS_TYPE_STRING);
		goto error;
	}
	dbus_message_iter_get_basic(&array, &type);
	if (!type) {
		ERR("Couldn't get SupplementaryServices.Initiate type");
		goto error;
	}
	DBG("type: %s", type);

	if (!dbus_message_iter_next(&array)) {
		ERR("Couldn't get SupplementaryServices.Initiate payload");
		goto error;
	}
	dbus_message_iter_recurse(&array, &variant);

	typelen = strlen(type);
	for (citr = ss_initiate_convert_map; citr->type != NULL; citr++) {
		if ((citr->typelen == typelen) &&
			(memcmp(citr->type, type, typelen) == 0)) {
			return citr->convert(type, &variant);
		}
	}
	ERR("Could not convert SupplementaryServices.Initiate type %s", type);

error:
	return NULL;
}

OFono_Pending *ofono_ss_initiate(const char *command, OFono_String_Cb cb, const void *data)
{
	OFono_String_Cb_Context *ctx = NULL;
	OFono_Error err = OFONO_ERROR_OFFLINE;
	OFono_Pending *p;
	DBusMessage *msg;
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_GOTO(m, error);
	EINA_SAFETY_ON_NULL_GOTO(command, error);

	if ((m->interfaces & OFONO_API_SUPPL_SERV) == 0)
		goto error;
	err = OFONO_ERROR_FAILED;

	ctx = calloc(1, sizeof(OFono_String_Cb_Context));
	EINA_SAFETY_ON_NULL_GOTO(ctx, error);
	ctx->cb = cb;
	ctx->data = data;
	ctx->name = OFONO_PREFIX OFONO_SUPPL_SERV_IFACE ".Initiate";
	ctx->convert = _ss_initiate_convert;

	msg = dbus_message_new_method_call(
		bus_id, m->base.path, OFONO_PREFIX OFONO_SUPPL_SERV_IFACE,
		"Initiate");
	if (!msg)
		goto error;

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &command,
					DBUS_TYPE_INVALID))
		goto error_message;

	INF("SupplementaryServices.Initiate(%s)", command);
	p = _bus_object_message_send(&m->base, msg, _ofono_string_reply, ctx);
	return p;

error_message:
	dbus_message_unref(msg);
error:
	if (cb)
		cb((void *)data, err, NULL);
	free(ctx);
	return NULL;
}

static char *_ussd_respond_convert(DBusMessage *msg)
{
	DBusMessageIter itr;
	const char *s;

	if (!msg || !dbus_message_iter_init(msg, &itr)) {
		ERR("Could not handle message %p", msg);
		return NULL;
	}

	if (dbus_message_iter_get_arg_type(&itr) != DBUS_TYPE_STRING) {
		ERR("Invalid type: %c (expected: %c)",
			dbus_message_iter_get_arg_type(&itr), DBUS_TYPE_STRING);
		return NULL;
	}
	dbus_message_iter_get_basic(&itr, &s);
	EINA_SAFETY_ON_NULL_RETURN_VAL(s, NULL);
	return strdup(s);
}

OFono_Pending *ofono_ussd_respond(const char *string,
					OFono_String_Cb cb, const void *data)
{
	OFono_String_Cb_Context *ctx = NULL;
	OFono_Error err = OFONO_ERROR_OFFLINE;
	OFono_Pending *p;
	DBusMessage *msg;
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_GOTO(m, error);
	EINA_SAFETY_ON_NULL_GOTO(string, error);

	if ((m->interfaces & OFONO_API_SUPPL_SERV) == 0)
		goto error;
	err = OFONO_ERROR_FAILED;

	ctx = calloc(1, sizeof(OFono_String_Cb_Context));
	EINA_SAFETY_ON_NULL_GOTO(ctx, error);
	ctx->cb = cb;
	ctx->data = data;
	ctx->name = OFONO_PREFIX OFONO_SUPPL_SERV_IFACE ".Initiate";
	ctx->convert = _ussd_respond_convert;

	msg = dbus_message_new_method_call(
		bus_id, m->base.path, OFONO_PREFIX OFONO_SUPPL_SERV_IFACE,
		"Respond");
	if (!msg)
		goto error;

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &string,
					DBUS_TYPE_INVALID))
		goto error_message;

	INF("SupplementaryServices.Respond(%s)", string);
	p = _bus_object_message_send(&m->base, msg, _ofono_string_reply, ctx);
	return p;

error_message:
	dbus_message_unref(msg);
error:
	if (cb)
		cb((void *)data, err, NULL);
	free(ctx);
	return NULL;
}

OFono_Pending *ofono_ussd_cancel(OFono_Simple_Cb cb, const void *data)
{
	return _ofono_simple_do(OFONO_API_SUPPL_SERV, "Cancel", cb, data);
}

static void _ofono_dial_reply(void *data, DBusMessage *msg, DBusError *err)
{
	OFono_Call_Cb_Context *ctx = data;
	OFono_Call *c = NULL;
	OFono_Error oe = OFONO_ERROR_NONE;

	if (!msg) {
		DBG("%s: %s", err->name, err->message);
		oe = _ofono_error_parse(err->name);
	} else {
		DBusError e;
		const char *path;
		dbus_error_init(&e);
		if (!dbus_message_get_args(msg, &e, DBUS_TYPE_OBJECT_PATH,
						&path, DBUS_TYPE_INVALID)) {
			ERR("Could not get Dial reply: %s: %s",
				e.name, e.message);
			dbus_error_free(&e);
			oe = OFONO_ERROR_FAILED;
		} else {
			c = eina_hash_find(ctx->modem->calls, path);
			DBG("path=%s, existing call=%p", path, c);
			if (!c) {
				c = _call_pending_add(ctx->modem, path, ctx);
				if (c) {
					/* ctx->cb will be dispatched on
					 * CallAdded signal handler.
					 */
					return;
				}
			}

			ERR("Could not find call %s", path);
			oe = OFONO_ERROR_FAILED;
		}
	}

	if (ctx->cb)
		ctx->cb((void *)ctx->data, oe, c);

	free(ctx);
}

OFono_Pending *ofono_dial(const char *number, const char *hide_callerid,
				OFono_Call_Cb cb, const void *data)
{
	OFono_Call_Cb_Context *ctx = NULL;
	OFono_Error err = OFONO_ERROR_OFFLINE;
	OFono_Pending *p;
	DBusMessage *msg;
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_GOTO(m, error);


	if ((m->interfaces & OFONO_API_VOICE) == 0)
		goto error;
	err = OFONO_ERROR_FAILED;

	if (!hide_callerid)
		hide_callerid = "";

	ctx = calloc(1, sizeof(OFono_Call_Cb_Context));
	EINA_SAFETY_ON_NULL_GOTO(ctx, error);
	ctx->cb = cb;
	ctx->data = data;
	ctx->name = OFONO_PREFIX OFONO_VOICE_IFACE ".Dial";
	ctx->modem = m;

	msg = dbus_message_new_method_call(
		bus_id, m->base.path, OFONO_PREFIX OFONO_VOICE_IFACE, "Dial");
	if (!msg)
		goto error;

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &number,
					DBUS_TYPE_STRING, &hide_callerid,
					DBUS_TYPE_INVALID))
		goto error_message;

	INF("Dial(%s, %s)", number, hide_callerid);
	p = _bus_object_message_send(&m->base, msg, _ofono_dial_reply, ctx);
	return p;

error_message:
	dbus_message_unref(msg);
error:
	if (cb)
		cb((void *)data, err, NULL);
	free(ctx);
	return NULL;
}

static OFono_Pending *_ofono_simple_do(OFono_API api, const char *method,
					OFono_Simple_Cb cb, const void *data)
{
	OFono_Simple_Cb_Context *ctx = NULL;
	OFono_Error err = OFONO_ERROR_OFFLINE;
	OFono_Pending *p;
	DBusMessage *msg;
	char iface[128] = "";
	const struct API_Interface_Map *itr;
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_GOTO(m, error);
	EINA_SAFETY_ON_NULL_GOTO(method, error);

	if ((m->interfaces & api) == 0)
		goto error;
	err = OFONO_ERROR_FAILED;

	for (itr = api_iface_map; itr->name != NULL; itr++) {
		if (itr->bit == api) {
			snprintf(iface, sizeof(iface), "%s%s",
					OFONO_PREFIX, itr->name);
			break;
		}
	}
	if (iface[0] == '\0') {
		ERR("Could not map api %d to interface name!", api);
		goto error;
	}

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(bus_id, m->base.path, iface, method);
	if (!msg)
		goto error;

	INF("%s.%s()", iface, method);
	p = _bus_object_message_send(&m->base, msg, _ofono_simple_reply, ctx);
	return p;

error:
	if (cb)
		cb((void *)data, err);
	free(ctx);
	return NULL;
}

OFono_Pending *ofono_transfer(OFono_Simple_Cb cb, const void *data)
{
	return _ofono_simple_do(OFONO_API_VOICE, "Transfer", cb, data);
}

OFono_Pending *ofono_swap_calls(OFono_Simple_Cb cb, const void *data)
{
	return _ofono_simple_do(OFONO_API_VOICE, "SwapCalls", cb, data);
}

OFono_Pending *ofono_release_and_answer(OFono_Simple_Cb cb, const void *data)
{
	return _ofono_simple_do(OFONO_API_VOICE, "ReleaseAndAnswer", cb, data);
}

OFono_Pending *ofono_release_and_swap(OFono_Simple_Cb cb, const void *data)
{
	return _ofono_simple_do(OFONO_API_VOICE, "ReleaseAndSwap", cb, data);
}

OFono_Pending *ofono_hold_and_answer(OFono_Simple_Cb cb, const void *data)
{
	return _ofono_simple_do(OFONO_API_VOICE, "HoldAndAnswer", cb, data);
}

OFono_Pending *ofono_hangup_all(OFono_Simple_Cb cb, const void *data)
{
	return _ofono_simple_do(OFONO_API_VOICE, "HangupAll", cb, data);
}

const char *ofono_modem_serial_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, NULL);
	return m->serial;
}

void ofono_modem_api_require(const char *spec)
{
	unsigned int api_mask = 0;
	const char *name = spec;

	EINA_SAFETY_ON_NULL_RETURN(spec);

	do {
		const struct API_Interface_Map *itr;
		const char *p;
		unsigned int namelen;

		p = strchr(name, ',');
		if (p)
			namelen = p - name;
		else
			namelen = strlen(name);

		for (itr = api_iface_map; itr->name != NULL; itr++) {
			if ((itr->namelen == namelen) &&
				(memcmp(itr->name, name, namelen) == 0)) {
				api_mask |= itr->bit;
				break;
			}
		}
		if (itr->name == NULL)
			WRN("Unknown oFono API: %.*s", namelen, name);

		if (p)
			name = p + 1;
		else
			name = NULL;
	} while (name);

	if (api_mask)
		DBG("API parsed: '%s' = %#x", spec, api_mask);
	else {
		ERR("Could not parse API: %s", spec);
		return;
	}

	if (modem_api_mask == api_mask)
		return;
	modem_api_mask = api_mask;
	modem_selected = NULL;
}

void ofono_modem_api_list(FILE *fp, const char *prefix, const char *suffix)
{
	const struct API_Interface_Map *itr;
	for (itr = api_iface_map; itr->name != NULL; itr++)
		fprintf(fp, "%s%s%s", prefix, itr->name, suffix);
}

void ofono_modem_type_require(const char *spec)
{
	Eina_List *lst = NULL;
	const char *name = spec;

	EINA_SAFETY_ON_NULL_RETURN(spec);

	do {
		const char **itr;
		const char *p;
		unsigned int namelen;

		p = strchr(name, ',');
		if (p)
			namelen = p - name;
		else
			namelen = strlen(name);

		for (itr = known_modem_types; *itr != NULL; itr++) {
			unsigned int itrlen = strlen(*itr);
			if ((itrlen == namelen) &&
				(memcmp(*itr, name, namelen) == 0)) {
				lst = eina_list_append(lst, *itr);
				break;
			}
		}
		if (*itr == NULL)
			WRN("Unknown oFono type: %.*s", namelen, name);

		if (p)
			name = p + 1;
		else
			name = NULL;
	} while (name);

	if (lst)
		DBG("Type parsed: '%s'", spec);
	else {
		ERR("Could not parse type: %s", spec);
		return;
	}

	eina_list_free(modem_types);
	modem_types = lst;
	modem_selected = NULL;
}

void ofono_modem_type_list(FILE *fp, const char *prefix, const char *suffix)
{
	const char **itr;
	for (itr = known_modem_types; *itr != NULL; itr++)
		fprintf(fp, "%s%s%s", prefix, *itr, suffix);
}

void ofono_modem_path_wanted_set(const char *path)
{
	if (eina_stringshare_replace(&modem_path_wanted, path))
		modem_selected = NULL;
}

unsigned int ofono_modem_api_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, 0);
	return m->interfaces;
}

Eina_Bool ofono_init(void)
{
	tzset();

	if (!elm_need_e_dbus()) {
		CRITICAL("Elementary does not support DBus.");
		return EINA_FALSE;
	}

	bus_conn = e_dbus_bus_get(DBUS_BUS_SYSTEM);
	if (!bus_conn) {
		CRITICAL("Could not get DBus System Bus");
		return EINA_FALSE;
	}

	modems = eina_hash_string_small_new(EINA_FREE_CB(_modem_free));
	EINA_SAFETY_ON_NULL_RETURN_VAL(modems, EINA_FALSE);

	e_dbus_signal_handler_add(bus_conn, E_DBUS_FDO_BUS, E_DBUS_FDO_PATH,
					E_DBUS_FDO_INTERFACE,
					"NameOwnerChanged",
					_name_owner_changed, NULL);

	e_dbus_get_name_owner(bus_conn, bus_name, _ofono_get_name_owner, NULL);

	return EINA_TRUE;
}

void ofono_shutdown(void)
{
	if (pc_get_modems) {
		dbus_pending_call_cancel(pc_get_modems);
		pc_get_modems = NULL;
	}

	_ofono_disconnected();
	eina_stringshare_replace(&modem_path_wanted, NULL);

	eina_hash_free(modems);
	modems = NULL;

	eina_list_free(modem_types);
}

static OFono_Pending *_ofono_call_volume_property_set(char *property,
							int type, void *value,
							OFono_Simple_Cb cb,
							const void *data)
{
	OFono_Pending *p;
	OFono_Simple_Cb_Context *ctx = NULL;
	DBusMessage *msg;
	DBusMessageIter iter, variant;
	OFono_Modem *m = _modem_selected_get();
	char type_to_send[2] = { type , DBUS_TYPE_INVALID };

	EINA_SAFETY_ON_NULL_GOTO(m, error_no_dbus_message);

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error_no_dbus_message);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(bus_id, m->base.path,
					   OFONO_PREFIX OFONO_CALL_VOL_IFACE,
					   "SetProperty");
	if (!msg)
		goto error_no_dbus_message;

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &property,
				 DBUS_TYPE_INVALID))
		goto error_message_args;

	dbus_message_iter_init_append(msg, &iter);

	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT,
					 type_to_send, &variant))
		goto error_message_args;

	if (!dbus_message_iter_append_basic(&variant, type, value) ||
			!dbus_message_iter_close_container(&iter, &variant)) {
		dbus_message_iter_abandon_container(&iter, &variant);
		goto error_message_args;
	}

	INF("%s.SetProperty(%s)", OFONO_CALL_VOL_IFACE, property);
	p = _bus_object_message_send(&m->base, msg, _ofono_simple_reply, ctx);
	return p;

error_message_args:
	dbus_message_unref(msg);

error_no_dbus_message:
	if (cb)
		cb((void *)data, OFONO_ERROR_FAILED);
	free(ctx);
	return NULL;
}

OFono_Pending *ofono_mute_set(Eina_Bool mute, OFono_Simple_Cb cb,
				const void *data)
{
	dbus_bool_t dbus_mute = !!mute;

	return  _ofono_call_volume_property_set("Muted", DBUS_TYPE_BOOLEAN,
						&dbus_mute, cb, data);
}

Eina_Bool ofono_mute_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, EINA_FALSE);
	return m->muted;
}

OFono_Pending *ofono_volume_speaker_set(unsigned char volume,
					OFono_Simple_Cb cb,
					const void *data)
{

	return _ofono_call_volume_property_set("SpeakerVolume", DBUS_TYPE_BYTE,
						&volume, cb, data);
}

unsigned char ofono_volume_speaker_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, 0);
	return m->speaker_volume;
}

OFono_Pending *ofono_volume_microphone_set(unsigned char volume,
						OFono_Simple_Cb cb,
						const void *data)
{
	return _ofono_call_volume_property_set("MicrophoneVolume",
						DBUS_TYPE_BYTE, &volume, cb,
						data);
}

unsigned char ofono_volume_microphone_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, 0);
	return m->microphone_volume;
}

Eina_Bool ofono_voicemail_waiting_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, EINA_FALSE);
	return m->voicemail_waiting;
}

unsigned char ofono_voicemail_count_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, 0);
	return m->voicemail_count;
}

const char *ofono_voicemail_number_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, NULL);
	return m->voicemail_number;
}

OFono_USSD_State ofono_ussd_state_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, OFONO_USSD_STATE_IDLE);
	return m->ussd_state;
}

const char *ofono_service_center_address_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, NULL);
	return m->serv_center_addr;
}

Eina_Bool ofono_use_delivery_reports_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, EINA_FALSE);
	return m->use_delivery_reports;
}

const char *ofono_message_bearer_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, NULL);
	return m->msg_bearer;
}

const char *ofono_message_alphabet_get(void)
{
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, NULL);
	return m->msg_alphabet;
}

OFono_Sent_SMS_State ofono_sent_sms_state_get(const OFono_Sent_SMS *sms)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(sms, OFONO_SENT_SMS_STATE_FAILED);
	return sms->state;
}

const char *ofono_sent_sms_destination_get(const OFono_Sent_SMS *sms)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(sms, NULL);
	return sms->destination;
}

const char *ofono_sent_sms_message_get(const OFono_Sent_SMS *sms)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(sms, NULL);
	return sms->message;
}

time_t ofono_sent_sms_timestamp_get(const OFono_Sent_SMS *sms)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(sms, 0);
	return sms->timestamp;
}

OFono_Pending *ofono_sent_sms_cancel(OFono_Sent_SMS *sms, OFono_Simple_Cb cb,
					const void *data)
{
	OFono_Simple_Cb_Context *ctx = NULL;
	OFono_Error err = OFONO_ERROR_OFFLINE;
	OFono_Pending *p;
	DBusMessage *msg;

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(bus_id, sms->base.path,
						OFONO_PREFIX "Message",
						"Cancel");
	if (!msg)
		goto error;

	INF("Cancel(%s)", sms->base.path);
	p = _bus_object_message_send(&sms->base, msg, _ofono_simple_reply, ctx);
	return p;

error:
	if (cb)
		cb((void *)data, err);
	free(ctx);
	return NULL;
}

static void _ofono_sms_send_reply(void *data, DBusMessage *msg, DBusError *err)
{
	OFono_Sent_SMS_Cb_Context *ctx = data;
	OFono_Sent_SMS *sms = NULL;
	OFono_Error oe = OFONO_ERROR_NONE;

	if (!msg) {
		DBG("%s: %s", err->name, err->message);
		oe = _ofono_error_parse(err->name);
	} else {
		DBusError e;
		const char *path;
		dbus_error_init(&e);
		if (!dbus_message_get_args(msg, &e, DBUS_TYPE_OBJECT_PATH,
						&path, DBUS_TYPE_INVALID)) {
			ERR("Could not get SendMessage reply: %s: %s",
				e.name, e.message);
			dbus_error_free(&e);
			oe = OFONO_ERROR_FAILED;
		} else {
			sms = eina_hash_find(ctx->modem->sent_sms, path);
			DBG("path=%s, existing sms=%p", path, sms);
			if (!sms) {
				sms = _sent_sms_pending_add(ctx->modem, path,
								ctx);
				if (sms) {
					/* ctx->cb will be dispatched on
					 * MessageAdded signal handler.
					 */
					return;
				}
			}

			ERR("Could not find sms %s", path);
			oe = OFONO_ERROR_FAILED;
		}
	}

	if (ctx->cb)
		ctx->cb((void *)ctx->data, oe, sms);

	eina_stringshare_del(ctx->destination);
	eina_stringshare_del(ctx->message);
	free(ctx);
}

OFono_Pending *ofono_sms_send(const char *number, const char *message,
				OFono_Sent_SMS_Cb cb, const void *data)
{
	OFono_Sent_SMS_Cb_Context *ctx = NULL;
	OFono_Error err = OFONO_ERROR_OFFLINE;
	OFono_Pending *p;
	DBusMessage *msg;
	OFono_Modem *m = _modem_selected_get();
	EINA_SAFETY_ON_NULL_GOTO(m, error);
	EINA_SAFETY_ON_NULL_GOTO(number, error);
	EINA_SAFETY_ON_NULL_GOTO(message, error);

	if ((m->interfaces & OFONO_API_MSG) == 0)
		goto error;
	err = OFONO_ERROR_FAILED;

	ctx = calloc(1, sizeof(OFono_Sent_SMS_Cb_Context));
	EINA_SAFETY_ON_NULL_GOTO(ctx, error);
	ctx->cb = cb;
	ctx->data = data;
	ctx->modem = m;
	ctx->destination = eina_stringshare_add(number);
	ctx->message = eina_stringshare_add(message);

	msg = dbus_message_new_method_call(
		bus_id, m->base.path, OFONO_PREFIX OFONO_MSG_IFACE,
		"SendMessage");
	if (!msg)
		goto error_setup;

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &number,
					DBUS_TYPE_STRING, &message,
					DBUS_TYPE_INVALID))
		goto error_message;

	INF("SendMessage(%s, %s)", number, message);
	p = _bus_object_message_send(&m->base, msg, _ofono_sms_send_reply, ctx);
	return p;

error_message:
	dbus_message_unref(msg);
error_setup:
	eina_stringshare_del(ctx->destination);
	eina_stringshare_del(ctx->message);
error:
	if (cb)
		cb((void *)data, err, NULL);
	free(ctx);
	return NULL;
}

OFono_Pending *ofono_tones_send(const char *tones,
						OFono_Simple_Cb cb,
						const void *data)
{
	OFono_Pending *p;
	DBusMessage *msg;
	OFono_Simple_Cb_Context *ctx = NULL;
	OFono_Modem *m = _modem_selected_get();

	EINA_SAFETY_ON_NULL_GOTO(m, error_no_dbus_message);

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error_no_dbus_message);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(
				bus_id, m->base.path,
				OFONO_PREFIX OFONO_VOICE_IFACE,
				"SendTones");
	if (!msg)
		goto error_no_dbus_message;

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &tones,
				      DBUS_TYPE_INVALID))
		goto error_message_args;

	INF("SendTones(%s)", tones);
	p = _bus_object_message_send(&m->base, msg, _ofono_simple_reply, ctx);
	return p;

error_message_args:
	dbus_message_unref(msg);

error_no_dbus_message:
	if (cb)
		cb((void *)data, OFONO_ERROR_FAILED);
	free(ctx);
	return NULL;
}

OFono_Pending *ofono_multiparty_create(OFono_Simple_Cb cb,
					const void *data)
{
	return _ofono_multiparty("CreateMultiparty", cb, data);
}

OFono_Pending *ofono_multiparty_hangup(OFono_Simple_Cb cb, const void *data)
{
	return _ofono_multiparty("HangupMultiparty", cb, data);
}

OFono_Pending *ofono_private_chat(OFono_Call *c, OFono_Simple_Cb cb,
					const void *data)
{
	OFono_Pending *p;
	DBusMessage *msg;
	OFono_Simple_Cb_Context *ctx = NULL;
	OFono_Modem *m = _modem_selected_get();

	EINA_SAFETY_ON_NULL_GOTO(m, error_no_message);
	EINA_SAFETY_ON_NULL_GOTO(c, error_no_message);

	if (cb) {
		ctx = calloc(1, sizeof(OFono_Simple_Cb_Context));
		EINA_SAFETY_ON_NULL_GOTO(ctx, error_no_message);
		ctx->cb = cb;
		ctx->data = data;
	}

	msg = dbus_message_new_method_call(
				bus_id, m->base.path,
				OFONO_PREFIX OFONO_VOICE_IFACE,
				"PrivateChat");

	if (!msg)
		goto error_no_message;

	if (!dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH,
					&(c->base.path), DBUS_TYPE_INVALID))
		goto error_message_append;

	INF("PrivateChat(%s)", c->base.path);
	p = _bus_object_message_send(&m->base, msg, _ofono_simple_reply, ctx);
	return p;

error_message_append:
	dbus_message_unref(msg);
error_no_message:
	if (cb)
		cb((void *)data, OFONO_ERROR_FAILED);
	free(ctx);
	return NULL;
}

static OFono_Callback_List_Modem_Node * _ofono_callback_modem_node_create(
	void (*cb)(void *data),const void *data)
{
	OFono_Callback_List_Modem_Node *node_new;

	node_new = calloc(1, sizeof(OFono_Callback_List_Modem_Node));
	EINA_SAFETY_ON_NULL_RETURN_VAL(node_new, NULL);

	node_new->cb_data = data;
	node_new->cb = cb;

	return node_new;
}

OFono_Callback_List_Modem_Node *
ofono_modem_conected_cb_add(void (*cb)(void *data), const void *data)
{
	OFono_Callback_List_Modem_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = _ofono_callback_modem_node_create(cb, data);
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	cbs_modem_connected = eina_inlist_append(cbs_modem_connected,
							EINA_INLIST_GET(node));

	return node;
}

OFono_Callback_List_Modem_Node *
ofono_modem_disconnected_cb_add(void (*cb)(void *data), const void *data)
{
	OFono_Callback_List_Modem_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = _ofono_callback_modem_node_create(cb, data);
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	cbs_modem_disconnected = eina_inlist_append(cbs_modem_disconnected,
							EINA_INLIST_GET(node));

	return node;
}

OFono_Callback_List_Modem_Node *
ofono_modem_changed_cb_add(void (*cb)(void *data), const void *data)
{
	OFono_Callback_List_Modem_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = _ofono_callback_modem_node_create(cb, data);
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	cbs_modem_changed = eina_inlist_append(cbs_modem_changed,
						EINA_INLIST_GET(node));

	return node;
}

static void _ofono_callback_modem_list_delete(Eina_Inlist **list,
					OFono_Callback_List_Modem_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(*list);
	*list = eina_inlist_remove(*list, EINA_INLIST_GET(node));
	free(node);
}

void ofono_modem_changed_cb_del(OFono_Callback_List_Modem_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	_ofono_callback_modem_list_delete(&cbs_modem_changed, node);
}

void ofono_modem_disconnected_cb_del(OFono_Callback_List_Modem_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	_ofono_callback_modem_list_delete(&cbs_modem_disconnected, node);
}

void ofono_modem_connected_cb_del(OFono_Callback_List_Modem_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	_ofono_callback_modem_list_delete(&cbs_modem_connected, node);
}

static OFono_Callback_List_Call_Node *_ofono_callback_call_node_create(
	void (*cb)(void *data, OFono_Call *call),const void *data)
{
	OFono_Callback_List_Call_Node *node;

	node = calloc(1, sizeof(OFono_Callback_List_Call_Node));
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	node->cb_data = data;
	node->cb = cb;

	return node;
}

static OFono_Callback_List_Call_Disconnected_Node *
_ofono_callback_call_disconnected_node_create(
	void (*cb)(void *data, OFono_Call *call, const char *reason),
	const void *data)
{
	OFono_Callback_List_Call_Disconnected_Node *node;

	node = calloc(1, sizeof(OFono_Callback_List_Call_Disconnected_Node));
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	node->cb_data = data;
	node->cb = cb;

	return node;
}

static OFono_Callback_List_USSD_Notify_Node *
_ofono_callback_ussd_notify_node_create(
	void (*cb)(void *data, Eina_Bool needs_reply, const char *msg),
	const void *data)
{
	OFono_Callback_List_USSD_Notify_Node *node;

	node = calloc(1, sizeof(OFono_Callback_List_USSD_Notify_Node));
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	node->cb_data = data;
	node->cb = cb;

	return node;
}

OFono_Callback_List_Call_Node *ofono_call_added_cb_add(
	void (*cb)(void *data,OFono_Call *call), const void *data)
{
	OFono_Callback_List_Call_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = _ofono_callback_call_node_create(cb, data);
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	cbs_call_added = eina_inlist_append(cbs_call_added,
						EINA_INLIST_GET(node));

	return node;
}

OFono_Callback_List_Call_Node *ofono_call_removed_cb_add(
	void (*cb)(void *data, OFono_Call *call), const void *data)
{
	OFono_Callback_List_Call_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = _ofono_callback_call_node_create(cb, data);
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	cbs_call_removed = eina_inlist_append(cbs_call_removed,
						EINA_INLIST_GET(node));

	return node;
}

OFono_Callback_List_Call_Node *ofono_call_changed_cb_add(
	void (*cb)(void *data, OFono_Call *call), const void *data)
{
	OFono_Callback_List_Call_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = _ofono_callback_call_node_create(cb, data);
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	cbs_call_changed = eina_inlist_append(cbs_call_changed,
						EINA_INLIST_GET(node));

	return node;
}

OFono_Callback_List_Call_Disconnected_Node *ofono_call_disconnected_cb_add(
	void (*cb)(void *data, OFono_Call *call, const char *reason),
	const void *data)
{
	OFono_Callback_List_Call_Disconnected_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = _ofono_callback_call_disconnected_node_create(cb, data);
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	cbs_call_disconnected = eina_inlist_append(cbs_call_disconnected,
							EINA_INLIST_GET(node));

	return node;
}

OFono_Callback_List_USSD_Notify_Node *ofono_ussd_notify_cb_add(
	void (*cb)(void *data, Eina_Bool needs_reply, const char *msg),
	const void *data)
{
	OFono_Callback_List_USSD_Notify_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = _ofono_callback_ussd_notify_node_create(cb, data);
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	cbs_ussd_notify = eina_inlist_append(cbs_ussd_notify,
						EINA_INLIST_GET(node));

	return node;
}

static void _ofono_callback_call_list_delete(Eina_Inlist **list,
					OFono_Callback_List_Call_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(*list);
	*list = eina_inlist_remove(*list, EINA_INLIST_GET(node));
	free(node);
}

void ofono_call_changed_cb_del(OFono_Callback_List_Call_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	_ofono_callback_call_list_delete(&cbs_call_changed, node);
}

void ofono_call_disconnected_cb_del(
	OFono_Callback_List_Call_Disconnected_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	EINA_SAFETY_ON_NULL_RETURN(cbs_call_disconnected);
	cbs_call_disconnected = eina_inlist_remove(cbs_call_disconnected,
							EINA_INLIST_GET(node));
	free(node);
}

void ofono_ussd_notify_cb_del(OFono_Callback_List_USSD_Notify_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	EINA_SAFETY_ON_NULL_RETURN(cbs_ussd_notify);
	cbs_ussd_notify = eina_inlist_remove(cbs_ussd_notify,
						EINA_INLIST_GET(node));
	free(node);
}

void ofono_call_added_cb_del(OFono_Callback_List_Call_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	_ofono_callback_call_list_delete(&cbs_call_added, node);
}

void ofono_call_removed_cb_del(OFono_Callback_List_Call_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	_ofono_callback_call_list_delete(&cbs_call_removed, node);
}

OFono_Callback_List_Sent_SMS_Node *
ofono_sent_sms_changed_cb_add(OFono_Sent_SMS_Cb cb, const void *data)
{
	OFono_Callback_List_Sent_SMS_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = calloc(1, sizeof(OFono_Callback_List_Sent_SMS_Node));
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);
	node->cb = cb;
	node->cb_data = data;

	cbs_sent_sms_changed = eina_inlist_append(cbs_sent_sms_changed,
							EINA_INLIST_GET(node));

	return node;
}

void ofono_sent_sms_changed_cb_del(OFono_Callback_List_Sent_SMS_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	cbs_sent_sms_changed = eina_inlist_remove(cbs_sent_sms_changed,
							EINA_INLIST_GET(node));
	free(node);
}

OFono_Callback_List_Incoming_SMS_Node *
ofono_incoming_sms_cb_add(OFono_Incoming_SMS_Cb cb, const void *data)
{
	OFono_Callback_List_Incoming_SMS_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = calloc(1, sizeof(OFono_Callback_List_Incoming_SMS_Node));
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);
	node->cb = cb;
	node->cb_data = data;

	cbs_incoming_sms = eina_inlist_append(cbs_incoming_sms,
						EINA_INLIST_GET(node));

	return node;
}

void ofono_incoming_sms_cb_del(OFono_Callback_List_Incoming_SMS_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	cbs_incoming_sms = eina_inlist_remove(cbs_incoming_sms,
						EINA_INLIST_GET(node));
	free(node);
}

Eina_Bool ofono_voice_is_online(void)
{
	OFono_Modem *m = _modem_selected_get();

	/* The modem is expected to be NULL here, because maybe
	 * OFono isn't up yet.
	 */
	if (!m)
		return EINA_FALSE;

	if (m->interfaces & OFONO_API_VOICE)
		return EINA_TRUE;

	return EINA_FALSE;
}
