#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>

Evas_Object *contacts_add(Evas_Object *parent) {
	Evas_Object *obj = elm_label_add(parent);
	elm_object_text_set(obj, "TO BE DONE");
	return obj;
}
