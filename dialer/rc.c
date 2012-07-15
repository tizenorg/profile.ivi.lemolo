#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "log.h"
#include "gui.h"

static E_DBus_Connection *bus_conn = NULL;
static E_DBus_Object *bus_obj = NULL;
static E_DBus_Interface *bus_iface = NULL;

#define RC_SERVICE "org.tizen.dialer"
#define RC_IFACE "org.tizen.dialer.Control"
#define RC_PATH "/"

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

	dbus_error_init(&err);
	dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &number,
				DBUS_TYPE_BOOLEAN, &do_auto, DBUS_TYPE_INVALID);
	if (dbus_error_is_set(&err)) {
		ERR("Could not parse message: %s: %s", err.name, err.message);
		return dbus_message_new_error(msg, err.name, err.message);
	}

	INF("dial '%s' auto=%d!", number, do_auto);
	gui_activate();
	gui_call_exit();
	gui_number_set(number, do_auto);
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
	IF_ADD("Dial", "sb", "", _rc_dial);
#undef IF_ADD
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
		RC_SERVICE, RC_PATH, RC_IFACE, "Activate");
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

Eina_Bool rc_init(void)
{
	if (!elm_need_e_dbus()) {
		CRITICAL("Elementary does not support DBus.");
		return EINA_FALSE;
	}

	bus_conn = e_dbus_bus_get(DBUS_BUS_SESSION);
	if (!bus_conn) {
		CRITICAL("Could not get DBus System Bus");
		return EINA_FALSE;
	}

	e_dbus_request_name(bus_conn, RC_SERVICE, DBUS_NAME_FLAG_DO_NOT_QUEUE,
				_rc_request_name_reply, NULL);
	return EINA_TRUE;
}

void rc_shutdown(void)
{
	if (bus_obj)
		e_dbus_object_free(bus_obj);
	if (bus_iface)
		e_dbus_interface_unref(bus_iface);
	bus_conn = NULL;
}
