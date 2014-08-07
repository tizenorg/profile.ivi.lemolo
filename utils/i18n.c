#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <Eldbus.h>

#include "i18n.h"
#include "log.h"

typedef struct _Locale_Manager Locale_Manager;
typedef struct _Locale_Bus_Object Locale_Bus_Object;

static const char bus_name[] = "org.freedesktop.locale1";

static Eldbus_Connection *bus_conn = NULL;
static char *bus_id = NULL;
static Locale_Manager *locale_manager = NULL;
static Eldbus_Signal_Handler *sig_locale_properties_changed = NULL;

struct _Locale_Callback_List_Node
{
	EINA_INLIST;
	void (*cb)(void *data);
	const void *cb_data;
};

static Eina_Inlist *cbs_locale_connected = NULL;
static Eina_Inlist *cbs_locale_disconnected = NULL;
static Eina_Inlist *cbs_locale_properties_changed = NULL;

#define LOCALE_IFACE				"org.freedesktop.locale1"
#define LOCALE_BUS_PATH				"/org/freedesktop/locale1"
#define LOCALE_PROPERTIES_IFACE			"org.freedesktop.DBus.Properties"
#define PROP_LOCALE_STRING			"Locale"

struct _Locale_Bus_Object
{
	const char *path;
	Eina_List *dbus_signals; /* of Eldbus_Signal_Handler */
};

static void _bus_object_free(Locale_Bus_Object *o)
{
	Eldbus_Signal_Handler *sh;

	eina_stringshare_del(o->path);

	EINA_LIST_FREE(o->dbus_signals, sh)
		eldbus_signal_handler_del(sh);
}

struct _Locale_Manager
{
	Locale_Bus_Object base;
	const char *name;
	unsigned int interfaces;
	const char *lang;
};

static Locale_Manager *_locale_get(void)
{
	return locale_manager;
}

static void _notify_locale_callbacks_list(Eina_Inlist *list)
{
	Locale_Callback_List_Node *node;

	EINA_INLIST_FOREACH(list, node)
		node->cb((void *) node->cb_data);
}

static void _locale_get_property_reply(void *data, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	Eldbus_Message_Iter *variant, *array;
	const char *type, *locale, *err_name, *err_message;

	EINA_SAFETY_ON_NULL_RETURN(data);

	if (!msg) {
		ERR("No message");
		return;
	}

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		ERR("Failed to get property: %s: %s", err_name, err_message);
		return;
	}

	if (!eldbus_message_arguments_get(msg, "v", &variant)) {
		ERR("Could not get arguments");
		return;
	}

	if (!eldbus_message_iter_arguments_get(variant, "as", &array)) {
		ERR("Could not get arguments");
		return;
	}

	if (!eldbus_message_iter_get_and_next(array, 's', &locale)) {
		ERR("Could not get locale");
		return;
	}

	char *property_name = (char*) data;
	Locale_Manager *m = _locale_get();

	if (strcmp(property_name, PROP_LOCALE_STRING) == 0) {
		if (m->lang)
			eina_stringshare_replace(&m->lang, locale);
		else
			m->lang = eina_stringshare_add(locale);

		DBG("Locale is set to: %s", m->lang);
	} else {
		return;
	}

	_notify_locale_callbacks_list(cbs_locale_properties_changed);
}

static void _locale_property_get(char *property, char *path, Eldbus_Message_Cb cb, void *data)
{
	Eldbus_Message *msg;
	char *interface_name;

	EINA_SAFETY_ON_NULL_RETURN(property);
	EINA_SAFETY_ON_NULL_RETURN(path);

	if (strcmp(property, PROP_LOCALE_STRING) == 0) {
		interface_name = LOCALE_IFACE;
	} else {
		return;
	}

	msg = eldbus_message_method_call_new(
		bus_id, LOCALE_BUS_PATH, LOCALE_PROPERTIES_IFACE, "Get");

	eldbus_message_arguments_append(msg, "ss", interface_name, property);
	eldbus_connection_send(bus_conn, msg, cb, data, -1);
}

static Locale_Manager *_locale_new(const char *path)
{
	Locale_Manager *m = calloc(1, sizeof(Locale_Manager));
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, NULL);

	m->base.path = eina_stringshare_add(path);
	EINA_SAFETY_ON_NULL_GOTO(m->base.path, error_path);

	return m;

error_path:
	free(m);
	return NULL;
}

static void _locale_free(Locale_Manager *m)
{
	if (m->lang)
		eina_stringshare_del(m->lang);

	eina_stringshare_del(m->base.path);

	_bus_object_free(&m->base);
	free(m);
}

