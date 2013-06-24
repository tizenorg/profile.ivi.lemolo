#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Ecore.h>
#include <stdio.h>
#include <stdlib.h>
#include <Eina.h>
#include <Evas.h>
#include <Elementary.h>
#include <E_DBus.h>

#ifdef HAVE_TIZEN
#include <Ecore_X.h>
#include <vconf.h>
#include <vconf-keys.h>
#if 0
#include <utilX.h>
#endif
#include <power.h>
#include <aul.h>
#endif

#define APP_NAME "org.tizen.answer"
#define BUS_NAME "org.tizen.dialer"
#define PATH "/"
#define IFACE "org.tizen.dialer.Control"

static int _log_domain = -1;
#define ERR(...)      EINA_LOG_DOM_ERR(_log_domain, __VA_ARGS__)
#define INF(...)      EINA_LOG_DOM_INFO(_log_domain, __VA_ARGS__)
#define DBG(...)      EINA_LOG_DOM_DBG(_log_domain, __VA_ARGS__)

static E_DBus_Connection *bus_conn = NULL;

typedef struct _Call {
	const char *line_id, *img, *type, *name;
} Call;

typedef struct _Call_Screen {
	Evas_Object *win, *layout;
	Call *call;
	Eina_Bool screen_visible;
	E_DBus_Signal_Handler *sig_call_add;
	E_DBus_Signal_Handler *sig_call_removed;
	E_DBus_Signal_Handler *sig_name_changed;
} Call_Screen;

static void _set_notification_type(Evas_Object *win, Eina_Bool high)
{
#if 0
	Ecore_X_Window xwin;

	xwin = elm_win_xwindow_get(win);

	if (high) {
		ecore_x_netwm_window_type_set(xwin,
						ECORE_X_WINDOW_TYPE_NOTIFICATION);
		utilx_set_system_notification_level(ecore_x_display_get(), xwin,
							UTILX_NOTIFICATION_LEVEL_HIGH);
		power_wakeup(EINA_TRUE);
	} else
		ecore_x_netwm_window_type_set(xwin, ECORE_X_WINDOW_TYPE_NORMAL);
#else
	(void)win;
	(void)high;
#endif
}

static Eina_Bool _phone_locked(void)
{
#ifdef HAVE_TIZEN
	int lock;
	if (vconf_get_int(VCONFKEY_IDLE_LOCK_STATE, &lock) == -1)
		return EINA_FALSE;

	if (lock == VCONFKEY_IDLE_LOCK)
		return EINA_TRUE;

	return EINA_FALSE;
#else
	return EINA_TRUE; /* always have it to show */
#endif
}

static Call *_call_create(DBusMessage *msg)
{
	DBusError err;
	const char *img, *name, *id, *type;
	Call *call;

	dbus_error_init(&err);
	dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &img,
				DBUS_TYPE_STRING, &id, DBUS_TYPE_STRING,
				&name, DBUS_TYPE_STRING, &type,
				DBUS_TYPE_INVALID);

	if (dbus_error_is_set(&err)) {
		ERR("Could not parse message: %s: %s", err.name,
			err.message);
		return NULL;
	}
	call = calloc(1, sizeof(Call));
	EINA_SAFETY_ON_NULL_RETURN_VAL(call, NULL);
	call->img = eina_stringshare_add(img);
	call->line_id = eina_stringshare_add(id);
	call->type = eina_stringshare_add(type);
	call->name = eina_stringshare_add(name);
	DBG("c=%p line_id=%s, name=%s, type=%s, img=%s",
		call, call->line_id, call->name, call->type, call->img);
	return call;
}

static void _call_destroy(Call *c)
{
	DBG("c=%p line_id=%s, name=%s, type=%s, img=%s",
		c, c->line_id, c->name, c->type, c->img);
	eina_stringshare_del(c->img);
	eina_stringshare_del(c->line_id);
	eina_stringshare_del(c->type);
	eina_stringshare_del(c->name);
	free(c);
}

