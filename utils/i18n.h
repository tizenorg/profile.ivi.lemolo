#ifndef _I18N_H__
#define _I18N_H__

#ifdef HAVE_TIZEN
#include <appcore-efl.h>
#else
#include <libintl.h>
#define _(String) gettext (String)
#define gettext_noop(String) String
#define N_(String) gettext_noop (String)
#endif

typedef struct _Locale_Callback_List_Node Locale_Callback_List_Node;

Locale_Callback_List_Node *locale_properties_changed_cb_add(void (*cb)(void *data), const void *data);

void locale_properties_changed_cb_del(Locale_Callback_List_Node *callback_node);

const char* locale_lang_get(void);

Eina_Bool locale_init(void);

void locale_shutdown(void);

#endif