static void _locale_properties_changed(void *data __UNUSED__, Eldbus_Message *msg)
{
	Eldbus_Message_Iter *array1, *array2;
	const char *interface, *err_name, *err_message;

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		ERR("Failed to get signal: %s: %s", err_name, err_message);
		return;
	}

	if (!eldbus_message_arguments_get(msg, "sa{sv}as", &interface, &array1, &array2)) {
		ERR("Could not get PropertiesChanged arguments");
		return;
	}

	DBG("Locale property changed at %s", interface);
	if (strcmp(interface, LOCALE_IFACE) == 0) {
		_locale_property_get(PROP_LOCALE_STRING, LOCALE_BUS_PATH, _locale_get_property_reply, PROP_LOCALE_STRING);
	}
}

static void _locale_load(void)
{
	locale_manager = _locale_new("/");
	EINA_SAFETY_ON_NULL_RETURN(locale_manager);
}

static void _locale_connected(const char *id)
{
	free(bus_id);
	bus_id = strdup(id);

	sig_locale_properties_changed = eldbus_signal_handler_add(
		bus_conn, bus_id, NULL,
		LOCALE_PROPERTIES_IFACE,
		"PropertiesChanged",
		_locale_properties_changed, NULL);

	_locale_load();

	_notify_locale_callbacks_list(cbs_locale_connected);
}

static void _locale_disconnected(void)
{
	if (sig_locale_properties_changed) {
		eldbus_signal_handler_del(sig_locale_properties_changed);
		sig_locale_properties_changed = NULL;
	}

	if (bus_id) {
		_notify_locale_callbacks_list(cbs_locale_disconnected);
		free(bus_id);
		bus_id = NULL;
	}
}

static void _name_owner_changed(void *data __UNUSED__, Eldbus_Message *msg)
{
	const char *name, *from, *to;

	if (!eldbus_message_arguments_get(msg, "sss", &name, &from, &to)) {
		ERR("Could not get NameOwnerChanged arguments");
		return;
	}

	if (strcmp(name, bus_name) != 0)
		return;

	DBG("NameOwnerChanged %s from=%s to=%s", name, from, to);

	if (from[0] == '\0' && to[0] != '\0') {
		INF("localed appeared as %s", to);
		_locale_connected(to);
	} else if (from[0] != '\0' && to[0] == '\0') {
		INF("localed disappeared from %s", from);
		_locale_disconnected();
	}
}

static void _locale_get_name_owner(void *data __UNUSED__, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	const char *id;
	const char *err_name, *err_message;

	if (!msg) {
		ERR("No message");
		return;
	}

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		ERR("Failed to get name owner: %s: %s", err_name, err_message);
		return;
	}

	if (!eldbus_message_arguments_get(msg, "s", &id)) {
		ERR("Could not get arguments");
		return;
	}

	if (!id || id[0] == '\0') {
		ERR("No name owner fo %s!", bus_name);
		return;
	}

	INF("localed bus id: %s", id);
	_locale_connected(id);
}

const char* locale_lang_get(void)
{
	Locale_Manager *m = _locale_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, EINA_FALSE);

	return m->lang;
}

Eina_Bool locale_init(void)
{
	if (!elm_need_eldbus()) {
		CRITICAL("Elementary does not support DBus.");
		return EINA_FALSE;
	}

	bus_conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
	if (!bus_conn) {
		CRITICAL("Could not get DBus System Bus");
		return EINA_FALSE;
	}

	eldbus_signal_handler_add(bus_conn, ELDBUS_FDO_BUS, ELDBUS_FDO_PATH,
					ELDBUS_FDO_INTERFACE,
					"NameOwnerChanged",
					_name_owner_changed, NULL);

	eldbus_name_owner_get(bus_conn, bus_name, _locale_get_name_owner, NULL);

	return EINA_TRUE;
}

void locale_shutdown(void)
{
	if (locale_manager) {
		_locale_free(locale_manager);
		locale_manager = NULL;
	}	
	_locale_disconnected();
}

static Locale_Callback_List_Node * _locale_callback_list_node_create(
	void (*cb)(void *data),const void *data)
{
	Locale_Callback_List_Node  *node_new;

	node_new = calloc(1, sizeof(Locale_Callback_List_Node));
	EINA_SAFETY_ON_NULL_RETURN_VAL(node_new, NULL);

	node_new->cb_data = data;
	node_new->cb = cb;

	return node_new;
}

static void _locale_callback_list_delete(Eina_Inlist **list,
					Locale_Callback_List_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(*list);
	*list = eina_inlist_remove(*list, EINA_INLIST_GET(node));
	free(node);
}

Locale_Callback_List_Node *
locale_properties_changed_cb_add(void (*cb)(void *data), const void *data)
{
	Locale_Callback_List_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = _locale_callback_list_node_create(cb, data);
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	cbs_locale_properties_changed = eina_inlist_append(cbs_locale_properties_changed,
						EINA_INLIST_GET(node));

	return node;
}

void locale_properties_changed_cb_del(Locale_Callback_List_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	_locale_callback_list_delete(&cbs_locale_properties_changed, node);
}
