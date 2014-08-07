#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <appcore-efl.h>
#include <stdio.h>
#include <stdlib.h>
#include <Evas.h>
#include <Eldbus.h>

#define APP_NAME "org.tizen.call"
#define BUS_NAME "org.tizen.dialer"
#define PATH "/"
#define RC_IFACE "org.tizen.dialer.Control"
#define FREEDESKTOP_IFACE "org.freedesktop.DBus"

static Eldbus_Connection *bus_conn = NULL;

static void _activate_cb(void *data __UNUSED__, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	const char *err_name, *err_message;

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		fprintf(stderr, "Dialer activate error %s: %s",	err_name, err_message);
		return;
	}

	elm_exit();
}

static void _bring_to_foreground(void)
{
	Eldbus_Message *msg;

	msg = eldbus_message_method_call_new(BUS_NAME, PATH, RC_IFACE, "Activate");

	EINA_SAFETY_ON_NULL_RETURN(msg);

	eldbus_connection_send(bus_conn, msg, _activate_cb, NULL, -1);
}

static void _start_cb(void *data __UNUSED__, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	const char *err_name, *err_message;

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		fprintf(stderr, "Dialer start error %s: %s",	err_name, err_message);
		elm_exit();
		return;
	}

	_bring_to_foreground();
}

static void _has_owner_cb(void *data __UNUSED__, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	Eina_Bool online;
	const char *err_name, *err_message;

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		fprintf(stderr, "Error %s: %s",	err_name, err_message);
		elm_exit();
		return;
	}

	if (!eldbus_message_arguments_get(msg, "b", &online)) {
		fprintf(stderr, "Could not get arguments");
		return;
	}

	if (!online) {
		Eldbus_Message *send_msg = eldbus_message_method_call_new(
			BUS_NAME, PATH, FREEDESKTOP_IFACE,
			"StartServiceByName");

		if (!send_msg)
			return;

		if (!eldbus_message_arguments_append(send_msg, "si", BUS_NAME, 0))
			return;

		eldbus_connection_send(bus_conn, send_msg, _start_cb, NULL, -1);
	} else {
		_bring_to_foreground();
	}
}

static Eina_Bool _dbus_init(void)
{
	Eldbus_Message *msg;
	char *bus_name = BUS_NAME;

	bus_conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
	if (!bus_conn) {
		fprintf(stderr, "Could not fetch the DBus session");
		return EINA_FALSE;
	}

	msg = eldbus_message_method_call_new(ELDBUS_FDO_BUS, ELDBUS_FDO_PATH,
						ELDBUS_FDO_INTERFACE,
						"NameHasOwner");

	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, EINA_FALSE);

	if (!eldbus_message_arguments_append(msg, "s", bus_name))
		goto err_msg;

	eldbus_connection_send(bus_conn, msg, _has_owner_cb, NULL, -1);

	return EINA_TRUE;
err_msg:
	eldbus_message_unref(msg);
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

	eldbus_init();
	EINA_SAFETY_ON_FALSE_RETURN_VAL(_dbus_init(), -1);
	r = appcore_efl_main(APP_NAME, &argc, &argv, &ops);
	eldbus_connection_unref(bus_conn);

	eldbus_shutdown();
	return r;
}
