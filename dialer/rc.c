#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <Eldbus.h>

#ifdef HAVE_NOTIFICATION
#include <appsvc.h>
#include <notification.h>
#endif

#include "log.h"
#include "gui.h"
#include "ofono.h"

static Eldbus_Connection *bus_conn = NULL;
static Eldbus_Service_Interface *bus_iface = NULL;

#define RC_IFACE "org.tizen.dialer.Control"
#define RC_PATH "/"
#define RC_SIG_CALL_ADDED "AddedCall"
#define RC_SIG_CALL_REMOVED "RemovedCall"

enum {
   RC_SIGNAL_CALL_ADDED,
   RC_SIGNAL_CALL_REMOVED
};

static const char *rc_service = NULL;
static OFono_Callback_List_Modem_Node *modem_changed_node = NULL;
static OFono_Callback_List_Call_Node *call_added = NULL;
static OFono_Callback_List_Call_Node *call_removed = NULL;
static OFono_Callback_List_Call_Node *call_changed = NULL;
static Eldbus_Message *pending_dial = NULL;
static OFono_Call *waiting = NULL;

static void _dial_number(const char *number, Eina_Bool do_auto)
{
	INF("dial '%s' auto=%d!", number, do_auto);
	gui_activate();
	gui_call_exit();
	gui_number_set(number, do_auto);
}

static void _modem_changed_cb(void *data __UNUSED__)
{
	const char *number;
	Eina_Bool do_auto;
	Eldbus_Message *reply;

	if (!ofono_voice_is_online() || !pending_dial)
		return;

	if (!eldbus_message_arguments_get(pending_dial, "sb", &number, &do_auto)) {
		ERR("Could not pending dial arguments");
		reply = eldbus_message_error_new(pending_dial, "Pending dial", "Invalid argument");
		goto reply_send;
	}

	_dial_number(number, do_auto);
	reply = eldbus_message_method_return_new(pending_dial);
reply_send:
	eldbus_connection_send(bus_conn, reply, NULL, NULL, -1);
	eldbus_message_unref(pending_dial);
	pending_dial = NULL;
}

static Eldbus_Message *
_rc_activate(Eldbus_Object *obj __UNUSED__, Eldbus_Message *msg)
{
	INF("Remotely activated!");
	gui_activate();
	return eldbus_message_method_return_new(msg);
}

static Eldbus_Message *
_rc_dial(Eldbus_Object *obj __UNUSED__, Eldbus_Message *msg)
{
	Eina_Bool do_auto;
	const char *number;

	if (!ofono_voice_is_online()) {
		if (pending_dial)
			eldbus_message_unref(pending_dial);
		pending_dial = eldbus_message_ref(msg);
		return NULL;
	}

	if (!eldbus_message_arguments_get(pending_dial, "sb", &number, &do_auto)) {
		ERR("Could not get rc dial arguments");
		return eldbus_message_error_new(pending_dial, "RC dial", "Invalid argument");
	}

	_dial_number(number, do_auto);
	return eldbus_message_method_return_new(msg);
}

static Eldbus_Message *_rc_hangup_call(Eldbus_Object *obj __UNUSED__,
						Eldbus_Message *msg)
{
	if (!waiting) {
		return eldbus_message_error_new(msg,
					"org.tizen.dialer.error.NotAvailable",
					"No calls available");
	}

	ofono_call_hangup(waiting, NULL, NULL);

	return eldbus_message_method_return_new(msg);
}

static Eldbus_Message *_rc_answer_call(Eldbus_Object *obj __UNUSED__,
						Eldbus_Message *msg)
{
	OFono_Call_State state;

	if (!waiting) {
		return eldbus_message_error_new(msg,
					"org.tizen.dialer.error.NotAvailable",
					"No calls available");
	}

	state = ofono_call_state_get(waiting);
	if (state == OFONO_CALL_STATE_INCOMING)
		ofono_call_answer(waiting, NULL, NULL);
	else if (state == OFONO_CALL_STATE_WAITING)
		ofono_hold_and_answer(NULL, NULL);

	return eldbus_message_method_return_new(msg);
}

static void _new_call_sig_emit(OFono_Call *call)
{
	Eldbus_Message *msg;
	const char *line_id, *name = "", *type = "", *img = "";
	Contact_Info *c_info;

	line_id = ofono_call_line_id_get(call);
	c_info = gui_contact_search(line_id, &type);

	if (c_info) {
		name = contact_info_full_name_get(c_info);
		img = contact_info_picture_get(c_info);
		if (!img)
			img = "";
	}

	msg = eldbus_service_signal_new(bus_iface, RC_SIGNAL_CALL_ADDED);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	eldbus_message_arguments_append(msg,"ssss", img, line_id, name, type);
	eldbus_service_signal_send(bus_iface, msg);
}

