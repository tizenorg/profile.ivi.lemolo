#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <Eet.h>
#include <Eina.h>
#include <ui-gadget.h>
#include <contacts-ug.h>
#include <contacts.h>

#include "log.h"
#include "ofono.h"
#include "contacts.h"
#include "util.h"

typedef struct _Contacts {
	Evas_Object *self;
	Eina_Bool contacts_on;
	Ecore_Timer *reconnect;
	struct ui_gadget *ug_all;
	Eina_Hash *numbers, *hash_ids;
	/*
	 * hash_ids is a pair (tizen_db_id, Contact_Info)
	 * So we won't create a duplicated Contact_Info when dealing
	 * with alias numbers
	 */
} Contacts;

typedef struct  _Contact_Number {
	EINA_INLIST;
	unsigned int numberlen;
	const char *type;
	char number[];
} Contact_Number;

typedef struct _Contact_Number_Entry {
	Eina_List *contacts;
	char number[];
} Contact_Number_Entry;

struct _Contact_Info {
	int id;
	const char *first_name;
	const char *last_name;
	const char *full_name;
	const char *picture;
	Eina_Inlist *numbers;
	Eina_Inlist *alias;
	Eina_Inlist *on_del_cbs;
	Contacts *contacts;
	struct {
		Eina_Inlist *listeners;
		Eina_List *deleted;
		int walking;
	} on_changed_cbs;
};

typedef struct _Contact_Info_On_Del_Ctx {
	EINA_INLIST;
	void (*cb)(void *, const Contact_Info *);
	const void *data;
} Contact_Info_On_Del_Ctx;

typedef struct _Contact_Info_On_Changed_Ctx {
	EINA_INLIST;
	void (*cb)(void *, Contact_Info *);
	const void *data;
	Eina_Bool deleted;
} Contact_Info_On_Changed_Ctx;

static const char *phone_type_get(contact_number_h number);
static void _contact_info_free(Contact_Info *c_info);

static void _contact_number_add(char *number,
				Contact_Info *c_info,
				contact_number_h number_h);

static void _contact_number_entry_add(const char *number,
					Contact_Info *c_info)
{
	Contact_Number_Entry *e = eina_hash_find(c_info->contacts->numbers,
							number);
	if (!e) {
		size_t numberlen = strlen(number);
		e = calloc(1, sizeof(Contact_Number_Entry) + numberlen + 1);
		EINA_SAFETY_ON_NULL_RETURN(e);
		memcpy(e->number, number, numberlen + 1);
		eina_hash_direct_add(c_info->contacts->numbers, e->number, e);
	}
	e->contacts = eina_list_append(e->contacts, c_info);
}

static void _contact_number_entry_del(const char *number,
					Contact_Info *c_info)
{
	Eina_Hash *numbers = c_info->contacts->numbers;
	Contact_Number_Entry *e = eina_hash_find(numbers, number);
	EINA_SAFETY_ON_NULL_RETURN(e);
	e->contacts = eina_list_remove(e->contacts, c_info);
	if (e->contacts)
		return;
	eina_hash_del_by_key(numbers, e->number);
	/* hash's free callback will free "e" for me */
}

static Eina_Bool _is_alias(Contact_Number *cn, const char *alias)
{
	unsigned int aliaslen = strlen(alias);
	unsigned int suffix = cn->numberlen - aliaslen;
	if (cn->numberlen < aliaslen)
		return EINA_FALSE;

	return memcmp(cn->number + suffix, alias, aliaslen) == 0;
}

static Eina_Bool _contact_number_is_equal(Contact_Number *cn,
						const char *number)
{
	unsigned int numberlen = strlen(number);
	if (cn->numberlen == numberlen &&
		memcmp(cn->number, number, numberlen) == 0)
		return EINA_TRUE;

	return EINA_FALSE;
}

