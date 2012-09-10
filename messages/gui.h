#ifndef _EFL_OFONO_GUI_H__
#define _EFL_OFONO_GUI_H__ 1

#include "contacts-ofono-efl.h"
#include "ofono.h"

Evas_Object *gui_simple_popup(const char *title, const char *message);

void gui_activate(void);
void gui_send(const char *number, const char *message, Eina_Bool auto_dial);

void gui_compose_enter(void);
void gui_compose_exit(void);

Eina_Bool gui_init(void);
void gui_shutdown(void);

#endif