static void _call_screen_show(Call_Screen *cs)
{
	Call *c = cs->call;
	Evas_Object *icon = NULL;

	INF("Show line_id=%s, name=%s, type=%s",
		c->line_id, c->name, c->type);

	_set_notification_type(cs->win, EINA_TRUE);
	cs->screen_visible = EINA_TRUE;
	elm_win_raise(cs->win);
	elm_win_activate(cs->win);

	if (strcmp("", c->name) == 0)
		elm_object_part_text_set(cs->layout, "elm.text.name",
						c->line_id);
	else
		elm_object_part_text_set(cs->layout, "elm.text.name",
						c->name);
	icon = elm_icon_add(cs->layout);

	if (strcmp("", c->img) != 0) {
		elm_image_file_set(icon, c->img, NULL);
	} else
		elm_icon_standard_set(icon, "no-picture");

	elm_object_part_text_set(cs->layout, "elm.text.state", "Incoming...");
	elm_object_part_text_set(cs->layout, "elm.text.phone.type", c->type);
	elm_object_part_content_set(cs->layout, "elm.swallow.photo", icon);
	elm_object_signal_emit(cs->layout, "show,activecall", "gui");
	evas_object_show(cs->win);
#ifdef HAVE_TIZEN
	//screen can't be off
	power_lock_state(POWER_STATE_NORMAL, 0);
#endif
}

static void _sig_call_added(void *data, DBusMessage *msg)
{
	Call_Screen *cs = data;
	Eina_Bool locked = _phone_locked();

	DBG("previous=%p, visible=%d, locked=%d", cs->call, cs->screen_visible,
		locked);

	if (cs->call)
		_call_destroy(cs->call);
	cs->call = _call_create(msg);

	if (!locked)
		return;

	_call_screen_show(cs);
}

static void _call_screen_hide(Call_Screen *cs)
{
	INF("hide call");
	cs->screen_visible = EINA_FALSE;
	_set_notification_type(cs->win, EINA_FALSE);
	elm_object_signal_emit(cs->layout, "hide,activecall", "gui");
	evas_object_hide(cs->win);
#ifdef HAVE_TIZEN
	//screen can go off again
	power_unlock_state(POWER_STATE_NORMAL);
#endif
}

static void _sig_call_removed(void *data, DBusMessage *msg __UNUSED__)
{
	Call_Screen *cs = data;

	DBG("previous=%p, visible=%d", cs->call, cs->screen_visible);

	if (cs->call) {
		_call_destroy(cs->call);
		cs->call = NULL;
	}

	if (!cs->screen_visible)
		return;

	_call_screen_hide(cs);
}

static void _signal_handlers_add(Call_Screen *cs)
{
	cs->sig_call_add = e_dbus_signal_handler_add(bus_conn, BUS_NAME,
							PATH, IFACE,
							"AddedCall",
							_sig_call_added,
							cs);
	cs->sig_call_removed = e_dbus_signal_handler_add(bus_conn,
								BUS_NAME, PATH,
								IFACE,
								"RemovedCall",
								_sig_call_removed,
								cs);
}

static void _signal_handlers_del(Call_Screen *cs)
{
	if (cs->sig_call_add) {
		e_dbus_signal_handler_del(bus_conn, cs->sig_call_add);
		cs->sig_call_add = NULL;
	}

	if (cs->sig_call_removed) {
		e_dbus_signal_handler_del(bus_conn, cs->sig_call_removed);
		cs->sig_call_removed = NULL;
	}
}

static void _daemon_method_call_reply(void *data __UNUSED__,
					DBusMessage *msg __UNUSED__,
					DBusError *err)
{
	if (dbus_error_is_set(err)) {
		ERR("Error calling remote method: %s %s", err->name,
			err->message);
	}
}

static void _daemon_method_call_make_with_reply(const char *method,
						E_DBus_Method_Return_Cb cb,
						void *data)
{
	DBusMessage *msg;

	msg = dbus_message_new_method_call(BUS_NAME, PATH, IFACE, method);
	INF("msg=%p name=%s, path=%s, %s.%s()",
		msg, BUS_NAME, PATH, IFACE, method);
	EINA_SAFETY_ON_NULL_RETURN(msg);

	e_dbus_message_send(bus_conn, msg,  cb, -1, data);
	dbus_message_unref(msg);
}

static void _daemon_method_call_make(const char *method)
{
	_daemon_method_call_make_with_reply(method, _daemon_method_call_reply,
						NULL);
}

