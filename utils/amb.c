#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <E_DBus.h>

#include "amb.h"
#include "log.h"

typedef struct _AMB_Manager AMB_Manager;
typedef struct _AMB_Bus_Object AMB_Bus_Object;

static const char bus_name[] = "org.automotive.message.broker";

static E_DBus_Connection *bus_conn = NULL;
static char *bus_id = NULL;
static AMB_Manager *amb_manager = NULL;
static E_DBus_Signal_Handler *sig_amb_properties_changed = NULL;
static DBusPendingCall *amb_get_night_mode_path = NULL;

struct _AMB_Callback_List_Node
{
	EINA_INLIST;
	void (*cb)(void *data);
	const void *cb_data;
};

static Eina_Inlist *cbs_amb_connected = NULL;
static Eina_Inlist *cbs_amb_disconnected = NULL;
static Eina_Inlist *cbs_amb_properties_changed = NULL;

#define AMB_MANAGER_IFACE			"org.automotive.Manager"
#define AMB_PROPERTIES_IFACE			"org.freedesktop.DBus.Properties"
#define AMB_NIGHT_MODE_IFACE			"org.automotive.NightMode"
#define PROP_NIGHT_MODE_STRING			"NightMode"

static Eina_Bool _dbus_bool_get(DBusMessageIter *itr)
{
	dbus_bool_t val;
	dbus_message_iter_get_basic(itr, &val);
	return val;
}

struct _AMB_Bus_Object
{
	const char *path;
	Eina_List *dbus_signals; /* of E_DBus_Signal_Handler */
};

static void _bus_object_free(AMB_Bus_Object *o)
{
	E_DBus_Signal_Handler *sh;

	eina_stringshare_del(o->path);

	EINA_LIST_FREE(o->dbus_signals, sh)
		e_dbus_signal_handler_del(bus_conn, sh);
}

struct _AMB_Manager
{
	AMB_Bus_Object base;
	const char *name;
	unsigned int interfaces;
	unsigned char vehiclespeed;
	Eina_Bool nightmode : 1;
};

static void _amb_property_update(AMB_Manager *m, const char *interface,
					DBusMessageIter *property)
{
	if (strcmp(interface, AMB_NIGHT_MODE_IFACE) == 0) {
		/* TODO: change later to use night mode */
		DBusMessageIter entry, variant;
		const char *key;

		dbus_message_iter_recurse(property, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		if (strcmp(key, PROP_NIGHT_MODE_STRING) == 0) {

			dbus_message_iter_next(&entry);
			dbus_message_iter_recurse(&entry, &variant);
			m->nightmode = _dbus_bool_get(&variant);
			DBG("NightMode updated: %d", m->nightmode);
		} else {
			DBG("invalid key: %s", key);
		}
	} else {
		DBG("%s (unused property)", interface);
	}
}

static AMB_Manager *_amb_get(void)
{
	return amb_manager;
}

static void _notify_amb_callbacks_list(Eina_Inlist *list)
{
	AMB_Callback_List_Node *node;

	EINA_INLIST_FOREACH(list, node)
		node->cb((void *) node->cb_data);
}

static void _amb_get_night_mode_reply(void *data __UNUSED__, DBusMessage *msg,
					DBusError *err)
{
	if (!msg) {
		if (err)
			ERR("%s: %s", err->name, err->message);
		else
			ERR("No message");
		return;
	}

	DBusError e;
	dbus_error_init(&e);
	dbus_bool_t value;

	AMB_Manager *m = _amb_get();

	dbus_message_get_args(msg, &e, DBUS_TYPE_BOOLEAN, &value, DBUS_TYPE_INVALID);

	m->nightmode = value;
	DBG("NightMode is set to: %d", m->nightmode);
	_notify_amb_callbacks_list(cbs_amb_properties_changed);
}

static void _amb_manager_property_get(char *property, char *path, E_DBus_Method_Return_Cb cb, void *data)
{
	DBusMessage *msg;

	EINA_SAFETY_ON_NULL_RETURN(property);
	EINA_SAFETY_ON_NULL_RETURN(path);

	if (strcmp(property, PROP_NIGHT_MODE_STRING) == 0) {
		char* interface_name = AMB_NIGHT_MODE_IFACE;

		msg = dbus_message_new_method_call(
			bus_id, path, AMB_PROPERTIES_IFACE, "Get");

		dbus_message_append_args(msg,
			DBUS_TYPE_STRING, &interface_name,
			DBUS_TYPE_STRING, &property,
			DBUS_TYPE_INVALID);

		e_dbus_message_send(bus_conn, msg, cb, -1, data);
	}

	dbus_message_unref(msg);
}

static void _amb_get_night_mode_path_reply(void *data __UNUSED__, DBusMessage *msg,
					DBusError *err)
{
	amb_get_night_mode_path = NULL;

	if (!msg) {
		if (err)
			ERR("%s: %s", err->name, err->message);
		else
			ERR("No message");
		return;
	}

	DBusError e;
	char *path;
	dbus_error_init(&e);
	dbus_message_get_args(msg, &e, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID);

	if(!path) { 
		ERR("Could not get NightMode object path");
		return;
	}

	_amb_manager_property_get(PROP_NIGHT_MODE_STRING, path, _amb_get_night_mode_reply, NULL);
}

static AMB_Manager *_amb_new(const char *path)
{
	AMB_Manager *m = calloc(1, sizeof(AMB_Manager));
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, NULL);

	m->base.path = eina_stringshare_add(path);
	EINA_SAFETY_ON_NULL_GOTO(m->base.path, error_path);

	return m;

error_path:
	free(m);
	return NULL;
}