static void _alias_delete(Contact_Number *cn, Contact_Info *c_info)
{
	Contact_Number *alias;
	Eina_List *deleted_list = NULL;

	EINA_INLIST_FOREACH(c_info->alias, alias) {
		if (_is_alias(cn, alias->number))
			deleted_list = eina_list_append(deleted_list, alias);
	}

	EINA_LIST_FREE(deleted_list, alias) {
		c_info->alias = eina_inlist_remove(c_info->alias,
							EINA_INLIST_GET(alias));
		_contact_number_entry_del(alias->number, c_info);
		free(alias);
	}
}

static Eina_Bool _contact_phone_changed(Contact_Info *c_info,
						contact_h contact)
{
	Contact_Number *cn;
	Eina_Bool ret = EINA_FALSE;
	Eina_List *deleted_list = NULL;

	/* Looking for deleted phones */
	EINA_INLIST_FOREACH(c_info->numbers, cn) {
		Eina_Bool deleted = EINA_TRUE;
		contact_number_iterator_h it;
		contact_number_h number_h;
		char *number;
		if (contact_get_number_iterator(contact, &it) !=
			CONTACTS_ERROR_NONE)
			continue;
		while (contact_number_iterator_has_next(it)) {
			if (contact_number_iterator_next(&it, &number_h) !=
				CONTACTS_ERROR_NONE)
				continue;
			if (contact_number_get_number(number_h, &number) !=
				CONTACTS_ERROR_NONE)
				continue;
			Eina_Bool equal = _contact_number_is_equal(cn, number);
			free(number);
			if (equal) {
				deleted = EINA_FALSE;
				break;
			}
		}
		if (deleted) {
			ret = EINA_TRUE;
			_contact_number_entry_del(cn->number, c_info);
			deleted_list = eina_list_append(deleted_list, cn);
		}
	}

	contact_number_iterator_h it;
	if (contact_get_number_iterator(contact, &it) != CONTACTS_ERROR_NONE)
		return ret;

	/* Looking for new phones */
	while (contact_number_iterator_has_next(it)) {
		Eina_Bool added = EINA_TRUE;
		contact_number_h number_h;
		char *number;
		if (contact_number_iterator_next(&it, &number_h) !=
			CONTACTS_ERROR_NONE)
			continue;
		if (contact_number_get_number(number_h, &number) !=
			CONTACTS_ERROR_NONE)
			continue;
		EINA_INLIST_FOREACH(c_info->numbers, cn) {
			if (_contact_number_is_equal(cn, number)) {
				added = EINA_FALSE;
				break;
			}
		}
		if (added)
			_contact_number_add(number, c_info, number_h);
		free(number);
	}

	EINA_LIST_FREE(deleted_list, cn) {
		c_info->numbers = eina_inlist_remove(c_info->numbers,
							EINA_INLIST_GET(cn));
		_alias_delete(cn, c_info);
		free(cn);
	}

	return ret;
}

static void _contact_info_on_del_dispatch(Contact_Info *c)
{
	Eina_Inlist *lst = c->on_del_cbs;
	c->on_del_cbs = NULL; /* avoid contact_info_on_del_callback_del() */
	while (lst) {
		Contact_Info_On_Del_Ctx *ctx = EINA_INLIST_CONTAINER_GET
			(lst, Contact_Info_On_Del_Ctx);

		lst = eina_inlist_remove(lst, lst);

		ctx->cb((void *)ctx->data, c);
		free(ctx);
	}
}

static void _contact_info_on_changed_dispatch(Contact_Info *c)
{
	Contact_Info_On_Changed_Ctx *ctx;

	c->on_changed_cbs.walking++;
	EINA_INLIST_FOREACH(c->on_changed_cbs.listeners, ctx) {
		if (ctx->deleted)
			continue;
		ctx->cb((void *)ctx->data, c);
	}
	c->on_changed_cbs.walking--;

	if (c->on_changed_cbs.walking <= 0) {
		EINA_LIST_FREE(c->on_changed_cbs.deleted, ctx) {
			c->on_changed_cbs.listeners = eina_inlist_remove(
				c->on_changed_cbs.listeners,
				EINA_INLIST_GET(ctx));
			free(ctx);
		}
	}
}