static void _daemon_available_call_reply(void *data, DBusMessage *msg,
					DBusError *err)
{
	Call_Screen *cs = data;

	if (dbus_error_is_set(err)) {
		const char *err_name = "org.tizen.dialer.error.NotAvailable";
		if (strcmp(err_name, err->name) == 0) {
			DBG("no incoming calls");
			return;
		}

		ERR("Error calling remote method: %s %s", err->name,
			err->message);
		return;
	}

	if (cs->call)
		_call_destroy(cs->call);

	cs->call = _call_create(msg);
	_call_screen_show(cs);
}

static void _service_start_reply(void *data, DBusMessage *msg __UNUSED__,
					DBusError *err)
{
	Call_Screen *cs = data;
	if (dbus_error_is_set(err)) {
		ERR("Error: %s %s", err->name, err->message);
		return;
	}
	_signal_handlers_add(cs);
	_daemon_method_call_make_with_reply("GetAvailableCall",
						_daemon_available_call_reply,
						cs);
}

static void _has_owner_cb(void *data, DBusMessage *msg __UNUSED__,
				DBusError *error)
{
	dbus_bool_t online;
	DBusError err;
	Call_Screen *cs = data;


	if (dbus_error_is_set(error)) {
		ERR("Error: %s %s", error->name, error->message);
		return;
	}
	dbus_error_init(&err);
	dbus_message_get_args(msg, &err, DBUS_TYPE_BOOLEAN, &online,
				DBUS_TYPE_INVALID);

	if (!online) {
		INF("no dialer, start it");
		e_dbus_start_service_by_name(bus_conn, BUS_NAME, 0,
						_service_start_reply, cs);
		return;
	}

	_signal_handlers_add(cs);
	_daemon_method_call_make_with_reply("GetAvailableCall",
						_daemon_available_call_reply,
						cs);
}

static void _name_owner_changed(void *data, DBusMessage *msg)
{
	Call_Screen *cs = data;
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

	if (strcmp(name, BUS_NAME) != 0)
		return;

	if ((to == NULL) || (*to == '\0')) {
		INF("missed dialer");
		_signal_handlers_del(cs);
		return;
	}

	INF("got new dialer at %s", to);
	_signal_handlers_add(cs);
	_daemon_method_call_make_with_reply("GetAvailableCall",
						_daemon_available_call_reply,
						cs);
}

static Eina_Bool _dbus_init(Call_Screen *cs)
{
	DBusMessage *msg;
	char *bus_name = BUS_NAME;

	INF("Running on Session bus");
	bus_conn = e_dbus_bus_get(DBUS_BUS_SESSION);

	if (!bus_conn) {
		ERR("Could not fetch the DBus session");
		return EINA_FALSE;
	}

	cs->sig_name_changed = e_dbus_signal_handler_add(
		bus_conn, E_DBUS_FDO_BUS, E_DBUS_FDO_PATH, E_DBUS_FDO_INTERFACE,
		"NameOwnerChanged", _name_owner_changed, cs);

	msg = dbus_message_new_method_call(E_DBUS_FDO_BUS, E_DBUS_FDO_PATH,
						E_DBUS_FDO_INTERFACE,
						"NameHasOwner");

	EINA_SAFETY_ON_NULL_RETURN_VAL(msg, EINA_FALSE);

	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &bus_name,
					DBUS_TYPE_INVALID))
		goto err_msg;

	e_dbus_message_send(bus_conn, msg, _has_owner_cb, -1, cs);
	dbus_message_unref(msg);

	return EINA_TRUE;
err_msg:
	dbus_message_unref(msg);
	return EINA_FALSE;
}

Evas_Object *gui_layout_add(Evas_Object *parent, const char *style)
{
	Evas_Object *layout = elm_layout_add(parent);
	if (!elm_layout_theme_set(layout, "layout", "dialer", style)) {
		evas_object_del(layout);
		return NULL;
	}
	return layout;
}

#ifdef HAVE_TIZEN
static int _running_apps_iter(const aul_app_info *info, void *data __UNUSED__)
{
	if (strcmp(info->pkg_name, "org.tizen.draglock") == 0)
		 aul_terminate_pid(info->pid);
	return 0;
}
#endif

static void _phone_unlock_screen(void)
{
	INF("unlock screen");
#ifdef HAVE_TIZEN
	aul_app_get_running_app_info(_running_apps_iter, NULL);
#endif
}

