#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <appcore-efl.h>
#include <stdio.h>
#include <stdlib.h>
#include <Evas.h>
#include <Eldbus.h>
#include <aul.h>
#include <appsvc.h>
#include <Eina.h>

#define APP_NAME "org.tizen.call"
#define BUS_NAME "org.tizen.dialer"
#define PATH "/"
#define RC_IFACE "org.tizen.dialer.Control"
#define FREEDESKTOP_IFACE "org.freedesktop.DBus"

static Eldbus_Connection *bus_conn = NULL;


typedef struct _Daemon {
	Eina_Bool online;
	Eina_List *pending;
} Daemon;

static void _dial_return_cb(void *data __UNUSED__, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	const char *err_name, *err_message;

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		fprintf(stderr, "Dialer return error %s: %s",	err_name, err_message);
		return;
	}

	elm_exit();
}

static void _pending_call_send(Daemon *d)
{
	Eldbus_Message *p_msg;
	EINA_LIST_FREE(d->pending, p_msg) {
		eldbus_connection_send(bus_conn, p_msg, _dial_return_cb, NULL, -1);
	}
}

static void _has_owner_cb(void *data, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	Eina_Bool online;
	Daemon *d;
	const char *err_name, *err_message;

	EINA_SAFETY_ON_NULL_RETURN(data);

	d = data;

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		fprintf(stderr, "Error %s: %s",	err_name, err_message);
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

		eldbus_connection_send(bus_conn, send_msg, NULL, NULL, -1);
	} else {
		d->online = EINA_TRUE;
		_pending_call_send(d);
	}
}

static void _name_owner_changed(void *data, Eldbus_Message *msg)
{
	const char *name, *from, *to;
	Daemon *d = data;

	if (!eldbus_message_arguments_get(msg, "sss", &name, &from, &to)) {
		fprintf(stderr, "Could not get NameOwnerChanged arguments");
		return;
	}

	if (strcmp(name, BUS_NAME) != 0)
		return;

	d->online = EINA_TRUE;
	_pending_call_send(d);
}

static Eina_Bool _dbus_init(Daemon *dialer_daemon)
{
	Eldbus_Message *msg;
	char *bus_name = BUS_NAME;

	bus_conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
	if (!bus_conn) {
		fprintf(stderr, "Could not fetch the DBus session");
		return EINA_FALSE;
	}

	eldbus_signal_handler_add(bus_conn, ELDBUS_FDO_BUS, ELDBUS_FDO_PATH,
					ELDBUS_FDO_INTERFACE,
					"NameOwnerChanged",
					_name_owner_changed, dialer_daemon);

	msg = eldbus_message_method_call_new(ELDBUS_FDO_BUS, ELDBUS_FDO_PATH,
						ELDBUS_FDO_INTERFACE,
						"NameHasOwner");

	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, EINA_FALSE);

	if (!eldbus_message_arguments_append(msg, "s", bus_name))
		goto err_msg;

	eldbus_connection_send(bus_conn, msg, _has_owner_cb, dialer_daemon, -1);

	return EINA_TRUE;
err_msg:
	eldbus_message_unref(msg);
	return EINA_FALSE;
}

static int _dial(const char *number, Daemon *d)
{
	Eina_Bool do_auto = EINA_TRUE;
	Eldbus_Message *msg;

	msg = eldbus_message_method_call_new(BUS_NAME, PATH, RC_IFACE, "Dial");

	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, -1);

	if (!eldbus_message_arguments_append(msg, "sb", number, &do_auto))
		goto err_msg;

	if (d->online) {
		eldbus_connection_send(bus_conn, msg, _dial_return_cb, NULL, -1);
	} else
		d->pending = eina_list_append(d->pending, msg);

	return 0;
err_msg:
	eldbus_message_unref(msg);
	return -1;
}

static int _create(void *data __UNUSED__)
{
	return 0;
}

static int _reset(bundle *b, void *data)
{
	const char *number, *mime_type, *tmp;
	Daemon *d = data;

	EINA_SAFETY_ON_NULL_RETURN_VAL(b, -1);
	number = bundle_get_val(b, "number");
	if (!number) {
		mime_type = bundle_get_val(b, AUL_K_MIME_TYPE);
		if (mime_type){
			if (strncmp(mime_type, "phonenum.uri", 12) == 0 ||
				strncmp(mime_type, "tel.uri", 7) == 0) {
				tmp = bundle_get_val(b, AUL_K_MIME_CONTENT);
				EINA_SAFETY_ON_NULL_RETURN_VAL(tmp, -1);
				if (strncmp(tmp, "tel:", 4) == 0)
					number = (char *)tmp + 4;
				else {
					fprintf(stderr, "Phone not present");
					return -1;
				}
			} else {
				fprintf(stderr, "Unexpected mime type.");
				return -1;
			}
		} else {
			tmp = (const char *)appsvc_get_uri(b);
			EINA_SAFETY_ON_NULL_RETURN_VAL(tmp, -1);
			number = (char *)tmp + 4;
		}
	}
	EINA_SAFETY_ON_NULL_RETURN_VAL(number, -1);
	return _dial(number, d);
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
	Daemon dialer_daemon;
	dialer_daemon.pending = NULL;
	dialer_daemon.online = EINA_FALSE;
	struct appcore_ops ops = {
		.create = _create,
		.resume = _resume,
		.reset = _reset,
		.pause = _pause,
		.terminate = _terminate,
	};
	ops.data = &dialer_daemon;

	eldbus_init();
	EINA_SAFETY_ON_FALSE_RETURN_VAL(_dbus_init(&dialer_daemon), -1);
	r = appcore_efl_main(APP_NAME, &argc, &argv, &ops);
	eldbus_connection_unref(bus_conn);

	eldbus_shutdown();
	return r;
}
