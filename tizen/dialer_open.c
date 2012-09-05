#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <appcore-efl.h>
#include <stdio.h>
#include <stdlib.h>
#include <Evas.h>
#include <E_DBus.h>

#define APP_NAME "org.tizen.call"
#define BUS_NAME "org.tizen.dialer"
#define PATH "/"
#define IFACE "org.tizen.dialer.Control"

static E_DBus_Connection *bus_conn = NULL;

static void _activate_cb(void *data __UNUSED__, DBusMessage *msg __UNUSED__,
				DBusError *error)
{
	if (dbus_error_is_set(error)) {
		fprintf(stderr, "Error: %s %s", error->name, error->message);
	}
	elm_exit();
}

static void _bring_to_foreground(void)
{
	DBusMessage *msg;

	msg = dbus_message_new_method_call(BUS_NAME, PATH, IFACE, "Activate");

	EINA_SAFETY_ON_NULL_RETURN(msg);

	e_dbus_message_send(bus_conn, msg, _activate_cb, -1, NULL);
	dbus_message_unref(msg);
}

static void _start_cb(void *data __UNUSED__, DBusMessage *msg __UNUSED__,
			DBusError *error)
{
	if (dbus_error_is_set(error)) {
		fprintf(stderr, "Error: %s %s", error->name, error->message);
		elm_exit();
		return;
	}

	_bring_to_foreground();
}

static void _has_owner_cb(void *data __UNUSED__, DBusMessage *msg,
				DBusError *error)
{
	dbus_bool_t online;
	DBusError err;

	if (dbus_error_is_set(error)) {
		fprintf(stderr, "Error: %s %s", error->name, error->message);
		elm_exit();
		return;
	}
	dbus_error_init(&err);
	dbus_message_get_args(msg, &err, DBUS_TYPE_BOOLEAN, &online,
				DBUS_TYPE_INVALID);

	if (!online)
		e_dbus_start_service_by_name(bus_conn, BUS_NAME, 0, _start_cb, NULL);
	else
		_bring_to_foreground();
}

static Eina_Bool _dbus_init(void)
{
	DBusMessage *msg;
	char *bus_name = BUS_NAME;

	bus_conn = e_dbus_bus_get(DBUS_BUS_SYSTEM);
	if (!bus_conn) {
		fprintf(stderr, "Could not fetch the DBus session");
		return EINA_FALSE;
	}

	msg = dbus_message_new_method_call(E_DBUS_FDO_BUS, E_DBUS_FDO_PATH,
						E_DBUS_FDO_INTERFACE,
						"NameHasOwner");

	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, EINA_FALSE);

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &bus_name,
					DBUS_TYPE_INVALID))
		goto err_msg;

	e_dbus_message_send(bus_conn, msg, _has_owner_cb, -1, NULL);
	dbus_message_unref(msg);

	return EINA_TRUE;
err_msg:
	dbus_message_unref(msg);
	return EINA_FALSE;
}

static int _create(void *data __UNUSED__)
{
	return 0;
}

static int _reset(bundle *b __UNUSED__, void *data __UNUSED__)
{
	return 0;
}

static int _resume(void *data __UNUSED__)
{
	return 0;
}

static int _pause(void *data __UNUSED__)
{
	return 0;
}

static int _terminate(void *data __UNUSED__)
{
	return 0;
}

int main(int argc __UNUSED__, char **argv __UNUSED__)
{
	int r;
	struct appcore_ops ops = {
		.create = _create,
		.resume = _resume,
		.reset = _reset,
		.pause = _pause,
		.terminate = _terminate,
	};
	ops.data = NULL;

	e_dbus_init();
	EINA_SAFETY_ON_FALSE_RETURN_VAL(_dbus_init(), -1);
	r = appcore_efl_main(APP_NAME, &argc, &argv, &ops);
	e_dbus_connection_close(bus_conn);

	e_dbus_shutdown();
	return r;
}
