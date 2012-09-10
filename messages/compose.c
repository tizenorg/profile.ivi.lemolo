#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include "log.h"
#include "util.h"

typedef struct _Compose
{
	Evas_Object *layout;
} Compose;

static void _on_del(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Compose *compose = data;
	free(compose);
}

Evas_Object *compose_add(Evas_Object *parent)
{
	Compose *compose;
	Evas_Object *obj;

	compose = calloc(1, sizeof(Compose));
	EINA_SAFETY_ON_NULL_RETURN_VAL(compose, NULL);

	compose->layout = obj = layout_add(parent, "compose");
	EINA_SAFETY_ON_NULL_GOTO(obj, err_obj);

	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _on_del,
					compose);

	evas_object_data_set(obj, "compose.ctx", compose);
	return obj;

err_obj:
	free(compose);
	return NULL;
}

void compose_set(Evas_Object *obj, const char *number, const char *message,
			Eina_Bool do_auto)
{
	Compose *compose;

	EINA_SAFETY_ON_NULL_RETURN(obj);
	EINA_SAFETY_ON_NULL_RETURN(number);
	EINA_SAFETY_ON_NULL_RETURN(message);

	compose = evas_object_data_get(obj, "compose.ctx");
	EINA_SAFETY_ON_NULL_RETURN(compose);

	ERR("TODO '%s' '%s' %d", number, message, do_auto);
}
