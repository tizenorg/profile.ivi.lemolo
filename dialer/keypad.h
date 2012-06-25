#ifndef _EFL_OFONO_KEYPAD_H__
#define _EFL_OFONO_KEYPAD_H__ 1

Evas_Object *keypad_add(Evas_Object *parent);
void keypad_number_set(Evas_Object *obj, const char *number, Eina_Bool do_auto);

#endif
