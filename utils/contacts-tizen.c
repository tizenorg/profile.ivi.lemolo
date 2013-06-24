#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <Eet.h>
#include <Eina.h>
#ifdef HAVE_UI_GADGET
#include <ui-gadget.h>
#include <contacts-ug.h>
#endif
#include <contacts.h>
#include <aul.h>

#include "log.h"
#include "ofono.h"
#include "contacts-ofono-efl.h"
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

typedef struct _Contact_Name_Search {
	const char *name;
	Contact_Info *c_info;
} Contact_Name_Search;

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

struct _Contact_Partial_Match
{
	const Contact_Info *info;
	const char *type;
	Eina_Bool name_match : 1;
};

typedef struct _Partial_Match_Search
{
	Eina_List *matches;
	const Contacts *contacts;
} Partial_Match_Search;


static void _contact_number_entry_add(const char *number, Contact_Info *c_info);

static const char *_contact_number_type_get(contacts_record_h contact_number_h);
static void _contact_info_free(Contact_Info *c_info);

static void _contact_number_add(char *number,
				Contact_Info *c_info,
				contacts_record_h contact_number_h);

const char *contact_info_number_check(const Contact_Info *c,
					const char *number);

static void _partial_match_add(Eina_List **p_list, const char *type,
				const Contact_Info *c_info,
				Eina_Bool name_match)
{
	Contact_Partial_Match *pm = malloc(sizeof(Contact_Partial_Match));
	EINA_SAFETY_ON_NULL_RETURN(pm);
	pm->info = c_info;
	pm->type = type;
	pm->name_match = name_match;
	*p_list = eina_list_append(*p_list, pm);
}

static void _partial_number_match_add(Eina_List **p_list, const char *type,
					const Contact_Info *c_info)
{
	_partial_match_add(p_list, type, c_info, EINA_FALSE);
}

