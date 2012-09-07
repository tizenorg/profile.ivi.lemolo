#ifndef _EFL_OFONO_SIMPLE_POPUP_H__
#define _EFL_OFONO_SIMPLE_POPUP_H__ 1

Evas_Object *simple_popup_add(Evas_Object *win,
				const char *title, const char *message);

void simple_popup_title_set(Evas_Object *p, const char *title);
void simple_popup_message_set(Evas_Object *p, const char *msg);

void simple_popup_button_dismiss_set(Evas_Object *p);
void simple_popup_buttons_set(Evas_Object *p,
				const char *b1_label,
				const char *b1_class,
				Evas_Smart_Cb b1_cb,
				const char *b2_label,
				const char *b2_class,
				Evas_Smart_Cb b2_cb,
				const void *data);

void simple_popup_entry_enable(Evas_Object *p);
void simple_popup_entry_disable(Evas_Object *p);
const char *simple_popup_entry_get(const Evas_Object *p);

#endif