static void _amb_free(AMB_Manager *m)
{
	eina_stringshare_del(m->name);

	_bus_object_free(&m->base);
	free(m);
}

static void _amb_properties_changed(void *data __UNUSED__, DBusMessage *msg)
{
	const char *path;
	AMB_Manager *m = _amb_get();
	DBusMessageIter iter, value;
	const char *interface;

	if (!msg || !dbus_message_iter_init(msg, &iter)) {
		ERR("Could not handle message %p", msg);
		return;
	}

	path = dbus_message_get_path(msg);

	dbus_message_iter_get_basic(&iter, &interface);
	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &value);

	if (strcmp(interface, AMB_NIGHT_MODE_IFACE) == 0) {
		_amb_property_update(m, interface, &value);
		_notify_amb_callbacks_list(cbs_amb_properties_changed);
	}
}

static void _amb_load(void)
{
	amb_manager = _amb_new("/");
	EINA_SAFETY_ON_NULL_RETURN(amb_manager);

	char *property_name = PROP_NIGHT_MODE_STRING; 

	DBusMessage *msg = dbus_message_new_method_call(
		bus_id, "/", AMB_MANAGER_IFACE, "findProperty");

	dbus_message_append_args(msg,
		DBUS_TYPE_STRING, &property_name,
		DBUS_TYPE_INVALID);

	amb_get_night_mode_path = e_dbus_message_send(
		bus_conn, msg, _amb_get_night_mode_path_reply, -1, NULL);

	dbus_message_unref(msg);
}

static void _amb_connected(const char *id)
{
	free(bus_id);
	bus_id = strdup(id);

	sig_amb_properties_changed = e_dbus_signal_handler_add(
		bus_conn, bus_id, NULL,
		AMB_PROPERTIES_IFACE,
		"PropertiesChanged",
		_amb_properties_changed, NULL);

	_amb_load();

	_notify_amb_callbacks_list(cbs_amb_connected);
}

static void _amb_disconnected(void)
{
	if (sig_amb_properties_changed) {
		e_dbus_signal_handler_del(bus_conn, sig_amb_properties_changed);
		sig_amb_properties_changed = NULL;
	}

	if (bus_id) {
		_notify_amb_callbacks_list(cbs_amb_disconnected);
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
		INF("amb appeared as %s", to);
		_amb_connected(to);
	} else if (from[0] != '\0' && to[0] == '\0') {
		INF("amb disappeared from %s", from);
		_amb_disconnected();
	}
}

static void _amb_get_name_owner(void *data __UNUSED__, DBusMessage *msg, DBusError *err)
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

	INF("amb bus id: %s", id);
	_amb_connected(id);
}

Eina_Bool amb_nightmode_get(void)
{
	AMB_Manager *m = _amb_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, EINA_FALSE);
	return m->nightmode;
}

Eina_Bool amb_init(void)
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

	e_dbus_get_name_owner(bus_conn, bus_name, _amb_get_name_owner, NULL);

	return EINA_TRUE;
}

void amb_shutdown(void)
{
	if (amb_manager) {
		_amb_free(amb_manager);
		amb_manager = NULL;
	}	
	_amb_disconnected();
}

Eina_Bool amb_night_mode_get(void)
{
	AMB_Manager *m = _amb_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, EINA_FALSE);
	return m->nightmode;
}

static AMB_Callback_List_Node * _amb_callback_list_node_create(
	void (*cb)(void *data),const void *data)
{
	AMB_Callback_List_Node  *node_new;

	node_new = calloc(1, sizeof(AMB_Callback_List_Node));
	EINA_SAFETY_ON_NULL_RETURN_VAL(node_new, NULL);

	node_new->cb_data = data;
	node_new->cb = cb;

	return node_new;
}

static void _amb_callback_list_delete(Eina_Inlist **list,
					AMB_Callback_List_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(*list);
	*list = eina_inlist_remove(*list, EINA_INLIST_GET(node));
	free(node);
}

AMB_Callback_List_Node *
amb_properties_changed_cb_add(void (*cb)(void *data), const void *data)
{
	AMB_Callback_List_Node *node;

	EINA_SAFETY_ON_NULL_RETURN_VAL(cb, NULL);
	node = _amb_callback_list_node_create(cb, data);
	EINA_SAFETY_ON_NULL_RETURN_VAL(node, NULL);

	cbs_amb_properties_changed = eina_inlist_append(cbs_amb_properties_changed,
						EINA_INLIST_GET(node));

	return node;
}

void amb_properties_changed_cb_del(AMB_Callback_List_Node *node)
{
	EINA_SAFETY_ON_NULL_RETURN(node);
	_amb_callback_list_delete(&cbs_amb_properties_changed, node);
}