static Eina_Bool _hash_foreach(const Eina_Hash *hash __UNUSED__,
				const void *key __UNUSED__, void *data,
				void *fdata)
{
	Eina_List **deleted = fdata;
	Eina_Bool disp = EINA_FALSE;
	Contact_Info *c_info = data;
	contact_h contact = NULL;
	contact_name_h name_h = NULL;
	char *f_name = NULL, *l_name = NULL, *img = NULL;

	contact_get_from_db(c_info->id, &contact);
	/* Contact no longer exists. */
	if (!contact)
		goto deleted;
	contact_get_name(contact, &name_h);
	EINA_SAFETY_ON_NULL_GOTO(name_h, err_contact);

	contact_name_get_detail(name_h, CONTACT_NAME_DETAIL_FIRST, &f_name);
	contact_name_get_detail(name_h, CONTACT_NAME_DETAIL_LAST, &l_name);
	contact_get_image(contact, &img);

	if (eina_stringshare_replace(&c_info->first_name, f_name)) {
		disp = EINA_TRUE;
		eina_stringshare_del(c_info->full_name);
		c_info->full_name = NULL;
	}

	if (eina_stringshare_replace(&c_info->last_name, l_name)) {
		disp = EINA_TRUE;
		eina_stringshare_del(c_info->full_name);
		c_info->full_name = NULL;
	}

	disp |= eina_stringshare_replace(&c_info->picture, img);

	disp |= _contact_phone_changed(c_info, contact);

	if (disp)
		_contact_info_on_changed_dispatch(c_info);

	free(img);
	free(l_name);
	free(f_name);
err_contact:
	contact_destroy(contact);
	return EINA_TRUE;

deleted:
	*deleted = eina_list_append(*deleted, c_info);
	return EINA_TRUE;
}

static void _contact_db_changed(void *data)
{
	Contacts *contacts = data;
	Contact_Info *c_info;
	Eina_List *deleted = NULL;

	EINA_SAFETY_ON_NULL_RETURN(contacts);
	eina_hash_foreach(contacts->hash_ids, _hash_foreach, &deleted);

	EINA_LIST_FREE(deleted, c_info) {
		Contact_Number *cn;
		/* _contact_info_free() will free the lists for me */
		EINA_INLIST_FOREACH(c_info->alias, cn)
			_contact_number_entry_del(cn->number, c_info);
		EINA_INLIST_FOREACH(c_info->numbers, cn)
			_contact_number_entry_del(cn->number, c_info);
		eina_hash_del_by_key(contacts->hash_ids, &c_info->id);
	}
}

static void _contact_number_add(char *number,
				Contact_Info *c_info,
				contact_number_h number_h)
{
	unsigned int numberlen = strlen(number);
	Contact_Number *cn = malloc(sizeof(Contact_Number) + numberlen + 1);
	EINA_SAFETY_ON_NULL_RETURN(cn);
	memcpy(cn->number, number, numberlen);
	cn->numberlen = numberlen;
	cn->number[numberlen] = '\0';
	cn->type = phone_type_get(number_h);
	c_info->numbers = eina_inlist_append(c_info->numbers,
						EINA_INLIST_GET(cn));
}

bool _search_cb(contact_query_number_s *query, void *data)
{
	Contact_Info **c_info = data;
	char *n;
	contact_h tizen_c;
	contact_number_iterator_h it;
	contact_number_h number_h;

	*c_info = calloc(1, sizeof(Contact_Info));
	EINA_SAFETY_ON_NULL_RETURN_VAL((*c_info), false);
	(*c_info)->first_name = eina_stringshare_add(query->first_name);
	(*c_info)->last_name = eina_stringshare_add(query->last_name);
	(*c_info)->picture = eina_stringshare_add(query->contact_image_path);
	(*c_info)->id = query->contact_db_id;
	if (contact_get_from_db((*c_info)->id, &tizen_c) != CONTACTS_ERROR_NONE)
		return false;

	if (contact_get_number_iterator(tizen_c, &it) !=
		CONTACTS_ERROR_NONE)
		return false;

	while (contact_number_iterator_has_next(it)) {
		if (contact_number_iterator_next(&it, &number_h) !=
			CONTACTS_ERROR_NONE)
			continue;
		if (contact_number_get_number(number_h, &n) !=
			CONTACTS_ERROR_NONE)
			continue;
		_contact_number_add(n, (*c_info), number_h);
		free(n);
	}
	contact_destroy(tizen_c);
	return false;
}

