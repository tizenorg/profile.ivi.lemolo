#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "log.h"
#include "gui.h"
#include "ofono.h"

static E_DBus_Connection *bus_conn = NULL;
static E_DBus_Object *bus_obj = NULL;
static E_DBus_Interface *bus_iface = NULL;

#define RC_IFACE "org.tizen.dialer.Control"
#define RC_PATH "/"
#define RC_SIG_CALL_ADDED "AddedCall"
#define RC_SIG_CALL_REMOVED "RemovedCall"

static const char *rc_service = NULL;
static OFono_Callback_List_Modem_Node *modem_changed_node = NULL;
static OFono_Callback_List_Call_Node *call_added = NULL;
static OFono_Callback_List_Call_Node *call_removed = NULL;
static OFono_Callback_List_Call_Node *call_changed = NULL;
static DBusMessage *pending_dial = NULL;
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
	DBusError err;
	const char *number;
	dbus_bool_t do_auto;
	DBusMessage *reply;

	if (!ofono_voice_is_online() || !pending_dial)
		return;

	dbus_error_init(&err);
	dbus_message_get_args(pending_dial, &err, DBUS_TYPE_STRING, &number,
				DBUS_TYPE_BOOLEAN, &do_auto, DBUS_TYPE_INVALID);

	if (dbus_error_is_set(&err)) {
		ERR("Could not parse message: %s: %s", err.name, err.message);
		reply = dbus_message_new_error(pending_dial, err.name,
						err.message);
		goto reply_send;
	}

	_dial_number(number, do_auto);
	reply = dbus_message_new_method_return(pending_dial);
reply_send:
	e_dbus_message_send(bus_conn, reply, NULL, -1, NULL);
	dbus_message_unref(pending_dial);
	dbus_message_unref(reply);
	pending_dial = NULL;
}

static DBusMessage *
_rc_activate(E_DBus_Object *obj __UNUSED__, DBusMessage *msg)
{
	INF("Remotely activated!");
	gui_activate();
	return dbus_message_new_method_return(msg);
}

static DBusMessage *
_rc_dial(E_DBus_Object *obj __UNUSED__, DBusMessage *msg)
{
	DBusError err;
	dbus_bool_t do_auto;
	const char *number;

	if (!ofono_voice_is_online()) {
		if (pending_dial)
			dbus_message_unref(pending_dial);
		pending_dial = dbus_message_ref(msg);
		return NULL;
	}

	dbus_error_init(&err);
	dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &number,
				DBUS_TYPE_BOOLEAN, &do_auto, DBUS_TYPE_INVALID);
	if (dbus_error_is_set(&err)) {
		ERR("Could not parse message: %s: %s", err.name, err.message);
		return dbus_message_new_error(msg, err.name, err.message);
	}
	_dial_number(number, do_auto);
	return dbus_message_new_method_return(msg);
}

static DBusMessage *_rc_hangup_call(E_DBus_Object *obj __UNUSED__,
						DBusMessage *msg)
{
	if (!waiting) {
		return dbus_message_new_error(msg,
					"org.tizen.dialer.error.NotAvailable",
					"No calls available");
	}

	ofono_call_hangup(waiting, NULL, NULL);

	return dbus_message_new_method_return(msg);
}

static DBusMessage *_rc_answer_call(E_DBus_Object *obj __UNUSED__,
						DBusMessage *msg)
{
	OFono_Call_State state;

	if (!waiting) {
		return dbus_message_new_error(msg,
					"org.tizen.dialer.error.NotAvailable",
					"No calls available");
	}

	state = ofono_call_state_get(waiting);
	if (state == OFONO_CALL_STATE_INCOMING)
		ofono_call_answer(waiting, NULL, NULL);
	else if (state == OFONO_CALL_STATE_WAITING)
		ofono_hold_and_answer(NULL, NULL);

	return dbus_message_new_method_return(msg);
}

static void _rc_signal_reply(void *data __UNUSED__,
					DBusMessage *msg __UNUSED__,
					DBusError *err)
{
	if (dbus_error_is_set(err)) {
		CRITICAL("Failed to send a signal: %s: %s",
				err->name, err->message);
		return;
	}

	DBG("Signal was sent successfully");
}

static void _new_call_sig_emit(OFono_Call *call)
{
	DBusMessage *msg;
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

	msg = dbus_message_new_signal(RC_PATH, RC_IFACE, RC_SIG_CALL_ADDED);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &img,
					DBUS_TYPE_STRING, &line_id,
					DBUS_TYPE_STRING, &name,
					DBUS_TYPE_STRING, &type,
					DBUS_TYPE_INVALID)) {
		ERR("Could not append msg args.");
		goto err_args;
	}

	e_dbus_message_send(bus_conn, msg, _rc_signal_reply, -1, NULL);
err_args:
	dbus_message_unref(msg);
}

