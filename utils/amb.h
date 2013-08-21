#ifndef _AMB_H__
#define _AMB_H__

typedef enum
{
	AMB_ERROR_NONE = 0,
	AMB_ERROR_FAILED
} AMB_Error;

typedef struct _AMB_Callback_List_Node AMB_Callback_List_Node;

AMB_Callback_List_Node *amb_properties_changed_cb_add(void (*cb)(void *data), const void *data);

void amb_properties_changed_cb_del(AMB_Callback_List_Node *callback_node);

Eina_Bool amb_night_mode_get(void);

Eina_Bool amb_init(void);

void amb_shutdown(void);

#endif
