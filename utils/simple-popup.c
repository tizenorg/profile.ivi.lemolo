#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

#include "simple-popup.h"
#include "util.h"

typedef struct _Simple_Popup
{
	Evas_Object *popup;
	Evas_Object *box;
	Evas_Object *message;
	Evas_Object *entry;
	Ecore_Timer *recalc_timer; /* HACK */
} Simple_Popup;

static void _popup_close(void *data, Evas_Object *bt __UNUSED__,
				void *event __UNUSED__)
{
	Evas_Object *popup = data;
	evas_object_del(popup);
}

void simple_popup_title_set(Evas_Object *p, const char *title)
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
static Eina_Bool _simple_popup_entries_reeval(void *data)
{
	Simple_Popup *ctx = data;
	if (ctx->message)
		elm_entry_calc_force(ctx->message);
	if (ctx->entry)
		elm_entry_calc_force(ctx->entry);
	ctx->recalc_timer = NULL;
	return EINA_FALSE;
}

static void _simple_popup_timer_cancel_if_needed(Simple_Popup *ctx)
{
	if (!ctx->recalc_timer)
		return;
	if (ctx->message || ctx->entry)
		return;
	ecore_timer_del(ctx->recalc_timer);
	ctx->recalc_timer = NULL;
}

static void _simple_popup_message_del(void *data, Evas *e __UNUSED__,
						Evas_Object *en __UNUSED__,
						void *einfo __UNUSED__)
{
	Simple_Popup *ctx = data;
	ctx->message = NULL;
	_simple_popup_timer_cancel_if_needed(ctx);
}

static void _simple_popup_entry_del(void *data, Evas *e __UNUSED__,
					Evas_Object *en __UNUSED__,
					void *einfo __UNUSED__)
{
	Simple_Popup *ctx = data;
	ctx->entry = NULL;
	_simple_popup_timer_cancel_if_needed(ctx);
}

static void _simple_popup_reeval_content(Simple_Popup *ctx)
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
		0.02, _simple_popup_entries_reeval, ctx);
}

void simple_popup_message_set(Evas_Object *p, const char *msg)
{
	Simple_Popup *ctx = evas_object_data_get(p, "simple_popup");
	Evas_Object *en;

	EINA_SAFETY_ON_NULL_RETURN(ctx);

	if (!msg) {
		if (ctx->message)
			evas_object_del(ctx->message);
		_simple_popup_reeval_content(ctx);
		return;
	}

	if (ctx->message)
		en = ctx->message;
	else {
		en = ctx->message = elm_entry_add(p);
		elm_object_style_set(en, "dialer-popup");
		elm_entry_editable_set(en, EINA_FALSE);
		elm_entry_scrollable_set(en, EINA_TRUE);
#ifdef HAVE_TIZEN
		/* old, deprecated API */
		elm_entry_scrollbar_policy_set(en, ELM_SCROLLER_POLICY_OFF,
						ELM_SCROLLER_POLICY_AUTO);
#else
		elm_scroller_policy_set(en, ELM_SCROLLER_POLICY_OFF,
					ELM_SCROLLER_POLICY_AUTO);
#endif

		evas_object_event_callback_add(en, EVAS_CALLBACK_DEL,
					_simple_popup_message_del,
					ctx);
		evas_object_size_hint_weight_set(en, EVAS_HINT_EXPAND,
							EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(en, EVAS_HINT_FILL,
						EVAS_HINT_FILL);
	}

	elm_object_text_set(en, msg);
	_simple_popup_reeval_content(ctx);
}

void simple_popup_entry_enable(Evas_Object *p)
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
#ifdef HAVE_TIZEN
	/* old, deprecated API */
	elm_entry_scrollbar_policy_set(en, ELM_SCROLLER_POLICY_OFF,
					ELM_SCROLLER_POLICY_AUTO);
#else
	elm_scroller_policy_set(en, ELM_SCROLLER_POLICY_OFF,
				ELM_SCROLLER_POLICY_AUTO);
#endif

	evas_object_size_hint_weight_set(en, EVAS_HINT_EXPAND,
						EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(en, EVAS_HINT_FILL,
					EVAS_HINT_FILL);

	evas_object_event_callback_add(en, EVAS_CALLBACK_DEL,
					_simple_popup_entry_del,
					ctx);
	_simple_popup_reeval_content(ctx);
}

void simple_popup_entry_disable(Evas_Object *p)
{
	Simple_Popup *ctx = evas_object_data_get(p, "simple_popup");
	EINA_SAFETY_ON_NULL_RETURN(ctx);
	if (!ctx->entry)
		return;
	evas_object_del(ctx->entry);
	_simple_popup_reeval_content(ctx);
}

const char *simple_popup_entry_get(const Evas_Object *p)
{
	Simple_Popup *ctx = evas_object_data_get(p, "simple_popup");
	EINA_SAFETY_ON_NULL_RETURN_VAL(ctx, NULL);
	EINA_SAFETY_ON_NULL_RETURN_VAL(ctx->entry, NULL);
	return elm_object_text_get(ctx->entry);
}

void simple_popup_button_dismiss_set(Evas_Object *p)
{
	simple_popup_buttons_set(p,
					"Dismiss",
					"dialer",
					_popup_close,
					NULL, NULL, NULL,
					p);
}

void simple_popup_buttons_set(Evas_Object *p,
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

static void _simple_popup_del(void *data, Evas *e __UNUSED__,
					Evas_Object *o __UNUSED__,
					void *event_info __UNUSED__)
{
	Simple_Popup *ctx = data;
	free(ctx);
}

Evas_Object *simple_popup_add(Evas_Object *win,
				const char *title, const char *message)
{
	Evas_Object *p = layout_add(win, "popup");
	Simple_Popup *ctx;

	EINA_SAFETY_ON_NULL_RETURN_VAL(p, NULL);

	ctx = calloc(1, sizeof(Simple_Popup));
	EINA_SAFETY_ON_NULL_GOTO(ctx, failed_calloc);

	evas_object_data_set(p, "simple_popup", ctx);
	ctx->popup = p;
	evas_object_event_callback_add(p, EVAS_CALLBACK_DEL,
					_simple_popup_del, ctx);

	evas_object_size_hint_weight_set(p, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	elm_win_resize_object_add(win, p);

	ctx->box = elm_box_add(p);

	simple_popup_title_set(p, title);
	simple_popup_message_set(p, message);
	simple_popup_button_dismiss_set(p);

	evas_object_show(p);

	return p;

failed_calloc:
	evas_object_del(p);
	return NULL;
}