static const char *phone_type_get(contact_number_h number)
{
	contact_number_type_e type_e;

	if (contact_number_get_type(number, &type_e) < 0)
		return NULL;

	switch (type_e) {
	case CONTACT_NUMBER_TYPE_NONE:
		return "None";
	case CONTACT_NUMBER_TYPE_HOME:
		return "Home";
	case CONTACT_NUMBER_TYPE_WORK:
		return "Work";
	case CONTACT_NUMBER_TYPE_VOICE:
		return "Home";
	case CONTACT_NUMBER_TYPE_FAX:
		return "Fax";
	case CONTACT_NUMBER_TYPE_MSG:
		return "Message";
	case CONTACT_NUMBER_TYPE_CELL:
		return "Mobile";
	case CONTACT_NUMBER_TYPE_PAGER:
		return "Pager";
	case CONTACT_NUMBER_TYPE_BBS:
		return "Bulletin board";
	case CONTACT_NUMBER_TYPE_MODEM:
		return "Modem";
	case CONTACT_NUMBER_TYPE_CAR:
		return "Car phone";
	case CONTACT_NUMBER_TYPE_ISDN:
		return "ISDN";
	case CONTACT_NUMBER_TYPE_VIDEO:
		return "Video conference";
	case CONTACT_NUMBER_TYPE_PCS:
		return "Personal communicatior";
	case CONTACT_NUMBER_TYPE_ASSISTANT:
		return "Assistant telephone";
	case CONTACT_NUMBER_TYPE_CUSTOM:
		return "Custom";
	default:
		return "Unknown";
	}
}

static const char *_alias_phone_type_match(Contact_Info *c_info,
						const char *alias)
{
	Contact_Number *cn;
	EINA_INLIST_FOREACH(c_info->numbers, cn) {
		if (_is_alias(cn, alias))
			return cn->type;
	}
	return "Unknown";
}

static Eina_Bool _alias_create(Contact_Info *c_info, const char *number)
{
	unsigned int numberlen = strlen(number);
	Contact_Number *cn = malloc(sizeof(Contact_Number) + numberlen + 1);
	EINA_SAFETY_ON_NULL_RETURN_VAL(cn, EINA_FALSE);
	memcpy(cn->number, number, numberlen);
	cn->numberlen = numberlen;
	cn->number[numberlen] = '\0';
	cn->type = _alias_phone_type_match(c_info, number);
	c_info->alias = eina_inlist_append(
		c_info->alias, EINA_INLIST_GET(cn));
	_contact_number_entry_add(cn->number, c_info);
	return EINA_TRUE;
}

Contact_Info *contact_search(Evas_Object *obj, const char *number,
				const char **type)
{
	Contact_Info *c_info = NULL, *found;
	Contacts *contacts;
	Contact_Number_Entry *entry;

	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);
	EINA_SAFETY_ON_NULL_RETURN_VAL(number, NULL);
	contacts = evas_object_data_get(obj, "contacts.ctx");
	EINA_SAFETY_ON_NULL_RETURN_VAL(contacts, NULL);

	if (!contacts->contacts_on)
		return NULL;

	entry = eina_hash_find(contacts->numbers, number);

	if (entry) {
		c_info = eina_list_data_get(entry->contacts);
		EINA_SAFETY_ON_NULL_RETURN_VAL(c_info, NULL);
		goto get_type;
	}

	if (contact_query_contact_by_number(_search_cb, number, &c_info) < 0) {
		ERR("Could not fetch phone number: %s from DB", number);
		return NULL;
	}

	if (!c_info)
		return NULL;

	/* Do we have this contact already ? */
	found = eina_hash_find(contacts->hash_ids, &c_info->id);

	if (found) {
		_contact_info_free(c_info);
		c_info = found;
		/* The user enterer an alias */
		Eina_Bool r;
		r = _alias_create(c_info, number);
		EINA_SAFETY_ON_FALSE_RETURN_VAL(r, NULL);
	} else {
		/* New contact */
		c_info->contacts = contacts;
		eina_hash_add(contacts->hash_ids, &c_info->id, c_info);
		Contact_Number *cn;
		EINA_INLIST_FOREACH(c_info->numbers, cn)
			_contact_number_entry_add(cn->number, c_info);
	}

