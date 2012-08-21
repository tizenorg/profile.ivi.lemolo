#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "log.h"
#include "gui.h"
#include "keypad.h"
#include "history.h"
#include "callscreen.h"
#include "ussd.h"

static Evas_Object *win = NULL;
static Evas_Object *main_layout = NULL;
static Evas_Object *keypad = NULL;
static Evas_Object *contacts = NULL;
static Evas_Object *history = NULL;
static Evas_Object *cs = NULL;
static Evas_Object *flip = NULL;
static char def_theme[PATH_MAX] = "";

static OFono_Callback_List_Modem_Node *callback_node_modem_changed = NULL;
static OFono_Callback_List_USSD_Notify_Node *callback_node_ussd_notify = NULL;

/* XXX elm_flip should just do the right thing, but it does not */
static Eina_Bool in_call = EINA_FALSE;
static Eina_Bool in_flip_anim = EINA_FALSE;

Contact_Info *gui_contact_search(const char *number, const char **type)
{
	return contact_search(contacts, number, type);
}

Evas_Object *gui_layout_add(Evas_Object *parent, const char *style)
{
	Evas_Object *layout = elm_layout_add(parent);
	if (!elm_layout_theme_set(layout, "layout", "dialer", style)) {
		CRITICAL("No theme for 'elm/layout/dialer/%s' at %s",
				style, def_theme);
		evas_object_del(layout);
		return NULL;
	}
	return layout;
}

static void _gui_show(Evas_Object *o)
{
	if (o == keypad)
		elm_object_signal_emit(main_layout, "show,keypad", "gui");
	else if (o == contacts)
		elm_object_signal_emit(main_layout, "show,contacts", "gui");
	else if (o == history)
		elm_object_signal_emit(main_layout, "show,history", "gui");
	elm_object_focus_set(o, EINA_TRUE);
}

typedef struct _Simple_Popup
{
	Evas_Object *popup;
	Evas_Object *box;
	Evas_Object *message;
	Evas_Object *entry;
	Ecore_Timer *recalc_timer; /* HACK */
} Simple_Popup;

static void _popup_close(void *data, Evas_Object *bt __UNUSED__, void *event __UNUSED__)
{
	Evas_Object *popup = data;
	evas_object_del(popup);
}

void gui_simple_popup_title_set(Evas_Object *p, const char *title)
{
	if (title) {
		elm_object_part_text_set(p, "elm.text.title", title);
		elm_object_signal_emit(p, "show,title", "gui");
	} else {
		elm_object_part_text_set(p, "elm.text.title", "");
		elm_object_signal_emit(p, "hide,title", "gui");
	}
}

/* HACK: force recalc from an idler to fix elm_entry problem */
static Eina_Bool _gui_simple_popup_entries_reeval(void *data)
{
	Simple_Popup *ctx = data;
	if (ctx->message)
		elm_entry_calc_force(ctx->message);
	if (ctx->entry)
		elm_entry_calc_force(ctx->entry);
	ctx->recalc_timer = NULL;
	return EINA_FALSE;
}

static void _gui_simple_popup_timer_cancel_if_needed(Simple_Popup *ctx)
{
	if (!ctx->recalc_timer)
		return;
	if (ctx->message || ctx->entry)
		return;
	ecore_timer_del(ctx->recalc_timer);
	ctx->recalc_timer = NULL;
}

static void _gui_simple_popup_message_del(void *data, Evas *e __UNUSED__,
						Evas_Object *en __UNUSED__,
						void *einfo __UNUSED__)
{
	Simple_Popup *ctx = data;
	ctx->message = NULL;
	_gui_simple_popup_timer_cancel_if_needed(ctx);
}

static void _gui_simple_popup_entry_del(void *data, Evas *e __UNUSED__,
					Evas_Object *en __UNUSED__,
					void *einfo __UNUSED__)
{
	Simple_Popup *ctx = data;
	ctx->entry = NULL;
	_gui_simple_popup_timer_cancel_if_needed(ctx);
}