#ifdef HAVE_NOTIFICATION
static void _system_notification_emit(OFono_Call *call)
{
	const char *line_id, *type="", *content = "";
	const char *title = "Incoming call";
	Contact_Info *c_info;
	notification_h noti = NULL;
	notification_error_e noti_err = NOTIFICATION_ERROR_NONE;

	noti = notification_new(NOTIFICATION_TYPE_NOTI,
				NOTIFICATION_GROUP_ID_NONE,
				NOTIFICATION_PRIV_ID_NONE);
	if (noti == NULL) {
		ERR("Failed to create notification");
		return;
	}

	noti_err = notification_set_pkgname(noti, "org.tizen.dialer");
	if (noti_err != NOTIFICATION_ERROR_NONE) {
		ERR("Failed to set pkgname: %d", noti_err);
		return;
	}

	noti_err = notification_set_text(noti, NOTIFICATION_TEXT_TYPE_TITLE,
					title,
					NULL,
					NOTIFICATION_VARIABLE_TYPE_NONE);
	if (noti_err != NOTIFICATION_ERROR_NONE) {
		ERR("Failed to set notification title: %d", noti_err);
		return;
	}

	line_id = ofono_call_line_id_get(call);
	c_info = gui_contact_search(line_id, &type);

	if (c_info) {
		content = contact_info_full_name_get(c_info);
	} else {
		content = line_id;
	}

	noti_err = notification_set_text(noti, NOTIFICATION_TEXT_TYPE_CONTENT,
					content,
					NULL,
					NOTIFICATION_VARIABLE_TYPE_NONE);
	if (noti_err != NOTIFICATION_ERROR_NONE) {
		ERR("Failed to set notification content: %d", noti_err);
		return;
	}

	bundle *b = NULL;
	b = bundle_create();
	appsvc_set_pkgname(b, "org.tizen.dialer");
	noti_err = notification_set_execute_option(noti, NOTIFICATION_EXECUTE_TYPE_SINGLE_LAUNCH, "Launch", NULL, b);
	if (noti_err != NOTIFICATION_ERROR_NONE) {
		ERR("Failed to set notification execute option: %d", noti_err);
		return;
	}

	noti_err = notification_insert(noti, NULL);
	if (noti_err != NOTIFICATION_ERROR_NONE) {
		ERR("Failed to send notification: %d", noti_err);
		return;
	}
	DBG("Sent notification: %s : %s", title, content);

	noti_err = notification_free(noti);
	if (noti_err != NOTIFICATION_ERROR_NONE) {
		ERR("Failed to free notification: %d", noti_err);
		return;
	}
}
#endif

static Eldbus_Message *_rc_waiting_call_get(Eldbus_Object *obj __UNUSED__,
						Eldbus_Message *msg)
{
	Eldbus_Message *ret;
	const char *line_id, *name = "", *type = "", *img = "";
	Contact_Info *c_info;


	if (!waiting) {
		return eldbus_message_error_new(msg,
					"org.tizen.dialer.error.NotAvailable",
					"No calls available");
	}

	line_id = ofono_call_line_id_get(waiting);
	c_info = gui_contact_search(line_id, &type);

	if (c_info) {
		name = contact_info_full_name_get(c_info);
		img = contact_info_picture_get(c_info);
		if (!img)
			img = "";
	}

	ret = eldbus_message_method_return_new(msg);
	eldbus_message_arguments_append(msg,"ssss", img, line_id, name, type);

	return ret;
}

static const Eldbus_Method rc_methods[] = {
	{ "Activate", NULL, NULL, _rc_activate, },
	{ "Dial", ELDBUS_ARGS(
		{"s", "number"},
		{"b", "do_auto"}), NULL, _rc_dial },
	{ "HangupCall", NULL, NULL, _rc_hangup_call, },
	{ "AnswerCall", NULL, NULL, _rc_answer_call, },
	{ "GetAvailableCall", NULL, ELDBUS_ARGS(
		{"s", "img"},
		{"s", "line_id"},
		{"s", "name"},
		{"s", "type"}),	_rc_waiting_call_get, ELDBUS_METHOD_FLAG_DEPRECATED },
	{ }
};