get_type:
	if (type)
		*type = contact_info_number_check(c_info, number);
	return c_info;
}

const char *contact_info_picture_get(const Contact_Info *c)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	return c->picture;
}

const char *contact_info_full_name_get(const Contact_Info *c)
{
	Contact_Info *c2;
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	if (!c->full_name) {
		c2 = (Contact_Info *)c;
		c2->full_name = eina_stringshare_printf("%s %s", c->first_name,
							c->last_name);
	}
	return c->full_name;
}

const char *contact_info_first_name_get(const Contact_Info *c)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	return c->first_name;
}

const char *contact_info_last_name_get(const Contact_Info *c)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	return c->last_name;
}

const char *contact_info_detail_get(const Contact_Info *c,
					const char *type)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	EINA_SAFETY_ON_NULL_RETURN_VAL(type, NULL);
	Contact_Number *cn;
	/* Do not check in the alias list. Because here we want in fact a
	 * "working" numbers
	 */
	EINA_INLIST_FOREACH(c->numbers, cn) {
		if (strcmp(cn->type, type) == 0)
			return cn->number;
	}
	return NULL;
}

const char *contact_info_number_check(const Contact_Info *c,
					const char *number)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	EINA_SAFETY_ON_NULL_RETURN_VAL(number, NULL);
	Contact_Number *cn;

	EINA_INLIST_FOREACH(c->numbers, cn) {
		if (_contact_number_is_equal(cn, number))
			return cn->type;
	}
	/* It could be an alias */
	EINA_INLIST_FOREACH(c->alias, cn) {
		if (_contact_number_is_equal(cn, number))
			return cn->type;
	}
	return "Unknown";
}

Eina_Bool contact_info_picture_set(Contact_Info *c __UNUSED__,
					const char *filename __UNUSED__)
{
	ERR("TODO");
	return EINA_FALSE;
}

Eina_Bool contact_info_first_name_set(Contact_Info *c __UNUSED__,
					const char *name __UNUSED__)
{
	ERR("TODO");
	return EINA_FALSE;
}

Eina_Bool contact_info_last_name_set(Contact_Info *c __UNUSED__,
					const char *name __UNUSED__)
{
	ERR("TODO");
	return EINA_FALSE;
}

Eina_Bool contact_info_detail_set(Contact_Info *c __UNUSED__,
					const char *type __UNUSED__,
					const char *number __UNUSED__)
{
	ERR("TODO");
	return EINA_FALSE;
}

void contact_info_on_changed_callback_add(Contact_Info *c,
						void (*cb)(void *data, Contact_Info *c),
						const void *data )
{
	Contact_Info_On_Changed_Ctx *ctx;

	EINA_SAFETY_ON_NULL_RETURN(c);
	EINA_SAFETY_ON_NULL_RETURN(cb);

	ctx = calloc(1, sizeof(Contact_Info_On_Changed_Ctx));
	EINA_SAFETY_ON_NULL_RETURN(ctx);
	ctx->cb = cb;
	ctx->data = data;

	c->on_changed_cbs.listeners = eina_inlist_append(
		c->on_changed_cbs.listeners, EINA_INLIST_GET(ctx));
}