static void _gui_simple_popup_reeval_content(Simple_Popup *ctx)
{
	if ((!ctx->message) && (!ctx->entry)) {
		elm_object_part_content_unset(ctx->popup,
						"elm.swallow.content");
		elm_object_signal_emit(ctx->popup, "hide,content", "gui");
		evas_object_hide(ctx->box);
		return;
	}

	elm_box_unpack_all(ctx->box);
	if (ctx->message) {
		elm_box_pack_end(ctx->box, ctx->message);
		evas_object_show(ctx->message);
	}
	if (ctx->entry) {
		elm_box_pack_end(ctx->box, ctx->entry);
		evas_object_show(ctx->entry);
	}

	elm_object_part_content_set(ctx->popup, "elm.swallow.content",
					ctx->box);
	elm_object_signal_emit(ctx->popup, "show,content", "gui");

	/* HACK: elm_entry is not evaluating properly and the text is
	 * not centered as it should be. Then we must force a
	 * calculation from an timer.
	 */
	if (ctx->recalc_timer)
		ecore_timer_del(ctx->recalc_timer);
	ctx->recalc_timer = ecore_timer_add(
		0.02, _gui_simple_popup_entries_reeval, ctx);
}

void gui_simple_popup_message_set(Evas_Object *p, const char *msg)
{
	Simple_Popup *ctx = evas_object_data_get(p, "simple_popup");
	Evas_Object *en;

	EINA_SAFETY_ON_NULL_RETURN(ctx);

	if (!msg) {
		if (ctx->message)
			evas_object_del(ctx->message);
		_gui_simple_popup_reeval_content(ctx);
		return;
	}

	if (ctx->message)
		en = ctx->message;
	else {
		en = ctx->message = elm_entry_add(p);
		elm_object_style_set(en, "dialer-popup");
		elm_entry_editable_set(en, EINA_FALSE);
		elm_entry_scrollable_set(en, EINA_TRUE);
		elm_entry_scrollbar_policy_set(en, ELM_SCROLLER_POLICY_OFF,
						ELM_SCROLLER_POLICY_AUTO);

		evas_object_event_callback_add(en, EVAS_CALLBACK_DEL,
					_gui_simple_popup_message_del,
					ctx);
		evas_object_size_hint_weight_set(en, EVAS_HINT_EXPAND,
							EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(en, EVAS_HINT_FILL,
						EVAS_HINT_FILL);
	}

	elm_object_text_set(en, msg);
	_gui_simple_popup_reeval_content(ctx);
}

void gui_simple_popup_entry_enable(Evas_Object *p)
{
	Simple_Popup *ctx = evas_object_data_get(p, "simple_popup");
	Evas_Object *en;

	EINA_SAFETY_ON_NULL_RETURN(ctx);

	if (ctx->entry)
		return;
	en = ctx->entry = elm_entry_add(p);
	elm_object_style_set(en, "dialer-popup-editable");
	elm_entry_editable_set(en, EINA_TRUE);
	elm_entry_scrollable_set(en, EINA_TRUE);
	elm_entry_scrollbar_policy_set(en, ELM_SCROLLER_POLICY_OFF,
					ELM_SCROLLER_POLICY_AUTO);

	evas_object_size_hint_weight_set(en, EVAS_HINT_EXPAND,
						EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(en, EVAS_HINT_FILL,
					EVAS_HINT_FILL);

	evas_object_event_callback_add(en, EVAS_CALLBACK_DEL,
					_gui_simple_popup_entry_del,
					ctx);
	_gui_simple_popup_reeval_content(ctx);
}

void gui_simple_popup_entry_disable(Evas_Object *p)
{
	Simple_Popup *ctx = evas_object_data_get(p, "simple_popup");
	EINA_SAFETY_ON_NULL_RETURN(ctx);
	if (!ctx->entry)
		return;
	evas_object_del(ctx->entry);
	_gui_simple_popup_reeval_content(ctx);
}

const char *gui_simple_popup_entry_get(const Evas_Object *p)
{
	Simple_Popup *ctx = evas_object_data_get(p, "simple_popup");
	EINA_SAFETY_ON_NULL_RETURN_VAL(ctx, NULL);
	EINA_SAFETY_ON_NULL_RETURN_VAL(ctx->entry, NULL);
	return elm_object_text_get(ctx->entry);
}

void gui_simple_popup_button_dismiss_set(Evas_Object *p)
{
	gui_simple_popup_buttons_set(p,
					"Dismiss",
					"dialer",
					_popup_close,
					NULL, NULL, NULL,
					p);
}

void gui_simple_popup_buttons_set(Evas_Object *p,
					const char *b1_label,
					const char *b1_class,
					Evas_Smart_Cb b1_cb,
					const char *b2_label,
					const char *b2_class,
					Evas_Smart_Cb b2_cb,
					const void *data)
{
	Evas_Object *bt;
	unsigned int count = 0;

	if (b1_label) {
		bt = elm_button_add(p);
		elm_object_style_set(bt, b1_class ? b1_class : "dialer");
		elm_object_text_set(bt, b1_label);
		elm_object_part_content_set(p, "elm.swallow.button1", bt);
		evas_object_smart_callback_add(bt, "clicked", b1_cb, data);
		count++;
	}

	if (b2_label) {
		const char *part;
		bt = elm_button_add(p);
		elm_object_style_set(bt, b2_class ? b2_class : "dialer");
		elm_object_text_set(bt, b2_label);

		if (count == 1)
			part = "elm.swallow.button2";
		else
			part = "elm.swallow.button1";

		elm_object_part_content_set(p, part, bt);
		evas_object_smart_callback_add(bt, "clicked", b2_cb, data);
		count++;
	}

	if (count == 2)
		elm_object_signal_emit(p, "buttons,2", "gui");
	else if (count == 1) {
		elm_object_signal_emit(p, "buttons,1", "gui");
		elm_object_part_content_set(p, "elm.swallow.button2", NULL);
	} else {
		elm_object_signal_emit(p, "buttons,0", "gui");
		elm_object_part_content_set(p, "elm.swallow.button1", NULL);
		elm_object_part_content_set(p, "elm.swallow.button2", NULL);
	}
}

static void _gui_simple_popup_del(void *data, Evas *e __UNUSED__,
					Evas_Object *o __UNUSED__,
					void *event_info __UNUSED__)
{
	Simple_Popup *ctx = data;
	free(ctx);
}

Evas_Object *gui_simple_popup(const char *title, const char *message)
{
	Evas_Object *p = gui_layout_add(win, "popup");
	Simple_Popup *ctx;

	EINA_SAFETY_ON_NULL_RETURN_VAL(p, NULL);

	ctx = calloc(1, sizeof(Simple_Popup));
	EINA_SAFETY_ON_NULL_GOTO(ctx, failed_calloc);

	evas_object_data_set(p, "simple_popup", ctx);
	ctx->popup = p;
	evas_object_event_callback_add(p, EVAS_CALLBACK_DEL,
					_gui_simple_popup_del, ctx);

	evas_object_size_hint_weight_set(p, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	elm_win_resize_object_add(win, p);

	ctx->box = elm_box_add(p);

	gui_simple_popup_title_set(p, title);
	gui_simple_popup_message_set(p, message);
	gui_simple_popup_button_dismiss_set(p);

	evas_object_show(p);

	return p;

failed_calloc:
	evas_object_del(p);
	return NULL;
}

void gui_activate(void)
{
	elm_win_raise(win);
	elm_win_activate(win);
	evas_object_show(win);
}

void gui_number_set(const char *number, Eina_Bool auto_dial)
{
	_gui_show(keypad);
	keypad_number_set(keypad, number, auto_dial);
}

void gui_activecall_set(Evas_Object *o)
{
	if (o) {
		elm_object_part_content_set(main_layout,
						"elm.swallow.activecall", o);
		elm_object_signal_emit(main_layout, "show,activecall", "gui");
	} else {
		o = elm_object_part_content_unset(
			main_layout, "elm.swallow.activecall");
		elm_object_signal_emit(main_layout, "hide,activecall", "gui");
		evas_object_hide(o);
	}
}

void gui_contacts_show(void)
{
	_gui_show(contacts);
	gui_call_exit();
}

void gui_call_enter(void)
{
	gui_activate();
	if (in_call)
		return;
	in_call = EINA_TRUE;
	if (in_flip_anim)
		return;
	in_flip_anim = EINA_TRUE;
	elm_flip_go(flip, ELM_FLIP_ROTATE_Y_CENTER_AXIS);
	elm_object_focus_set(cs, EINA_TRUE);
}

void gui_call_exit(void)
{
	if (!in_call)
		return;
	in_call = EINA_FALSE;
	if (in_flip_anim)
		return;
	in_flip_anim = EINA_TRUE;
	elm_flip_go(flip, ELM_FLIP_ROTATE_Y_CENTER_AXIS);
	elm_object_focus_set(cs, EINA_FALSE);
}

static void _gui_call_sync(void *data __UNUSED__, Evas_Object *o __UNUSED__,
				void *event_info __UNUSED__)
{
	Eina_Bool showing_call = !elm_flip_front_visible_get(flip);

	if (showing_call ^ in_call) {
		DBG("Flip back to sync");
		elm_flip_go(flip, ELM_FLIP_ROTATE_Y_CENTER_AXIS);
		elm_object_focus_set(cs, in_call);
	}
	in_flip_anim = EINA_FALSE;
}

static void _dial_reply(void *data, OFono_Error err,
			OFono_Call *call __UNUSED__)
{
	const char *number = data;

	if (err != OFONO_ERROR_NONE) {
		char buf[1024];
		const char *msg = ofono_error_message_get(err);
		snprintf(buf, sizeof(buf), "Could not call %s: %s",
				number, msg);
		gui_simple_popup("Error", buf);
	}
}

static void _gui_voicemail(void)
{
	const char *number = ofono_voicemail_number_get();
	EINA_SAFETY_ON_NULL_RETURN(number);
	EINA_SAFETY_ON_FALSE_RETURN(*number != '\0');

	ofono_dial(number, NULL, _dial_reply, number);
}

static void _on_clicked(void *data __UNUSED__, Evas_Object *o __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	DBG("signal: %s", emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "clicked,"));
	emission += strlen("clicked,");

	if (strcmp(emission, "keypad") == 0)
		_gui_show(keypad);
	else if (strcmp(emission, "contacts") == 0)
		_gui_show(contacts);
	else if (strcmp(emission, "history") == 0)
		_gui_show(history);
	else if (strcmp(emission, "voicemail") == 0)
		_gui_voicemail();
}

static void _ofono_changed(void *data __UNUSED__)
{
	const char *number;
	Eina_Bool waiting;
	unsigned char count;
	char buf[32];

	if ((ofono_modem_api_get() & OFONO_API_MSG_WAITING) == 0) {
		elm_object_signal_emit(main_layout, "disable,voicemail", "gui");
		elm_object_signal_emit(main_layout,
					"toggle,off,voicemail", "gui");
		elm_object_part_text_set(main_layout, "elm.text.voicemail", "");
		return;
	}

	number = ofono_voicemail_number_get();
	waiting = ofono_voicemail_waiting_get();
	count = ofono_voicemail_count_get();

	if (number)
		elm_object_signal_emit(main_layout, "enable,voicemail", "gui");
	else
		elm_object_signal_emit(main_layout, "disable,voicemail", "gui");

	if (waiting) {
		if (count == 0)
			snprintf(buf, sizeof(buf), "*");
		else if (count < 255)
			snprintf(buf, sizeof(buf), "%d", count);
		else
			snprintf(buf, sizeof(buf), "255+");

		elm_object_signal_emit(main_layout,
					"toggle,on,voicemail", "gui");
	} else {
		buf[0] = '\0';
		elm_object_signal_emit(main_layout,
					"toggle,off,voicemail", "gui");
	}

	elm_object_part_text_set(main_layout, "elm.text.voicemail", buf);
}

static void _ofono_ussd_notify(void *data __UNUSED__, Eina_Bool needs_reply,
				const char *message)
{
	DBG("needs_reply=%d, message=%s", needs_reply, message);
	ussd_start(message);
}

Eina_Bool gui_init(const char *theme)
{
	Evas_Object *lay, *obj;
	Evas_Coord w, h;

	/* dialer should never, ever quit */
	elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_NONE);

	elm_app_compile_bin_dir_set(PACKAGE_BIN_DIR);
	elm_app_compile_data_dir_set(PACKAGE_DATA_DIR);
	elm_app_info_set(gui_init, "ofono-efl", "themes/default.edj");

	snprintf(def_theme, sizeof(def_theme), "%s/themes/default.edj",
			elm_app_data_dir_get());

	elm_theme_extension_add(NULL, def_theme);
	if (!theme)
		elm_theme_overlay_add(NULL, def_theme);
	else {
		char tmp[PATH_MAX];
		if (theme[0] != '/') {
			if (eina_str_has_suffix(theme, ".edj")) {
				snprintf(tmp, sizeof(tmp), "%s/themes/%s",
						elm_app_data_dir_get(), theme);
			} else {
				snprintf(tmp, sizeof(tmp), "%s/themes/%s.edj",
						elm_app_data_dir_get(), theme);
			}
			theme = tmp;
		}
		elm_theme_overlay_add(NULL, theme);
	}

	win = elm_win_util_standard_add("ofono-dialer", "oFono Dialer");
	EINA_SAFETY_ON_NULL_RETURN_VAL(win, EINA_FALSE);
	elm_win_autodel_set(win, EINA_FALSE);

	flip = elm_flip_add(win);
	evas_object_size_hint_weight_set(flip,
				EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(flip, EVAS_HINT_FILL, EVAS_HINT_FILL);
	elm_win_resize_object_add(win, flip);
	evas_object_smart_callback_add(flip, "animate,done",
					_gui_call_sync, NULL);
	evas_object_show(flip);

	main_layout = lay = gui_layout_add(win, "main");
	EINA_SAFETY_ON_NULL_RETURN_VAL(lay, EINA_FALSE);
	evas_object_size_hint_weight_set(lay,
				EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
	elm_object_part_content_set(flip, "front", lay);
	evas_object_show(lay);

	elm_object_signal_callback_add(lay, "clicked,*", "gui",
					_on_clicked, NULL);

	keypad = obj = keypad_add(win);
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, EINA_FALSE);
	elm_object_part_content_set(lay, "elm.swallow.keypad", obj);

	contacts = obj = contacts_add(win);
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, EINA_FALSE);
	elm_object_part_content_set(lay, "elm.swallow.contacts", obj);

	history = obj = history_add(win);
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, EINA_FALSE);
	elm_object_part_content_set(lay, "elm.swallow.history", obj);

	_gui_show(keypad);

	cs = obj = callscreen_add(win);
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, EINA_FALSE);
	evas_object_size_hint_weight_set(obj,
				EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(obj, EVAS_HINT_FILL, EVAS_HINT_FILL);
	elm_object_part_content_set(flip, "back", obj);
	evas_object_show(obj);

	callback_node_modem_changed =
		ofono_modem_changed_cb_add(_ofono_changed, NULL);
	callback_node_ussd_notify =
		ofono_ussd_notify_cb_add(_ofono_ussd_notify, NULL);

	/* TODO: make it match better with Tizen: icon and other properties */
	obj = elm_layout_edje_get(lay);
	edje_object_size_min_get(obj, &w, &h);
	if ((w == 0) || (h == 0))
		edje_object_size_min_restricted_calc(obj, &w, &h, w, h);
	if ((w == 0) || (h == 0))
		edje_object_parts_extends_calc(obj, NULL, NULL, &w, &h);
	evas_object_resize(win, w, h);

	/* do not show it yet, RC will check if it should be visible or not */
	return EINA_TRUE;
}

void gui_shutdown(void)
{
}
