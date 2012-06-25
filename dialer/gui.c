#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "log.h"
#include "gui.h"
#include "keypad.h"
#include "callscreen.h"

static Evas_Object *win = NULL;
static Evas_Object *kp = NULL;
static Evas_Object *cs = NULL;
static Evas_Object *flip = NULL;
static char def_theme[PATH_MAX] = "";

/* XXX elm_flip should just do the right thing, but it does not */
static Eina_Bool in_call = EINA_FALSE;
static Eina_Bool in_flip_anim = EINA_FALSE;

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

static void _popup_close(void *data, Evas_Object *bt __UNUSED__, void *event __UNUSED__)
{
	Evas_Object *popup = data;
	evas_object_del(popup);
}

void gui_simple_popup(const char *title, const char *message)
{
	Evas_Object *p = elm_popup_add(win);
	Evas_Object *bt;

	evas_object_size_hint_weight_set(p, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	elm_object_text_set(p, message);
	elm_object_part_text_set(p, "title,text", title);

	bt = elm_button_add(p);
	elm_object_text_set(bt, "Close");
	elm_object_part_content_set(p, "button1", bt);
	evas_object_smart_callback_add(bt, "clicked", _popup_close, p);
	evas_object_show(p);
}

void gui_activate(void)
{
	elm_win_raise(win);
	elm_win_activate(win);
	evas_object_show(win);
}

void gui_number_set(const char *number, Eina_Bool auto_dial)
{
	/* TODO: show keypad */
	keypad_number_set(kp, number, auto_dial);
}

void gui_call_enter(void)
{
	if (in_call)
		return;
	in_call = EINA_TRUE;
	if (in_flip_anim)
		return;
	in_flip_anim = EINA_TRUE;
	elm_flip_go(flip, ELM_FLIP_ROTATE_Y_CENTER_AXIS);
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
}

static void _gui_call_sync(void *data __UNUSED__, Evas_Object *o __UNUSED__,
				void *event_info __UNUSED__)
{
	Eina_Bool showing_call = !elm_flip_front_visible_get(flip);

	if (showing_call ^ in_call) {
		DBG("Flip back to sync");
		elm_flip_go(flip, ELM_FLIP_ROTATE_Y_CENTER_AXIS);
	}
	in_flip_anim = EINA_FALSE;
}

Eina_Bool gui_init(void)
{
	Evas_Object *lay, *obj;

	/* dialer should never, ever quit */
	elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_NONE);

	elm_app_compile_bin_dir_set(PACKAGE_BIN_DIR);
	elm_app_compile_data_dir_set(PACKAGE_DATA_DIR);
	elm_app_info_set(gui_init, "ofono-efl", "themes/default.edj");

	snprintf(def_theme, sizeof(def_theme), "%s/themes/default.edj",
			elm_app_data_dir_get());

	elm_theme_extension_add(NULL, def_theme);
	elm_theme_overlay_add(NULL, def_theme);

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

	lay = gui_layout_add(win, "main");
	EINA_SAFETY_ON_NULL_RETURN_VAL(lay, EINA_FALSE);
	evas_object_size_hint_weight_set(lay,
				EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
	elm_object_part_content_set(flip, "front", lay);
	evas_object_show(lay);

	kp = obj = keypad_add(win);
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, EINA_FALSE);
	elm_object_part_content_set(lay, "elm.swallow.keypad", obj);

	cs = obj = callscreen_add(win);
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, EINA_FALSE);
	evas_object_size_hint_weight_set(obj,
				EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(obj, EVAS_HINT_FILL, EVAS_HINT_FILL);
	elm_object_part_content_set(flip, "back", obj);
	evas_object_show(obj);

	/* TODO: make it match better with Tizen: icon and other properties */
	evas_object_resize(win, 480, 800);

	/* do not show it yet, RC will check if it should be visible or not */
	return EINA_TRUE;
}

void gui_shutdown(void)
{
}