static void _slider_pos_changed_cb(void *data, Evas_Object *obj,
					void *event_inf __UNUSED__)
{
	Call_Screen *cs = data;
	const char *label = elm_actionslider_selected_label_get(obj);

	if (!label)
		return;

	if (strcmp(label, "Answer") == 0) {
		_daemon_method_call_make("AnswerCall");
		elm_actionslider_indicator_pos_set(obj,
							ELM_ACTIONSLIDER_CENTER);
		_phone_unlock_screen();
		_call_screen_hide(cs);
	} else if (strcmp(label, "Hangup") == 0) {
		_daemon_method_call_make("HangupCall");
		elm_actionslider_indicator_pos_set(obj,
							ELM_ACTIONSLIDER_CENTER);
	}
}

static Eina_Bool _gui_init(Call_Screen *cs)
{
	Evas_Object *win, *obj, *lay, *slider;
	Evas_Coord h, w;
	char def_theme[PATH_MAX];
	/* should never, ever quit */

	elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_NONE);
	elm_app_compile_bin_dir_set(PACKAGE_BIN_DIR);
	elm_app_compile_data_dir_set(PACKAGE_DATA_DIR);
	elm_app_info_set(_gui_init, "answer-ofono-efl", "themes/default.edj");

	snprintf(def_theme, sizeof(def_theme), "%s/themes/default.edj",
			elm_app_data_dir_get());

	elm_theme_extension_add(NULL, def_theme);
	elm_theme_overlay_add(NULL, def_theme);

	win = elm_win_util_standard_add("answer screen", "oFono Answer");
	EINA_SAFETY_ON_NULL_RETURN_VAL(win, EINA_FALSE);
	elm_win_autodel_set(win, EINA_FALSE);

	lay = gui_layout_add(win, "answer");
	EINA_SAFETY_ON_NULL_RETURN_VAL(lay, EINA_FALSE);
	evas_object_show(lay);

	slider = elm_actionslider_add(win);
	EINA_SAFETY_ON_NULL_GOTO(slider, err_obj);
	elm_object_style_set(slider, "answer");
	elm_actionslider_indicator_pos_set(slider, ELM_ACTIONSLIDER_CENTER);
	elm_actionslider_magnet_pos_set(slider, ELM_ACTIONSLIDER_CENTER);
	elm_object_part_text_set(slider, "left", "Hangup");
	elm_object_part_text_set(slider, "right", "Answer");
	evas_object_smart_callback_add(slider, "selected",
					_slider_pos_changed_cb, cs);
	evas_object_show(slider);
	elm_object_part_content_set(lay, "elm.swallow.slider", slider);

	obj = elm_layout_edje_get(lay);
	edje_object_size_min_get(obj, &w, &h);
	if ((w == 0) || (h == 0))
		edje_object_size_min_restricted_calc(obj, &w, &h, w, h);
	if ((w == 0) || (h == 0))
		edje_object_parts_extends_calc(obj, NULL, NULL, &w, &h);
	evas_object_resize(lay, w, h);
	evas_object_resize(win, w, h);
	cs->win = win;
	cs->layout = lay;

	return EINA_TRUE;

err_obj:
	evas_object_del(win);
	return EINA_FALSE;
}

EAPI int elm_main(int argc __UNUSED__, char **argv __UNUSED__)
{
	Call_Screen *cs;

	if (!elm_need_e_dbus()) {
		ERR("Could not start E_dbus");
		return -1;
	}

	_log_domain = eina_log_domain_register("answer_daemon", NULL);
	cs = calloc(1, sizeof(Call_Screen));
	EINA_SAFETY_ON_NULL_RETURN_VAL(cs, -1);

	EINA_SAFETY_ON_FALSE_GOTO(_dbus_init(cs), err_dbus);
	EINA_SAFETY_ON_FALSE_GOTO(_gui_init(cs), err_dbus);

	elm_run();

	_signal_handlers_del(cs);

	if (cs->sig_name_changed)
		e_dbus_signal_handler_del(bus_conn, cs->sig_name_changed);

	evas_object_del(cs->win);
	if (cs->call)
		_call_destroy(cs->call);

	elm_shutdown();

	free(cs);

	return EXIT_SUCCESS;

err_dbus:
	elm_shutdown();

	free(cs);
	return EXIT_FAILURE;
}
ELM_MAIN()
