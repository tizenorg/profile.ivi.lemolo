#ifndef _EFL_OFONO_CONTACTS_H__
#define _EFL_OFONO_CONTACTS_H__ 1

typedef struct _Contact_Info Contact_Info;

Evas_Object *contacts_add(Evas_Object *parent);

Contact_Info *contact_search(Evas_Object *obj, const char *number, const char **type);

const char *contact_info_picture_get(const Contact_Info *c);

const char *contact_info_full_name_get(const Contact_Info *c);
const char *contact_info_first_name_get(const Contact_Info *c);
const char *contact_info_last_name_get(const Contact_Info *c);

const char *contact_info_detail_get(const Contact_Info *c, const char *type);

const char *contact_info_number_check(const Contact_Info *c, const char *number);

Eina_Bool contact_info_picture_set(Contact_Info *c, const char *filename);

Eina_Bool contact_info_first_name_set(Contact_Info *c, const char *name);

Eina_Bool contact_info_last_name_set(Contact_Info *c, const char *name);

Eina_Bool contact_info_detail_set(Contact_Info *c, const char *type, const char *number);

void contact_info_on_changed_callback_add(Contact_Info *c, void (*cb)(void *data, Contact_Info *c), const void *data);
void contact_info_on_changed_callback_del(Contact_Info *c, void (*cb)(void *data, Contact_Info *c), const void *data);

void contact_info_on_del_callback_add(Contact_Info *c, void (*cb)(void *data, const Contact_Info *c), const void *data);
void contact_info_on_del_callback_del(Contact_Info *c, void (*cb)(void *data, const Contact_Info *c), const void *data);
void contact_info_del(Contact_Info *c);

#endif