static int _db_query_partial_search(const char *name_or_number,
									Partial_Match_Search *pm_search,
									Eina_Bool name_match)
{
	const Contacts *contacts = pm_search->contacts;
	Contact_Info *c_info;
	contacts_error_e err = CONTACTS_ERROR_NONE;
	contacts_list_h list = NULL;
	contacts_query_h query = NULL;
	contacts_filter_h filter = NULL;
	unsigned int contact_count = 0;
	contacts_record_h contact_h = NULL;
	contacts_record_h contact_name_h = NULL;
	contacts_record_h contact_number_h = NULL;
	int contact_id;
	char *f_name = NULL, *l_name = NULL, *img = NULL;
	const char *type;
	int idx = 0;

	if (name_match) {
		err = contacts_query_create(_contacts_number._uri, &query);

		do {
			if (CONTACTS_ERROR_NONE != (err = contacts_filter_create(_contacts_name._uri, &filter)))
				break;
			if (CONTACTS_ERROR_NONE != (err = contacts_filter_add_str(filter, _contacts_name.first, CONTACTS_MATCH_EXACTLY, name_or_number)))
				break;
			if (CONTACTS_ERROR_NONE != (err = contacts_filter_add_operator( filter, CONTACTS_FILTER_OPERATOR_OR)))
				break;
			if (CONTACTS_ERROR_NONE != (err = contacts_filter_add_str(filter, _contacts_name.last, CONTACTS_MATCH_EXACTLY, name_or_number)))
				break;
			if (CONTACTS_ERROR_NONE != (err = contacts_query_set_filter(query, filter)))
				break;
			if (CONTACTS_ERROR_NONE != (err = contacts_db_get_records_with_query(query, 0, 0, &list)))
				break;
		} while (0);
	}
	else {
		err = contacts_query_create(_contacts_number._uri, &query);

		do {
			if (CONTACTS_ERROR_NONE != (err = contacts_filter_create(_contacts_number._uri, &filter)))
				break;
			if (CONTACTS_ERROR_NONE != (err = contacts_filter_add_str(filter, _contacts_number.number, CONTACTS_MATCH_EXACTLY, name_or_number)))
				break;
			if (CONTACTS_ERROR_NONE != (err = contacts_query_set_filter(query, filter)))
				break;
			if (CONTACTS_ERROR_NONE != (err = contacts_db_get_records_with_query(query, 0, 0, &list)))
				break;
		} while (0);
	}

	if (CONTACTS_ERROR_NONE != err) {
		ERR("contacts_query_create() Failed(%d)", err);
		return -1;
	}
	contacts_filter_destroy(filter);
	contacts_query_destroy(query);

	contacts_list_get_count(list, &contact_count);
	if (contact_count == 0)
		return 0;

	while (CONTACTS_ERROR_NONE == err) {
		c_info = calloc(1, sizeof(Contact_Info));
		EINA_SAFETY_ON_NULL_RETURN_VAL(c_info, -1);
		err = contacts_list_get_current_record_p(list, &contact_number_h);
		if (CONTACTS_ERROR_NONE != err) {
			ERR("contacts_list_get_current_record_p() Failed(%d)", err);
			return -1;
		}

		contacts_record_get_int(contact_number_h, _contacts_number.contact_id, &contact_id);
		err = contacts_db_get_record(_contacts_contact._uri, contact_id, &contact_h);
		if (CONTACTS_ERROR_NONE != err) {
			ERR("contacts_db_get_record() Failed(%d)", err);
			return -1;
		}

		err = contacts_record_get_child_record_at_p(contact_h, _contacts_contact.name, 0, &contact_name_h);
		if (CONTACTS_ERROR_NONE != err) {
			ERR("contacts_record_get_child_record_at_p() Failed(%d)", err);
			return -1;
		}

		contacts_record_get_str(contact_h, _contacts_contact.image_thumbnail_path, &img);
		contacts_record_get_str(contact_name_h, _contacts_name.first, &f_name);
		contacts_record_get_str(contact_name_h, _contacts_name.last, &l_name);

        c_info->id = contact_id;
        c_info->picture = eina_stringshare_add(img);
        c_info->first_name = eina_stringshare_add(f_name);
        c_info->last_name = eina_stringshare_add(l_name);

        idx = 0;
        while (CONTACTS_ERROR_NONE == contacts_record_get_child_record_at_p(contact_h,
																	_contacts_contact.number,
																	idx++,
																	&contact_number_h)) {
			char *number;
			contacts_record_get_str(contact_number_h, _contacts_number.number, &number);
			_contact_number_add(number, c_info, contact_number_h);

			Contact_Info *c_info_found;
			c_info_found = eina_hash_find(contacts->hash_ids, &c_info->id);
			if (c_info_found) {
				/* Destroy and use old contact info */
				_contact_info_free(c_info);
				c_info = c_info_found;
			}
			else {
				/* New contact info */
				eina_hash_add(contacts->hash_ids, &c_info->id, c_info);
			}

			type = contact_info_number_check(c_info, number);
			if (name_match) {
				_partial_match_add(&pm_search->matches, type, c_info, EINA_TRUE);
			}
			else {
				_partial_number_match_add(&pm_search->matches, type, c_info);
			}
			free(number);
		}

		contacts_record_destroy(contact_h, true);
		free(img);
		free(l_name);
		free(f_name);

        err = contacts_list_next(list);
	}

	if (CONTACTS_ERROR_NONE != err) {
		ERR("contacts_list_next() Failed(%d)", err);
		return -1;
	}

	return 1;
}

Eina_List *contact_partial_match_search(Evas_Object *obj, const char *query)
{

	const Contacts *contacts;
	int i, j;
	Eina_Bool name_search = EINA_FALSE;
	char *query_number;
	Partial_Match_Search pm_search;

	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);
	EINA_SAFETY_ON_NULL_RETURN_VAL(query, NULL);
	contacts = evas_object_data_get(obj, "contacts.ctx");
	EINA_SAFETY_ON_NULL_RETURN_VAL(contacts, NULL);

	if (!contacts->contacts_on)
		return NULL;

	/* Check if it is numeric */
	query_number = alloca(strlen(query) + 1);
	EINA_SAFETY_ON_NULL_RETURN_VAL(query_number, NULL);
	for (i = 0, j = 0; query[i] != '\0'; i++) {
		if (isalpha(query[i])) {
			name_search = EINA_TRUE;
			break;
		} else if (isdigit(query[i]))
			query_number[j++] = query[i];
	}

	pm_search.contacts = contacts;
	pm_search.matches = NULL;

	if (name_search) {
		if (_db_query_partial_search(query, &pm_search, name_search) < 0) {
			ERR("Could not search in contacts DB the name: %s",
				query);
			return NULL;
		}
	} else {
		query_number[j] = '\0';
		if (_db_query_partial_search(query, &pm_search, name_search) < 0) {
			ERR("Could not search in contacts DB the number: %s",
				query);
			return NULL;
		}
	}

	return pm_search.matches;
}

