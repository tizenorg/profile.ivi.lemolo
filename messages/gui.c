#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "log.h"
#include "gui.h"
#include "compose.h"
#include "util.h"
#include "simple-popup.h"
#include "contacts-ofono-efl.h"

#ifdef HAVE_TIZEN
#include <appcore-efl.h>
#ifdef HAVE_UI_GADGET
#include <ui-gadget.h>
#endif
#include <Ecore_X.h>
#endif

static Evas_Object *win = NULL;
static Evas_Object *main_layout = NULL;
static Evas_Object *cs = NULL;
static Evas_Object *flip = NULL;
static Evas_Object *ov = NULL;
static Evas_Object *contacts = NULL;

/* XXX elm_flip should just do the right thing, but it does not */
static Eina_Bool in_compose = EINA_FALSE;
static Eina_Bool in_flip_anim = EINA_FALSE;

Eina_List * gui_contact_partial_match_search(const char *query)
{
	return contact_partial_match_search(contacts, query);
}

void gui_overview_genlist_update(Message *msg, const char *contact)
{
	overview_genlist_update(ov, msg, contact);
}

void gui_overview_all_contact_messages_clear(const char *contact)
{
	overview_all_contact_messages_clear(ov, contact);
}

void gui_message_from_file_delete(Message *msg, const char *contact)
{
	overview_message_from_file_delete(ov, msg, contact);
}

void gui_compose_messages_set(Eina_List *list, const char *number)
{
	compose_messages_set(cs, list, number);
}

Contact_Info *gui_contact_search(const char *number, const char **type)
{
	return contact_search(contacts, number, type);
}

Evas_Object *gui_simple_popup(const char *title, const char *message)
{
	return simple_popup_add(win, title, message);
}

void gui_activate(void)
{
	elm_win_raise(win);
	elm_win_activate(win);
	evas_object_show(win);
}

void gui_send(const char *number, const char *message, Eina_Bool do_auto)
{
	compose_set(cs, number, message, do_auto);
}

void gui_compose_enter(void)
{
	gui_activate();
	if (in_compose)
		return;
	in_compose = EINA_TRUE;
	if (in_flip_anim)
		return;
	in_flip_anim = EINA_TRUE;
	elm_flip_go(flip, ELM_FLIP_ROTATE_Y_CENTER_AXIS);
	elm_object_focus_set(cs, EINA_TRUE);
}

void gui_compose_exit(void)
{
	if (!in_compose)
		return;
	in_compose = EINA_FALSE;
	if (in_flip_anim)
		return;
	in_flip_anim = EINA_TRUE;
	elm_flip_go(flip, ELM_FLIP_ROTATE_Y_CENTER_AXIS);
	elm_object_focus_set(cs, EINA_FALSE);
}

static void _gui_compose_sync(void *data __UNUSED__, Evas_Object *o __UNUSED__,
				void *event_info __UNUSED__)
{
	Eina_Bool showing_compose = !elm_flip_front_visible_get(flip);

	if (showing_compose ^ in_compose) {
		DBG("Flip back to sync");
		elm_flip_go(flip, ELM_FLIP_ROTATE_Y_CENTER_AXIS);
		elm_object_focus_set(cs, in_compose);
	}
	in_flip_anim = EINA_FALSE;
}

static void _on_clicked(void *data __UNUSED__, Evas_Object *o __UNUSED__,
			const char *emission, const char *source __UNUSED__)
{
	DBG("signal: %s", emission);

	EINA_SAFETY_ON_FALSE_RETURN(eina_str_has_prefix(emission, "clicked,"));
	emission += strlen("clicked,");
}

Eina_Bool gui_init(void)
{
	Evas_Object *lay, *obj, *conform;
	Evas_Coord w, h;

	/* messages should never, ever quit */
	elm_policy_set(ELM_POLICY_QUIT, ELM_POLICY_QUIT_NONE);

	win = elm_win_util_standard_add("ofono-messages", "oFono Messages");
	EINA_SAFETY_ON_NULL_RETURN_VAL(win, EINA_FALSE);
	elm_win_autodel_set(win, EINA_FALSE);
	elm_win_conformant_set(win, EINA_TRUE);

#ifdef HAVE_TIZEN
	appcore_set_i18n("ofono-efl", "en-US");
#ifdef HAVE_UI_GADGET
	UG_INIT_EFL(win, UG_OPT_INDICATOR_PORTRAIT_ONLY);
#endif
#endif

	flip = elm_flip_add(win);
	evas_object_size_hint_weight_set(flip,
				EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(flip, EVAS_HINT_FILL, EVAS_HINT_FILL);
	elm_win_resize_object_add(win, flip);
	evas_object_smart_callback_add(flip, "animate,done",
					_gui_compose_sync, NULL);
	evas_object_show(flip);

	main_layout = lay = layout_add(win, "messages");
	EINA_SAFETY_ON_NULL_RETURN_VAL(lay, EINA_FALSE);
	evas_object_size_hint_weight_set(lay,
				EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
	elm_object_part_content_set(flip, "front", lay);
	evas_object_show(lay);

	elm_object_signal_callback_add(lay, "clicked,*", "gui",
					_on_clicked, NULL);

	contacts = obj = contacts_add(win);
	EINA_SAFETY_ON_NULL_RETURN_VAL(contacts, EINA_FALSE);

	cs = obj = compose_add(win);
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, EINA_FALSE);
	evas_object_size_hint_weight_set(obj,
				EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(obj, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(obj);

	ov = obj = overview_add(win);
	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, EINA_FALSE);
	elm_object_part_content_set(lay, "elm.swallow.overview", obj);
	evas_object_show(ov);

	conform = elm_conformant_add(win);
	EINA_SAFETY_ON_NULL_RETURN_VAL(conform, EINA_FALSE);
	elm_win_resize_object_add(win, conform);
	evas_object_size_hint_weight_set(conform, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_show(conform);
	elm_object_content_set(conform, cs);
	elm_object_part_content_set(flip, "back", conform);

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
