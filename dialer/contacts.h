#ifndef _EFL_OFONO_CONTACTS_H__
#define _EFL_OFONO_CONTACTS_H__ 1

typedef struct _Contact_Info Contact_Info;
Evas_Object *contacts_add(Evas_Object *parent);

Contact_Info *contact_search(Evas_Object *obj, const char *number, const char **type);

const char *contact_info_picture_get(const Contact_Info *c);

const char *contact_info_name_get(const Contact_Info *c);

const char *contact_info_detail_get(const Contact_Info *c, const char *type);

#endif