static DBusMessage *_rc_waiting_call_get(E_DBus_Object *obj __UNUSED__,
						DBusMessage *msg)
{
	DBusMessage *ret;
	const char *line_id, *name = "", *type = "", *img = "";
	Contact_Info *c_info;


	if (!waiting) {
		return dbus_message_new_error(msg,
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

	ret = dbus_message_new_method_return(msg);
	EINA_SAFETY_ON_NULL_GOTO(ret, err_ret);

	if (!dbus_message_append_args(ret, DBUS_TYPE_STRING, &img,
					DBUS_TYPE_STRING, &line_id,
					DBUS_TYPE_STRING, &name,
					DBUS_TYPE_STRING, &type,
					DBUS_TYPE_INVALID)) {
		ERR("Could not append msg args.");
		goto err_args;
	}

	return ret;

err_args:
	dbus_message_unref(ret);

err_ret:
	return dbus_message_new_error(msg,
					"org.tizen.dialer.error.Error",
					"Could not create a reply");
}

static void _rc_object_register(void)
{
	bus_obj = e_dbus_object_add(bus_conn, RC_PATH, NULL);
	if (!bus_obj) {
		CRITICAL("Could not create "RC_PATH" DBus object.");
		return;
	}
	bus_iface = e_dbus_interface_new(RC_IFACE);
	e_dbus_object_interface_attach(bus_obj, bus_iface);

#define IF_ADD(name, par, ret, cb)		\
	e_dbus_interface_method_add(bus_iface, name, par, ret, cb)

	IF_ADD("Activate", "", "", _rc_activate);
	IF_ADD("Dial", "sb", "", _rc_dial);
	IF_ADD("HangupCall", "", "", _rc_hangup_call);
	IF_ADD("AnswerCall", "", "", _rc_answer_call);
	IF_ADD("GetAvailableCall", "", "ssss", _rc_waiting_call_get);
#undef IF_ADD

	e_dbus_interface_signal_add(bus_iface, RC_SIG_CALL_ADDED,
					"ssss");
	e_dbus_interface_signal_add(bus_iface, RC_SIG_CALL_REMOVED,
					"");
}

static void _rc_activate_existing_reply(void *data __UNUSED__,
					DBusMessage *msg __UNUSED__,
					DBusError *err)
{
	if (dbus_error_is_set(err)) {
		CRITICAL("Failed to activate existing dialer: %s: %s",
				err->name, err->message);
		_app_exit_code = EXIT_FAILURE;
		ecore_main_loop_quit();
		return;
	}

	INF("Activated the existing dialer!");
	ecore_main_loop_quit();
}

static void _rc_activate_existing(void)
{
	DBusMessage *msg = dbus_message_new_method_call(
		rc_service, RC_PATH, RC_IFACE, "Activate");
	e_dbus_message_send(bus_conn, msg,  _rc_activate_existing_reply,
				-1, NULL);
	dbus_message_unref(msg);
}

static void _rc_request_name_reply(void *data __UNUSED__, DBusMessage *msg,
					DBusError *err)
{
	DBusError e;
	dbus_uint32_t t;

	if (!msg) {
		if (err)
			WRN("%s: %s", err->name, err->message);
		else
			WRN("No message");
		_rc_activate_existing();
		return;
	}

	dbus_error_init(&e);
	dbus_message_get_args(msg, &e, DBUS_TYPE_UINT32, &t, DBUS_TYPE_INVALID);
	if (t == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		_rc_object_register();
		gui_activate();
	} else {
		WRN("Dialer already running! Activate it!");
		_rc_activate_existing();
	}
}

static void _removed_signal_send(void)
{
	DBusMessage *msg;

	msg = dbus_message_new_signal(RC_PATH, RC_IFACE, RC_SIG_CALL_REMOVED);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	e_dbus_message_send(bus_conn, msg, _rc_signal_reply, -1, NULL);

	dbus_message_unref(msg);
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

	if (!elm_need_e_dbus()) {
		CRITICAL("Elementary does not support DBus.");
		return EINA_FALSE;
	}

#ifdef HAVE_TIZEN
	/* NOTE: Tizen is stupid and does not have a session bus.  at
	 * least not for user "app". Moreover the dialer is started by
	 * user "root" :-(
	 */
	INF("Running on System bus");
	bus_conn = e_dbus_bus_get(DBUS_BUS_SYSTEM);
#else
	INF("Running on Session bus");
	bus_conn = e_dbus_bus_get(DBUS_BUS_SESSION);
#endif
	if (!bus_conn) {
		CRITICAL("Could not get DBus Bus");
		return EINA_FALSE;
	}

	e_dbus_request_name(bus_conn, rc_service, DBUS_NAME_FLAG_DO_NOT_QUEUE,
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
	if (bus_obj)
		e_dbus_object_free(bus_obj);
	if (bus_iface)
		e_dbus_interface_unref(bus_iface);

	ofono_modem_changed_cb_del(modem_changed_node);
	ofono_call_added_cb_del(call_added);
	ofono_call_removed_cb_del(call_removed);
	ofono_call_changed_cb_del(call_changed);

	if (pending_dial)
		dbus_message_unref(pending_dial);

	bus_conn = NULL;
}
