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

#define RC_IFACE "org.tizen.messages.Control"
#define RC_PATH "/"

static const char *rc_service = NULL;
static OFono_Callback_List_Modem_Node *modem_changed_node = NULL;
static DBusMessage *pending_send = NULL;

static void _send_message(const char *number, const char *message,
				Eina_Bool do_auto)
{
	INF("send '%s' '%s' auto=%d!", number, message, do_auto);
	gui_activate();
	gui_send(number, message, do_auto);
}

static void _modem_changed_cb(void *data __UNUSED__)
{
	DBusError err;
	const char *number, *message;
	dbus_bool_t do_auto;
	DBusMessage *reply;

	if (!ofono_voice_is_online() || !pending_send)
		return;

	dbus_error_init(&err);
	dbus_message_get_args(pending_send, &err,
				DBUS_TYPE_STRING, &number,
				DBUS_TYPE_STRING, &message,
				DBUS_TYPE_BOOLEAN, &do_auto, DBUS_TYPE_INVALID);

	if (dbus_error_is_set(&err)) {
		ERR("Could not parse message: %s: %s", err.name, err.message);
		reply = dbus_message_new_error(pending_send, err.name,
						err.message);
		goto reply_send;
	}

	_send_message(number, message, do_auto);
	reply = dbus_message_new_method_return(pending_send);
reply_send:
	e_dbus_message_send(bus_conn, reply, NULL, -1, NULL);
	dbus_message_unref(pending_send);
	dbus_message_unref(reply);
	pending_send = NULL;
}

static DBusMessage *
_rc_activate(E_DBus_Object *obj __UNUSED__, DBusMessage *msg)
{
	INF("Remotely activated!");
	gui_activate();
	return dbus_message_new_method_return(msg);
}

static DBusMessage *
_rc_send(E_DBus_Object *obj __UNUSED__, DBusMessage *msg)
{
	DBusError err;
	dbus_bool_t do_auto;
	const char *number, *message;

	if (!ofono_voice_is_online()) {
		if (pending_send)
			dbus_message_unref(pending_send);
		pending_send = dbus_message_ref(msg);
		return NULL;
	}

	dbus_error_init(&err);
	dbus_message_get_args(msg, &err,
				DBUS_TYPE_STRING, &number,
				DBUS_TYPE_STRING, &message,
				DBUS_TYPE_BOOLEAN, &do_auto, DBUS_TYPE_INVALID);
	if (dbus_error_is_set(&err)) {
		ERR("Could not parse message: %s: %s", err.name, err.message);
		return dbus_message_new_error(msg, err.name, err.message);
	}
	_send_message(number, message, do_auto);
	return dbus_message_new_method_return(msg);
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
	IF_ADD("Send", "ssb", "", _rc_send);
#undef IF_ADD
}

static void _rc_activate_existing_reply(void *data __UNUSED__,
					DBusMessage *msg __UNUSED__,
					DBusError *err)
{
	if (dbus_error_is_set(err)) {
		CRITICAL("Failed to activate existing messages: %s: %s",
				err->name, err->message);
		_app_exit_code = EXIT_FAILURE;
		ecore_main_loop_quit();
		return;
	}

	INF("Activated the existing messages!");
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
		WRN("Messages already running! Activate it!");
		_rc_activate_existing();
	}
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
	 * least not for user "app". Moreover the messages is started by
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

	return EINA_TRUE;
}

void rc_shutdown(void)
{
	if (bus_obj)
		e_dbus_object_free(bus_obj);
	if (bus_iface)
		e_dbus_interface_unref(bus_iface);

	ofono_modem_changed_cb_del(modem_changed_node);

	if (pending_send)
		dbus_message_unref(pending_send);

	bus_conn = NULL;
}