void contact_info_on_changed_callback_del(Contact_Info *c,
						void (*cb)(void *data, Contact_Info *c),
						const void *data)
{

	Contact_Info_On_Changed_Ctx *ctx, *found = NULL;
	EINA_SAFETY_ON_NULL_RETURN(c);
	EINA_SAFETY_ON_NULL_RETURN(cb);

	EINA_INLIST_FOREACH(c->on_changed_cbs.listeners, ctx) {
		if (ctx->cb == cb && ctx->data == data) {
			found = ctx;
			break;
		}
	}

	if (!found)
		return;

	if (c->on_changed_cbs.walking > 0) {
		found->deleted = EINA_TRUE;
		c->on_changed_cbs.deleted = eina_list_append(
			c->on_changed_cbs.deleted, found);
		return;
	}

	c->on_changed_cbs.listeners = eina_inlist_remove(
		c->on_changed_cbs.listeners, EINA_INLIST_GET(found));
	free(found);
}

void contact_info_on_del_callback_add(Contact_Info *c,
					void (*cb)(void *data, const Contact_Info *c),
					const void *data)
{

	Contact_Info_On_Del_Ctx *ctx;

	EINA_SAFETY_ON_NULL_RETURN(c);
	EINA_SAFETY_ON_NULL_RETURN(cb);

	ctx = calloc(1, sizeof(Contact_Info_On_Del_Ctx));
	EINA_SAFETY_ON_NULL_RETURN(ctx);
	ctx->cb = cb;
	ctx->data = data;

	c->on_del_cbs = eina_inlist_append(c->on_del_cbs, EINA_INLIST_GET(ctx));
}

void contact_info_on_del_callback_del(Contact_Info *c,
					void (*cb)(void *data, const Contact_Info *c),
					const void *data )
{

	Contact_Info_On_Del_Ctx *ctx, *found = NULL;
	EINA_SAFETY_ON_NULL_RETURN(c);
	EINA_SAFETY_ON_NULL_RETURN(cb);

	EINA_INLIST_FOREACH(c->on_del_cbs, ctx) {
		if (ctx->cb == cb && ctx->data == data) {
			found = ctx;
			break;
		}
	}

	if (!found)
		return;

	c->on_del_cbs = eina_inlist_remove(c->on_del_cbs,
						EINA_INLIST_GET(found));

	free(found);
}

void contact_info_del(Contact_Info *c_info __UNUSED__)
{
	ERR("TODO");
}

static void _contacts_ug_layout_create(struct ui_gadget *ug,
					enum ug_mode mode __UNUSED__,
					void *priv)
{
	Contacts *contacts = priv;
	EINA_SAFETY_ON_NULL_RETURN(ug);
	elm_object_part_content_set(contacts->self, "elm.swallow.genlist",
					ug_get_layout(ug));
}

static void _contact_info_free(Contact_Info *c_info)
{
	Contact_Number *cn;
	_contact_info_on_del_dispatch(c_info);
	EINA_SAFETY_ON_FALSE_RETURN(c_info->on_changed_cbs.walking == 0);

	if (c_info->on_changed_cbs.deleted) {
		ERR("contact still have changed deleted listeners: %p %s %s",
			c_info, c_info->first_name, c_info->last_name);
		eina_list_free(c_info->on_changed_cbs.deleted);
	}

	if (c_info->on_changed_cbs.listeners) {
		while (c_info->on_changed_cbs.listeners) {
			Contact_Info_On_Changed_Ctx *ctx;

			ctx = EINA_INLIST_CONTAINER_GET(
				c_info->on_changed_cbs.listeners,
				Contact_Info_On_Changed_Ctx);
			c_info->on_changed_cbs.listeners = eina_inlist_remove
				(c_info->on_changed_cbs.listeners,
					c_info->on_changed_cbs.listeners);
			free(ctx);
		}
	}

	while (c_info->numbers) {
		cn = EINA_INLIST_CONTAINER_GET(c_info->numbers,
						Contact_Number);
		c_info->numbers = eina_inlist_remove(c_info->numbers,
							c_info->numbers);
		free(cn);
	}

	while (c_info->alias) {
		cn = EINA_INLIST_CONTAINER_GET(c_info->alias,
						Contact_Number);
		c_info->alias = eina_inlist_remove(c_info->alias,
							c_info->alias);
		free(cn);
	}

	eina_stringshare_del(c_info->first_name);
	eina_stringshare_del(c_info->last_name);
	eina_stringshare_del(c_info->full_name);
	eina_stringshare_del(c_info->picture);
	free(c_info);
}

