#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <Eldbus.h>

#include "amb.h"
#include "log.h"

typedef struct _AMB_Manager AMB_Manager;
typedef struct _AMB_Bus_Object AMB_Bus_Object;

static const char bus_name[] = "org.automotive.message.broker";

static Eldbus_Connection *bus_conn = NULL;
static char *bus_id = NULL;
static AMB_Manager *amb_manager = NULL;
static Eldbus_Signal_Handler *sig_amb_properties_changed = NULL;

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

struct _AMB_Bus_Object
{
	const char *path;
	Eina_List *dbus_signals; /* of Eldbus_Signal_Handler */
};

static void _bus_object_free(AMB_Bus_Object *o)
{
	Eldbus_Signal_Handler *sh;

	eina_stringshare_del(o->path);

	EINA_LIST_FREE(o->dbus_signals, sh)
		eldbus_signal_handler_del(sh);
}

struct _AMB_Manager
{
	AMB_Bus_Object base;
	const char *name;
	unsigned int interfaces;
	Eina_Bool night_mode;
};

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

static void _amb_get_property_reply(void *data, const Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	Eldbus_Message_Iter *variant;
	const char *err_name, *err_message;

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

	char *property_name = (char*) data;
	AMB_Manager *m = _amb_get();

	if (strcmp(property_name, PROP_NIGHT_MODE_STRING) == 0) {
		eldbus_message_iter_arguments_get(variant, "b", &m->night_mode);
		DBG("NightMode is set to: %d", m->night_mode);
	} else {
		return;
	}

	_notify_amb_callbacks_list(cbs_amb_properties_changed);
}

static void _amb_manager_property_get(const char *property, const char *path, Eldbus_Message_Cb cb, void *data)
{
	Eldbus_Message *msg;
	char *interface_name;

	EINA_SAFETY_ON_NULL_RETURN(property);
	EINA_SAFETY_ON_NULL_RETURN(path);

	if (strcmp(property, PROP_NIGHT_MODE_STRING) == 0) {
		interface_name = AMB_NIGHT_MODE_IFACE;
	} else {
		return;
	}

	msg = eldbus_message_method_call_new(
		bus_id, path, AMB_PROPERTIES_IFACE, "Get");

	eldbus_message_arguments_append(msg,"ss", interface_name, property);
	eldbus_connection_send(bus_conn, msg, cb, data, -1);
}

static void _amb_get_property_path_reply(void *data, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	Eldbus_Message_Iter *ao;
	const char *path, *err_name, *err_message;

	EINA_SAFETY_ON_NULL_RETURN(data);

	if (!msg) {
		ERR("No message");
		return;
	}

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		ERR("Failed to get property path: %s: %s", err_name, err_message);
		return;
	}

	if (!eldbus_message_arguments_get(msg, "ao", &ao)) {
		ERR("Could not get arguments");
		return;
	}

	eldbus_message_iter_get_and_next(ao, 'o', &path);

	if(!path) { 
		ERR("Could not get object path");
		return;
	}

	_amb_manager_property_get((const char*) data, path, _amb_get_property_reply, data);
}

static void _amb_get_property_path(const char *property, Eldbus_Message_Cb cb)
{
	Eldbus_Message *msg = eldbus_message_method_call_new(
				bus_id, "/", AMB_MANAGER_IFACE, "FindObject");

	eldbus_message_arguments_append(msg, "s", property);
	eldbus_connection_send(bus_conn, msg, cb, (void*) property, -1);
}

static void _amb_get_property(const char *property)
{
	_amb_get_property_path(property, _amb_get_property_path_reply);
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

static void _amb_properties_changed(void *data __UNUSED__, Eldbus_Message *msg)
{
	Eina_Bool changed = EINA_FALSE;
	Eldbus_Message_Iter *array1, *array2, *dict;
	const char *interface, *err_name, *err_message;

	if (eldbus_message_error_get(msg, &err_name, &err_message)) {
		ERR("Failed to get signal: %s: %s", err_name, err_message);
		return;
	}

	if (!eldbus_message_arguments_get(msg, "sa{sv}as", &interface, &array1, &array2)) {
		ERR("Could not get PropertiesChanged arguments");
		return;
	}

	AMB_Manager *m = _amb_get();

	while (eldbus_message_iter_get_and_next(array1, 'e', &dict)) {
		Eldbus_Message_Iter *variant;
		const char *property;

		if (!eldbus_message_iter_arguments_get(dict, "sv", &property, &variant))
			continue;

		DBG("AMB property changed at %s: %s", interface, property);
		if (strcmp(interface, AMB_NIGHT_MODE_IFACE) == 0 &&
			strcmp(property, PROP_NIGHT_MODE_STRING) == 0) {
			eldbus_message_iter_arguments_get(variant, "b", &m->night_mode);
			changed = EINA_TRUE;
			DBG("NightMode updated: %d", m->night_mode);
		}
	}

	if (changed)
		_notify_amb_callbacks_list(cbs_amb_properties_changed);
}

static void _amb_load(void)
{
	amb_manager = _amb_new("/");
	EINA_SAFETY_ON_NULL_RETURN(amb_manager);

	_amb_get_property(PROP_NIGHT_MODE_STRING);
}

static void _amb_connected(const char *id)
{
	free(bus_id);
	bus_id = strdup(id);

	sig_amb_properties_changed = eldbus_signal_handler_add(
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
		eldbus_signal_handler_del(sig_amb_properties_changed);
		sig_amb_properties_changed = NULL;
	}

	if (bus_id) {
		_notify_amb_callbacks_list(cbs_amb_disconnected);
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
		INF("amb appeared as %s", to);
		_amb_connected(to);
	} else if (from[0] != '\0' && to[0] == '\0') {
		INF("amb disappeared from %s", from);
		_amb_disconnected();
	}
}

static void _amb_get_name_owner(void *data __UNUSED__, Eldbus_Message *msg,
						Eldbus_Pending *pending __UNUSED__)
{
	const char *id, *err_name, *err_message;

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

	INF("amb bus id: %s", id);
	_amb_connected(id);
}

Eina_Bool amb_night_mode_get(void)
{
	AMB_Manager *m = _amb_get();
	EINA_SAFETY_ON_NULL_RETURN_VAL(m, EINA_FALSE);
	return m->night_mode;
}

Eina_Bool amb_init(void)
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

	eldbus_name_owner_get(bus_conn, bus_name, _amb_get_name_owner, NULL);

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