void contact_partial_match_search_free(Eina_List *results)
{
	Contact_Partial_Match *pm;
	EINA_LIST_FREE(results, pm)
		free(pm);
}

const char *contact_partial_match_type_get(const Contact_Partial_Match *pm)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(pm, NULL);
	return pm->type;
}

const Contact_Info *contact_partial_match_info_get(const Contact_Partial_Match *pm)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(pm, NULL);
	return pm->info;
}

Eina_Bool contact_partial_match_name_match_get(const Contact_Partial_Match *pm)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(pm, EINA_FALSE);
	return pm->name_match;
}

Eina_List *contact_info_all_numbers_get(const Contact_Info *c)
{
	Eina_List *l = NULL;
	Contact_Number *cn;

	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);

	EINA_INLIST_FOREACH(c->numbers, cn)
		l = eina_list_append(l, cn->number);

	return l;
}

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
						contacts_record_h contact_h)
{
	Contact_Number *cn;
	Eina_Bool ret = EINA_FALSE;
	Eina_List *deleted_list = NULL;
	int idx = 0;

	/* Looking for deleted phones */
	EINA_INLIST_FOREACH(c_info->numbers, cn) {
		Eina_Bool deleted = EINA_TRUE;
		idx = 0;
		contacts_record_h contact_number_h;
		while (CONTACTS_ERROR_NONE == contacts_record_get_child_record_at_p(contact_h,
																	_contacts_contact.number,
																	idx++,
																	&contact_number_h)) {
			char *number;
			if (contacts_record_get_str_p(contact_number_h, _contacts_number.number, &number) !=
				CONTACTS_ERROR_NONE)
				continue;
			Eina_Bool equal = _contact_number_is_equal(cn, number);
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

	idx = 0;
	contacts_record_h contact_number_h;
	if (contacts_record_get_child_record_at_p(contact_h, _contacts_contact.number, 0, &contact_number_h) !=
		CONTACTS_ERROR_NONE)
		return ret;

	/* Looking for new phones */
	while (CONTACTS_ERROR_NONE == contacts_record_get_child_record_at_p(contact_h,
																_contacts_contact.number,
																idx++,
																&contact_number_h)) {
		Eina_Bool added = EINA_TRUE;
		char *number;
		if (contacts_record_get_str(contact_number_h, _contacts_number.number, &number) !=
			CONTACTS_ERROR_NONE)
			continue;
		EINA_INLIST_FOREACH(c_info->numbers, cn) {
			if (_contact_number_is_equal(cn, number)) {
				added = EINA_FALSE;
				break;
			}
		}
		if (added)
			_contact_number_add(number, c_info, contact_number_h);
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
	contacts_error_e err = CONTACTS_ERROR_NONE;
	Eina_List **deleted = fdata;
	Eina_Bool disp = EINA_FALSE;
	Contact_Info *c_info = data;
	contacts_record_h contact_h = NULL;
	contacts_record_h contact_name_h = NULL;
	char *f_name = NULL, *l_name = NULL, *img = NULL;

	err = contacts_db_get_record( _contacts_contact._uri, c_info->id, &contact_h);
	if (CONTACTS_ERROR_NONE != err) {
		ERR("contacts_db_get_record() Failed(%d)", err);
		return false;
	}

	/* Contact no longer exists. */
	if (!contact_h)
		goto deleted;
	err = contacts_record_get_child_record_at_p(contact_h, _contacts_contact.name, 0, &contact_name_h);
	if (CONTACTS_ERROR_NONE != err) {
		ERR("contacts_record_get_child_record_at_p(%d)", err);
	}

	EINA_SAFETY_ON_NULL_GOTO(contact_name_h, err_contact);

	contacts_record_get_str(contact_name_h, _contacts_name.first, &f_name);
	contacts_record_get_str(contact_name_h, _contacts_name.last, &l_name);
	contacts_record_get_str(contact_h, _contacts_contact.image_thumbnail_path, &img);

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

	disp |= _contact_phone_changed(c_info, contact_h);

	if (disp)
		_contact_info_on_changed_dispatch(c_info);

	contacts_record_destroy(contact_name_h, true);
	free(img);
	free(l_name);
	free(f_name);
err_contact:
	contacts_record_destroy(contact_h, true);
	return EINA_TRUE;

deleted:
	*deleted = eina_list_append(*deleted, c_info);
	return EINA_TRUE;
}

static void _contact_db_changed(const char *view_uri __UNUSED__, void *data)
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
				contacts_record_h contact_number_h)
{
	unsigned int numberlen = strlen(number);
	Contact_Number *cn = malloc(sizeof(Contact_Number) + numberlen + 1);
	EINA_SAFETY_ON_NULL_RETURN(cn);
	memcpy(cn->number, number, numberlen);
	cn->numberlen = numberlen;
	cn->number[numberlen] = '\0';
	cn->type = _contact_number_type_get(contact_number_h);
	c_info->numbers = eina_inlist_append(c_info->numbers,
						EINA_INLIST_GET(cn));
}

static int _db_query_search_number(const char *number, Contact_Info **c_info)
{
	contacts_error_e err = CONTACTS_ERROR_NONE;
	contacts_list_h list = NULL;
	contacts_query_h query = NULL;
	contacts_filter_h filter = NULL;
	unsigned int contact_count = 0;
	contacts_record_h contact_h = NULL;
	contacts_record_h contact_name_h = NULL;
	contacts_record_h contact_number_h = NULL;
	int contact_id;
	char *f_name = NULL, *l_name = NULL, *img = NULL;
	int idx = 0;

	*c_info = calloc(1, sizeof(Contact_Info));
	EINA_SAFETY_ON_NULL_RETURN_VAL((*c_info), -1);

	err = contacts_query_create(_contacts_number._uri, &query);

	do {
		if (CONTACTS_ERROR_NONE != (err = contacts_filter_create(_contacts_number._uri, &filter)))
			break;
		if (CONTACTS_ERROR_NONE != (err = contacts_filter_add_str(filter, _contacts_number.number, CONTACTS_MATCH_EXACTLY, number)))
			break;
		if (CONTACTS_ERROR_NONE != (err = contacts_query_set_filter(query, filter)))
			break;
		if (CONTACTS_ERROR_NONE != (err = contacts_db_get_records_with_query(query, 0, 0, &list)))
			break;
	} while (0);

	if (CONTACTS_ERROR_NONE != err) {
		ERR("contacts_query_create() Failed(%d)", err);
		return -1;
	}
	contacts_filter_destroy(filter);
	contacts_query_destroy(query);

	contacts_list_get_count(list, &contact_count);
	if (contact_count == 0) {
		free(*c_info);
		*c_info = NULL;
		return 0;
	}

	err = contacts_list_get_current_record_p(list, &contact_number_h);
	if (CONTACTS_ERROR_NONE != err) {
		ERR("contacts_list_get_current_record_p() Failed(%d)", err);
		free(*c_info);
		*c_info = NULL;
		return -1;
	}

	contacts_record_get_int(contact_number_h, _contacts_number.contact_id, &contact_id);
	err = contacts_db_get_record(_contacts_contact._uri, contact_id, &contact_h);
	if (CONTACTS_ERROR_NONE != err) {
		ERR("contacts_db_get_record() Failed(%d)", err);
		free(*c_info);
		*c_info = NULL;
		return -1;
	}

	err = contacts_record_get_child_record_at_p(contact_h, _contacts_contact.name, 0, &contact_name_h);
	if (CONTACTS_ERROR_NONE != err) {
		ERR("contacts_record_get_child_record_at_p() Failed(%d)", err);
		free(*c_info);
		*c_info = NULL;
		return -1;
	}

	contacts_record_get_str(contact_h, _contacts_contact.image_thumbnail_path, &img);
	contacts_record_get_str(contact_name_h, _contacts_name.first, &f_name);
	contacts_record_get_str(contact_name_h, _contacts_name.last, &l_name);

	(*c_info)->id = contact_id;
	(*c_info)->picture = eina_stringshare_add(img);
	(*c_info)->first_name = eina_stringshare_add(f_name);
	(*c_info)->last_name = eina_stringshare_add(l_name);

	idx = 0;
	while (CONTACTS_ERROR_NONE == contacts_record_get_child_record_at_p(contact_h,
																_contacts_contact.number,
																idx++,
																&contact_number_h)) {
		char *number_str;
		contacts_record_get_str(contact_number_h, _contacts_number.number, &number_str);
		_contact_number_add(number_str, (*c_info), contact_number_h);
		free(number_str);
	}

	contacts_record_destroy(contact_h, true);
	free(img);
	free(l_name);
	free(f_name);
	return 1;
}

static const char *_contact_number_type_get(contacts_record_h contact_number_h)
{
	int number_type;

	if (contacts_record_get_int(contact_number_h, _contacts_number.type, &number_type) < 0)
		return NULL;

	if (number_type & CONTACTS_NUMBER_TYPE_OTHER) {
		return "Other";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_HOME) {
		return "Home";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_WORK) {
		return "Work";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_VOICE) {
		return "Home";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_FAX) {
		return "Fax";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_MSG) {
		return "Message";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_CELL) {
		return "Mobile";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_PAGER) {
		return "Pager";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_BBS) {
		return "Bulletin board";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_MODEM) {
		return "Modem";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_CAR) {
		return "Car phone";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_ISDN) {
		return "ISDN";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_VIDEO) {
		return "Video conference";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_PCS) {
		return "Personal communicatior";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_ASSISTANT) {
		return "Assistant telephone";
	}
	else if (number_type & CONTACTS_NUMBER_TYPE_CUSTOM) {
		return "Custom";
	}
	else {
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

	if (_db_query_search_number(number, &c_info) < 0) {
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

#ifdef HAVE_UI_GADGET 
static void _contacts_ug_layout_create(struct ui_gadget *ug,
					enum ug_mode mode __UNUSED__,
					void *priv)
{
	Contacts *contacts = priv;
	EINA_SAFETY_ON_NULL_RETURN(ug);
	elm_object_part_content_set(contacts->self, "elm.swallow.genlist",
					ug_get_layout(ug));
}
#endif

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
#ifdef HAVE_UI_GADGET
	ug_destroy(contacts->ug_all);
#endif
	if (contacts->reconnect)
		ecore_timer_del(contacts->reconnect);
	free(contacts);
	contacts_disconnect2();
}

#ifdef HAVE_UI_GADGET
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
#endif

static Eina_Bool _contacts_reconnect(void *data)
{
	Contacts *contacts = data;

	if (contacts_connect2() != CONTACTS_ERROR_NONE)
		return ECORE_CALLBACK_RENEW;

	contacts->contacts_on = EINA_TRUE;
	contacts->reconnect = NULL;
	contacts_db_add_changed_cb(_contacts_contact._uri, _contact_db_changed, contacts);
#ifdef HAVE_UI_GADGET
	_create_contacts_ug(contacts);
#endif
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

	if (contacts_connect2() != CONTACTS_ERROR_NONE) {
		WRN("Could not connect to the contacts DB");
		contacts->contacts_on = EINA_FALSE;
		contacts->reconnect = ecore_timer_add(1.0, _contacts_reconnect,
							contacts);
	} else {
		contacts_db_add_changed_cb(_contacts_contact._uri, _contact_db_changed, contacts);
		contacts->contacts_on = EINA_TRUE;
#ifdef HAVE_UI_GADGET
		_create_contacts_ug(contacts);
#endif
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
