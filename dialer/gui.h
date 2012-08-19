#ifndef _EFL_OFONO_GUI_H__
#define _EFL_OFONO_GUI_H__ 1

#include "contacts.h"

Evas_Object *gui_layout_add(Evas_Object *parent, const char *style);

Evas_Object *gui_simple_popup(const char *title, const char *message);

void gui_simple_popup_title_set(Evas_Object *p, const char *title);
void gui_simple_popup_message_set(Evas_Object *p, const char *msg);

void gui_simple_popup_button_dismiss_set(Evas_Object *p);
void gui_simple_popup_buttons_set(Evas_Object *p,
					const char *b1_label,
					const char *b1_class,
					Evas_Smart_Cb b1_cb,
					const char *b2_label,
					const char *b2_class,
					Evas_Smart_Cb b2_cb,
					const void *data);


void gui_activate(void);
void gui_number_set(const char *number, Eina_Bool auto_dial);

void gui_activecall_set(Evas_Object *o);

void gui_contacts_show(void);

void gui_call_enter(void);
void gui_call_exit(void);

Eina_Bool gui_init(const char *theme);
void gui_shutdown(void);

Contact_Info *gui_contact_search(const char *number, const char **type);

#endif
