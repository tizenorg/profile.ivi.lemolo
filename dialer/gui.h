#ifndef _EFL_OFONO_GUI_H__
#define _EFL_OFONO_GUI_H__ 1

#include "contacts.h"

Evas_Object *gui_layout_add(Evas_Object *parent, const char *style);

void gui_simple_popup(const char *title, const char *message);

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
