#ifndef _EFL_OFONO_CONTACTS_H__
#define _EFL_OFONO_CONTACTS_H__ 1

typedef struct _Contact_Info {
	const char *name;
	const char *mobile;
	const char *home;
	const char *work;
	const char *picture;
} Contact_Info;

Evas_Object *contacts_add(Evas_Object *parent);

#endif