static const Eldbus_Signal rc_signals[] = {
   [RC_SIGNAL_CALL_ADDED] = { "AddedCall", ELDBUS_ARGS(
		{"s", "img"},
		{"s", "line_id"},
		{"s", "name"},
		{"s", "type"}), 0},
   [RC_SIGNAL_CALL_REMOVED] = { "RemovedCall", NULL, 0 },
   { NULL}
};

static const Eldbus_Service_Interface_Desc rc_iface_desc = {
   RC_IFACE, rc_methods, rc_signals, NULL, NULL, NULL
};

static void _rc_object_register(void)
{
	bus_iface = eldbus_service_interface_register(bus_conn,	RC_PATH, &rc_iface_desc);
}

static void _rc_activate_existing_reply(void *data __UNUSED__, const Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	const char *err_name, *err_message;

	EINA_SAFETY_ON_NULL_RETURN(msg);

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		ERR("Failed to activate existing dialer: %s: %s", err_name, err_message);
		_app_exit_code = EXIT_FAILURE;
		ecore_main_loop_quit();
		return;
	}

	INF("Activated the existing dialer!");
	ecore_main_loop_quit();
}

static void _rc_activate_existing(void)
{
	Eldbus_Message *msg = eldbus_message_method_call_new(
		rc_service, RC_PATH, RC_IFACE, "Activate");
	eldbus_connection_send(bus_conn, msg, _rc_activate_existing_reply, NULL, -1);
}

static void _rc_request_name_reply(void *data __UNUSED__, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	int t;
	const char *err_name, *err_message;

	EINA_SAFETY_ON_NULL_RETURN(msg);

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		ERR("Failed to request name: %s: %s", err_name, err_message);
		return;
	}

	if (!eldbus_message_arguments_get(msg, "u", &t)) {
		ERR("Could not get request name arguments");
		_rc_activate_existing();
		return;
	}

	if (t == ELDBUS_NAME_REQUEST_REPLY_PRIMARY_OWNER) {
		_rc_object_register();
		gui_activate();
	} else {
		WRN("Dialer already running! Activate it!");
		_rc_activate_existing();
	}
}

static void _removed_signal_send(void)
{
	Eldbus_Message *msg;

	msg = eldbus_service_signal_new(bus_iface, RC_SIGNAL_CALL_REMOVED);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	eldbus_service_signal_send(bus_iface, msg);
}

static void _rc_call_added_cb(void *data __UNUSED__, OFono_Call *call)
{
	OFono_Call_State state = ofono_call_state_get(call);

	if (state != OFONO_CALL_STATE_INCOMING &&
		state != OFONO_CALL_STATE_WAITING)
		return;

	if (waiting)
		_removed_signal_send();

	waiting = call;
	_new_call_sig_emit(call);

#ifdef HAVE_NOTIFICATION
	_system_notification_emit(call);
#endif
}

static void _rc_call_removed_cb(void *data __UNUSED__, OFono_Call *call)
{
	if (waiting != call)
		return;
	_removed_signal_send();
	waiting = NULL;
}

static void _rc_call_changed_cb(void *data __UNUSED__, OFono_Call *call)
{
	OFono_Call_State state;

	if (waiting != call)
		return;

	state = ofono_call_state_get(call);
	if (state == OFONO_CALL_STATE_INCOMING ||
		state == OFONO_CALL_STATE_WAITING)
		return;

	_removed_signal_send();
	waiting = NULL;
}

Eina_Bool rc_init(const char *service)
{
	rc_service = service;

	if (!elm_need_eldbus()) {
		CRITICAL("Elementary does not support DBus.");
		return EINA_FALSE;
	}

	INF("Running on Session bus");
	bus_conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SESSION);

	if (!bus_conn) {
		CRITICAL("Could not get DBus Bus");
		return EINA_FALSE;
	}

	eldbus_name_request(bus_conn, rc_service, ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE,
				_rc_request_name_reply, NULL);

	modem_changed_node = ofono_modem_changed_cb_add(_modem_changed_cb,
							NULL);

	call_added = ofono_call_added_cb_add(_rc_call_added_cb, NULL);
	call_removed = ofono_call_removed_cb_add(_rc_call_removed_cb, NULL);
	call_changed = ofono_call_changed_cb_add(_rc_call_changed_cb, NULL);

	return EINA_TRUE;
}

void rc_shutdown(void)
{
	if (bus_iface)
		eldbus_service_interface_unregister(bus_iface);

	ofono_modem_changed_cb_del(modem_changed_node);
	ofono_call_added_cb_del(call_added);
	ofono_call_removed_cb_del(call_removed);
	ofono_call_changed_cb_del(call_changed);

	if (pending_dial)
		eldbus_message_unref(pending_dial);

	bus_conn = NULL;
}
