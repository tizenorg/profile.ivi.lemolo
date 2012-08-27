#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <appcore-efl.h>
#include <stdio.h>
#include <stdlib.h>
#include <Evas.h>
#include <E_DBus.h>
#include <aul.h>
#include <appsvc.h>
#include <Eina.h>

#define APP_NAME "org.tizen.call"
#define BUS_NAME "org.tizen.dialer"
#define PATH "/"
#define IFACE "org.tizen.dialer.Control"

static E_DBus_Connection *bus_conn = NULL;


typedef struct _Daemon {
	Eina_Bool online;
	Eina_List *pending;
} Daemon;

static void _dial_return_cb(void *data __UNUSED__, DBusMessage *msg __UNUSED__,
				DBusError *error)
{
	if (dbus_error_is_set(error)) {
		fprintf(stderr, "Error: %s %s", error->name, error->message);
		return;
	}
	elm_exit();
}

static void _pending_call_send(Daemon *d)
{
	DBusMessage *p_msg;
	EINA_LIST_FREE(d->pending, p_msg) {
		e_dbus_message_send(bus_conn, p_msg, _dial_return_cb, -1, NULL);
		dbus_message_unref(p_msg);
	}
}

static void _has_owner_cb(void *data, DBusMessage *msg, DBusError *error)
{
	dbus_bool_t online;
	DBusError err;
	Daemon *d = data;

	if (dbus_error_is_set(error)) {
		fprintf(stderr, "Error: %s %s", error->name, error->message);
		return;
	}
	dbus_error_init(&err);
	dbus_message_get_args(msg, &err, DBUS_TYPE_BOOLEAN, &online,
				DBUS_TYPE_INVALID);

	if (!online)
		e_dbus_start_service_by_name(bus_conn, BUS_NAME, 0, NULL, NULL);
	else {
		d->online = EINA_TRUE;
		_pending_call_send(d);
	}
}

static void _name_owner_changed(void *data, DBusMessage *msg)
{
	DBusError err;
	const char *name, *from, *to;
	Daemon *d = data;

	dbus_error_init(&err);
	if (!dbus_message_get_args(msg, &err,
					DBUS_TYPE_STRING, &name,
					DBUS_TYPE_STRING, &from,
					DBUS_TYPE_STRING, &to,
					DBUS_TYPE_INVALID)) {
		fprintf(stderr,
			"Could not get NameOwnerChanged arguments: %s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		return;
	}

	if (strcmp(name, BUS_NAME) != 0)
		return;

	d->online = EINA_TRUE;
	_pending_call_send(d);
}

static Eina_Bool _dbus_init(Daemon *dialer_daemon)
{
	DBusMessage *msg;
	char *bus_name = BUS_NAME;

	bus_conn = e_dbus_bus_get(DBUS_BUS_SYSTEM);
	if (!bus_conn) {
		fprintf(stderr, "Could not fetch the DBus session");
		return EINA_FALSE;
	}

	e_dbus_signal_handler_add(bus_conn, E_DBUS_FDO_BUS, E_DBUS_FDO_PATH,
					E_DBUS_FDO_INTERFACE,
					"NameOwnerChanged",
					_name_owner_changed, dialer_daemon);

	msg = dbus_message_new_method_call(E_DBUS_FDO_BUS, E_DBUS_FDO_PATH,
						E_DBUS_FDO_INTERFACE,
						"NameHasOwner");

	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, EINA_FALSE);

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &bus_name,
					DBUS_TYPE_INVALID))
		goto err_msg;

	e_dbus_message_send(bus_conn, msg, _has_owner_cb, -1, dialer_daemon);
	dbus_message_unref(msg);

	return EINA_TRUE;
err_msg:
	dbus_message_unref(msg);
	return EINA_FALSE;
}

static int _dial(const char *number, Daemon *d)
{
	dbus_bool_t do_auto = TRUE;
	DBusMessage *msg;

	msg = dbus_message_new_method_call(BUS_NAME, PATH, IFACE, "Dial");

	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, -1);

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &number,
					DBUS_TYPE_BOOLEAN, &do_auto,
					DBUS_TYPE_INVALID))
		goto err_msg;

	if (d->online) {
		e_dbus_message_send(bus_conn, msg, _dial_return_cb, -1, NULL);
		dbus_message_unref(msg);
	} else
		d->pending = eina_list_append(d->pending, msg);

	return 0;
err_msg:
	dbus_message_unref(msg);
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

	e_dbus_init();
	EINA_SAFETY_ON_FALSE_RETURN_VAL(_dbus_init(&dialer_daemon), -1);
	r = appcore_efl_main(APP_NAME, &argc, &argv, &ops);
	e_dbus_connection_close(bus_conn);

	e_dbus_shutdown();
	return r;
}
