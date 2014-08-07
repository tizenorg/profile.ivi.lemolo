#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <Eldbus.h>

#include "log.h"
#include "gui.h"
#include "ofono.h"

static Eldbus_Connection *bus_conn = NULL;
static Eldbus_Service_Interface *bus_iface = NULL;

#define RC_IFACE "org.tizen.messages.Control"
#define RC_PATH "/"

static const char *rc_service = NULL;
static OFono_Callback_List_Modem_Node *modem_changed_node = NULL;
static Eldbus_Message *pending_send = NULL;

static void _send_message(const char *number, const char *message,
				Eina_Bool do_auto)
{
	INF("send '%s' '%s' auto=%d!", number, message, do_auto);
	gui_activate();
	gui_send(number, message, do_auto);
}

static void _modem_changed_cb(void *data __UNUSED__)
{
	const char *number, *message;
	Eina_Bool do_auto;
	Eldbus_Message *reply;

	if (!ofono_voice_is_online() || !pending_send)
		return;

	if (!eldbus_message_arguments_get(pending_send, "ssb", &number, &message, &do_auto)) {
		ERR("Could not get pending send arguments");
		reply = eldbus_message_error_new(pending_send, "Pending send", "Invalid argument");
		goto reply_send;
	}

	_send_message(number, message, do_auto);
	reply = eldbus_message_method_return_new(pending_send);
reply_send:
	eldbus_connection_send(bus_conn, reply, NULL, NULL, -1);
	eldbus_message_unref(pending_send);
	pending_send = NULL;
}

static Eldbus_Message *
_rc_activate(Eldbus_Object *obj __UNUSED__, Eldbus_Message *msg)
{
	INF("Remotely activated!");
	gui_activate();
	return eldbus_message_method_return_new(msg);
}

static Eldbus_Message *
_rc_send(Eldbus_Object *obj __UNUSED__, Eldbus_Message *msg)
{
	Eina_Bool do_auto;
	const char *number, *message;

	if (!ofono_voice_is_online()) {
		if (pending_send)
			eldbus_message_unref(pending_send);
		pending_send = eldbus_message_ref(msg);
		return NULL;
	}

	if (!eldbus_message_arguments_get(msg, "ssb", &number, &message, &do_auto)) {
		ERR("Could not get pending send arguments");
		return eldbus_message_error_new(pending_send, "Pending send", "Invalid argument");
	}

	_send_message(number, message, do_auto);
	return eldbus_message_method_return_new(msg);
}

static const Eldbus_Method rc_methods[] = {
	{ "Activate", NULL, NULL, _rc_activate, },
	{ "Send", NULL, ELDBUS_ARGS(
		{"s", "number"},
		{"s", "message"},
		{"b", "do_auto"}),	_rc_send, ELDBUS_METHOD_FLAG_DEPRECATED },
	{ }
};

static const Eldbus_Service_Interface_Desc rc_iface_desc = {
   RC_IFACE, rc_methods, NULL, NULL, NULL, NULL
};

static void _rc_object_register(void)
{
	bus_iface = eldbus_service_interface_register(bus_conn,	RC_PATH, &rc_iface_desc);
}

static void _rc_activate_existing_reply(void *data __UNUSED__, const Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	const char *err_name, *err_message;

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		CRITICAL("Failed to activate existing messages: %s: %s", err_name, err_message);
		_app_exit_code = EXIT_FAILURE;
		ecore_main_loop_quit();
		return;
	}

	INF("Activated the existing messages!");
	ecore_main_loop_quit();
}

static void _rc_activate_existing(void)
{
	Eldbus_Message *msg = eldbus_message_method_call_new(
		rc_service, RC_PATH, RC_IFACE, "Activate");
	eldbus_connection_send(bus_conn, msg,  _rc_activate_existing_reply, NULL, -1);
}

static void _rc_request_name_reply(void *data __UNUSED__, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	int t;
	const char *err_name, *err_message;

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
		WRN("Messages already running! Activate it!");
		_rc_activate_existing();
	}
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

	return EINA_TRUE;
}

void rc_shutdown(void)
{
	if (bus_iface)
		eldbus_service_interface_unregister(bus_iface);

	ofono_modem_changed_cb_del(modem_changed_node);

	if (pending_send)
		eldbus_message_unref(pending_send);

	bus_conn = NULL;
}