void _hash_elements_free(void *data)
{
	Contact_Info *c = data;
	_contact_info_free(c);
}

void _numbers_hash_elements_free(void *data)
{
	Contact_Number_Entry *e = data;
	/*Contact_Info will be deleted by hash_ids */
	eina_list_free(e->contacts);
	free(e);
}

static void _on_del(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Contacts *contacts = data;
	eina_hash_free(contacts->hash_ids);
	eina_hash_free(contacts->numbers);
	ug_destroy(contacts->ug_all);
	if (contacts->reconnect)
		ecore_timer_del(contacts->reconnect);
	free(contacts);
	contacts_disconnect();
}

static void _create_contacts_ug(Contacts *contacts)
{
	char buf[PATH_MAX];
	struct ug_cbs cbs;
	struct ui_gadget *ug;
	bundle *bd;

	cbs.priv = contacts;
	cbs.layout_cb = _contacts_ug_layout_create;
	cbs.result_cb = NULL;
	cbs.destroy_cb = NULL;

	bd = bundle_create();
	EINA_SAFETY_ON_NULL_RETURN(bd);

	snprintf(buf, sizeof(buf), "%d", CT_UG_REQUEST_DETAIL);
	bundle_add(bd, CT_UG_BUNDLE_TYPE, buf);
	snprintf(buf, sizeof(buf), "%d", 1);
	bundle_add(bd, CT_UG_BUNDLE_ID, buf);

	ug = ug_create(NULL, UG_CONTACTS_LIST, UG_MODE_FRAMEVIEW, bd, &cbs);
	EINA_SAFETY_ON_NULL_GOTO(ug, err_ug);

	contacts->ug_all = ug;
	evas_object_data_set(contacts->self, "contacts.ctx", contacts);
err_ug:
	bundle_free(bd);
	bd = NULL;
}

static Eina_Bool _contacts_reconnect(void *data)
{
	Contacts *contacts = data;

	if (contacts_connect() != CONTACTS_ERROR_NONE)
		return ECORE_CALLBACK_RENEW;

	contacts->contacts_on = EINA_TRUE;
	contacts->reconnect = NULL;
	contacts_add_contact_db_changed_cb(_contact_db_changed, contacts);
	_create_contacts_ug(contacts);
	return ECORE_CALLBACK_DONE;
}

Evas_Object *contacts_add(Evas_Object *parent)
{
	Contacts *contacts;

	contacts = calloc(1, sizeof(Contacts));
	EINA_SAFETY_ON_NULL_RETURN_VAL(contacts, NULL);
	contacts->self = layout_add(parent, "contacts_bg");
	EINA_SAFETY_ON_NULL_GOTO(contacts->self, err_layout);
	evas_object_event_callback_add(contacts->self, EVAS_CALLBACK_DEL,
					_on_del, contacts);

	if (contacts_connect() != CONTACTS_ERROR_NONE) {
		WRN("Could not connect to the contacts DB");
		contacts->contacts_on = EINA_FALSE;
		contacts->reconnect = ecore_timer_add(1.0, _contacts_reconnect,
							contacts);
	} else {
		contacts_add_contact_db_changed_cb(_contact_db_changed,
							contacts);
		contacts->contacts_on = EINA_TRUE;
		_create_contacts_ug(contacts);
	}

	contacts->numbers = eina_hash_string_superfast_new(
		_numbers_hash_elements_free);
	EINA_SAFETY_ON_NULL_GOTO(contacts->numbers, err_hash);

	contacts->hash_ids = eina_hash_int32_new(_hash_elements_free);
	EINA_SAFETY_ON_NULL_GOTO(contacts->hash_ids, err_hash_id);

	return contacts->self;

err_hash_id:
	eina_hash_free(contacts->numbers);
err_hash:
	evas_object_del(contacts->self);
	if (contacts->reconnect)
		ecore_timer_del(contacts->reconnect);
err_layout:
	free(contacts);
	return NULL;
}
