#ifndef _EFL_OFONO_GUI_H__
#define _EFL_OFONO_GUI_H__ 1

#include "contacts-ofono-efl.h"
#include "ofono.h"
#include "overview.h"
#include <Eina.h>

Evas_Object *gui_simple_popup(const char *title, const char *message);

void gui_activate(void);
void gui_send(const char *number, const char *message, Eina_Bool auto_dial);

void gui_compose_enter(void);
void gui_compose_exit(void);

Eina_Bool gui_init(void);
void gui_shutdown(void);

Contact_Info *gui_contact_search(const char *number, const char **type);

void gui_compose_messages_set(Eina_List *list, const char *number);

void gui_message_from_file_delete(Message *msg, const char *contact);

void gui_overview_genlist_update(Message *msg, const char *contact);

void gui_overview_all_contact_messages_clear(const char *contact);

Eina_List *gui_contact_partial_match_search(const char *query);

#endif
