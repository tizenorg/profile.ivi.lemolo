#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <E_DBus.h>

#include "i18n.h"
#include "log.h"

typedef struct _Locale_Manager Locale_Manager;
typedef struct _Locale_Bus_Object Locale_Bus_Object;

static const char bus_name[] = "org.freedesktop.locale1";

static E_DBus_Connection *bus_conn = NULL;
static char *bus_id = NULL;
static Locale_Manager *locale_manager = NULL;
static E_DBus_Signal_Handler *sig_locale_properties_changed = NULL;

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
	Eina_List *dbus_signals; /* of E_DBus_Signal_Handler */
};

static void _bus_object_free(Locale_Bus_Object *o)
{
	E_DBus_Signal_Handler *sh;

	eina_stringshare_del(o->path);

	EINA_LIST_FREE(o->dbus_signals, sh)
		e_dbus_signal_handler_del(bus_conn, sh);
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

static void _locale_get_property_reply(void *data __UNUSED__, DBusMessage *msg,
					DBusError *err)
{
	if (!msg) {
		if (err)
			ERR("%s: %s", err->name, err->message);
		else
			ERR("No message");
		return;
	}

	EINA_SAFETY_ON_NULL_RETURN(data);

	DBusError e;
	dbus_error_init(&e);
	DBusMessageIter iter;
	DBusMessageIter variant, array;
	char *locale;

	if (dbus_message_iter_init(msg, &iter) == FALSE) {
		ERR("Invalid dbus response");
		return;
	}

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) {
		ERR("Unexpected dbus response type");
		return;
	}

	dbus_message_iter_recurse(&iter, &variant);
	if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY) {
		ERR("Message does not contain property string");
		return;
	}

	dbus_message_iter_recurse(&variant, &array);
	if (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_STRING) {
		ERR("Message does not contain property string");
		return;
	}

	dbus_message_iter_get_basic(&array, &locale);
	if(!locale) { 
		ERR("Could not get property");
		return;
	}

	char *property_name = (char*) data;
	Locale_Manager *m = _locale_get();
	dbus_message_iter_recurse(&iter, &variant);

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

static void _locale_property_get(char *property, char *path, E_DBus_Method_Return_Cb cb, void *data)
{
	DBusMessage *msg;
	char *interface_name;

	EINA_SAFETY_ON_NULL_RETURN(property);
	EINA_SAFETY_ON_NULL_RETURN(path);

	if (strcmp(property, PROP_LOCALE_STRING) == 0) {
		interface_name = LOCALE_IFACE;
	} else {
		return;
	}

	msg = dbus_message_new_method_call(
		bus_id, LOCALE_BUS_PATH, LOCALE_PROPERTIES_IFACE, "Get");

	dbus_message_append_args(msg,
		DBUS_TYPE_STRING, &interface_name,
		DBUS_TYPE_STRING, &property,
		DBUS_TYPE_INVALID);

	e_dbus_message_send(bus_conn, msg, cb, -1, data);

	dbus_message_unref(msg);
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

static void _locale_properties_changed(void *data __UNUSED__, DBusMessage *msg)
{
	DBusMessageIter iter, value;
	const char *interface;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	dbus_message_iter_get_basic(&iter, &interface);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &value);

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

	sig_locale_properties_changed = e_dbus_signal_handler_add(
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
		e_dbus_signal_handler_del(bus_conn, sig_locale_properties_changed);
		sig_locale_properties_changed = NULL;
	}

	if (bus_id) {
		_notify_locale_callbacks_list(cbs_locale_disconnected);
		free(bus_id);
		bus_id = NULL;
	}
}

static void _name_owner_changed(void *data __UNUSED__, DBusMessage *msg)
{
	DBusError err;
	const char *name, *from, *to;

	dbus_error_init(&err);
	if (!dbus_message_get_args(msg, &err,
					DBUS_TYPE_STRING, &name,
					DBUS_TYPE_STRING, &from,
					DBUS_TYPE_STRING, &to,
					DBUS_TYPE_INVALID)) {
		ERR("Could not get NameOwnerChanged arguments: %s: %s",
			err.name, err.message);
		dbus_error_free(&err);
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

static void _locale_get_name_owner(void *data __UNUSED__, DBusMessage *msg, DBusError *err)
{
	DBusMessageIter itr;
	const char *id;

	if (!msg) {
		if (err)
			ERR("%s: %s", err->name, err->message);
		else
			ERR("No message");
		return;
	}

	dbus_message_iter_init(msg, &itr);
	dbus_message_iter_get_basic(&itr, &id);
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
	if (!elm_need_e_dbus()) {
		CRITICAL("Elementary does not support DBus.");
		return EINA_FALSE;
	}

	bus_conn = e_dbus_bus_get(DBUS_BUS_SYSTEM);
	if (!bus_conn) {
		CRITICAL("Could not get DBus System Bus");
		return EINA_FALSE;
	}

	e_dbus_signal_handler_add(bus_conn, E_DBUS_FDO_BUS, E_DBUS_FDO_PATH,
					E_DBUS_FDO_INTERFACE,
					"NameOwnerChanged",
					_name_owner_changed, NULL);

	e_dbus_get_name_owner(bus_conn, bus_name, _locale_get_name_owner, NULL);

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
