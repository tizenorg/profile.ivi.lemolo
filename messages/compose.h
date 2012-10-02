#ifndef _EFL_OFONO_COMPOSE_H__
#define _EFL_OFONO_COMPOSE_H__ 1

Evas_Object *compose_add(Evas_Object *parent);

void compose_set(Evas_Object *obj, const char *number, const char *message,
			Eina_Bool do_auto);

void compose_messages_set(Evas_Object *obj, Eina_List *list, const char *number);

#endif
