#ifndef _EFL_OFONO_CONTACTS_H__
#define _EFL_OFONO_CONTACTS_H__ 1

typedef struct _Contact_Info Contact_Info;

Evas_Object *contacts_add(Evas_Object *parent);

Contact_Info *contact_search(Evas_Object *obj, const char *number, const char **type);

const char *contact_info_picture_get(const Contact_Info *c);

const char *contact_info_full_name_get(const Contact_Info *c);
const char *contact_info_first_name_get(const Contact_Info *c);
const char *contact_info_last_name_get(const Contact_Info *c);

Eina_List *contact_info_all_numbers_get(const Contact_Info *c);

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

typedef struct _Contact_Partial_Match Contact_Partial_Match;
Eina_List *contact_partial_match_search(Evas_Object *obj, const char *query);
void contact_partial_match_search_free(Eina_List *results);
const char *contact_partial_match_type_get(const Contact_Partial_Match *pm);
const Contact_Info *contact_partial_match_info_get(const Contact_Partial_Match *pm);
Eina_Bool contact_partial_match_name_match_get(const Contact_Partial_Match *pm);

#endif
