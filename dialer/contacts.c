#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <Elementary.h>
#include <Eet.h>
#include <Eina.h>

#include "log.h"
#include "gui.h"
#include "ofono.h"
#include "contacts.h"
#include "util.h"

#ifndef EET_COMPRESSION_DEFAULT
#define EET_COMPRESSION_DEFAULT 1
#endif

#define CONTACTS_ENTRY "contacts"

typedef struct _Contacts_List {
	Eina_List *list;
	Eina_Bool dirty;
	Ecore_Poller *save_poller;
} Contacts_List;

typedef struct _Contacts {
	char *path;
	char *bkp;
	Eet_Data_Descriptor *edd;
	Eet_Data_Descriptor *edd_list;
	Elm_Genlist_Item_Class *itc, *group;
	Evas_Object *genlist, *layout, *details;
	Contacts_List *c_list;
} Contacts;

struct _Contact_Info {
	const char *first_name;
	const char *last_name;
	const char *full_name; /* not in edd */
	const char *mobile;
	const char *home;
	const char *work;
	const char *picture;

	Contacts *contacts; /* not in edd */
	Elm_Object_Item *it; /* not in edd */
	Eina_Inlist *on_del_cbs; /* not in edd */
	Ecore_Idler *changed_idler; /* not in edd */
	struct {
		Eina_Inlist *listeners;
		Eina_List *deleted;
		int walking;
	} on_changed_cbs; /* not in edd */
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

Contact_Info *contact_search(Evas_Object *obj, const char *number, const char **type)
{
	Contact_Info *c_info;
	Eina_List *l;
	Contacts *contacts;

	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);
	EINA_SAFETY_ON_NULL_RETURN_VAL(number, NULL);
	contacts = evas_object_data_get(obj, "contacts.ctx");
	EINA_SAFETY_ON_NULL_RETURN_VAL(contacts, NULL);

	EINA_LIST_FOREACH(contacts->c_list->list, l, c_info) {
		if (strcmp(number, c_info->mobile) == 0) {
			if (type)
				*type = "Mobile";
			return c_info;
		} else if (strcmp(number, c_info->work) == 0) {
			if (type)
				*type = "Work";
			return c_info;
		} else if (strcmp(number, c_info->home) == 0) {
			if (type)
				*type = "Home";
			return c_info;
		}
	}

	return NULL;
}

const char *contact_info_full_name_get(const Contact_Info *c)
{
	Contact_Info *c2;

	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);

	if (c->full_name)
		return c->full_name;

	c2 = (Contact_Info *)c;
	c2->full_name = eina_stringshare_printf("%s %s",
						c->first_name, c->last_name);
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

const char *contact_info_picture_get(const Contact_Info *c)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	return c->picture;
}

const char *contact_info_detail_get(const Contact_Info *c, const char *type)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, NULL);
	EINA_SAFETY_ON_NULL_RETURN_VAL(type, NULL);
	if (strcmp(type, "Mobile") == 0)
		return c->mobile;
	else if (strcmp(type, "Work") == 0)
		return c->work;
	else if (strcmp(type, "Home") == 0)
		return c->home;
	else
		return NULL;
}

const char *contact_info_number_check(const Contact_Info *c, const char *number)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, EINA_FALSE);
	EINA_SAFETY_ON_NULL_RETURN_VAL(number, EINA_FALSE);

	if (strcmp(number, c->mobile) == 0)
		return "Mobile";
	else if (strcmp(number, c->work) == 0)
		return "Work";
	else if (strcmp(number, c->home) == 0)
		return "Home";

	return NULL;
}

static Eina_Bool _contacts_save_do(void *data)
{
	Contacts *contacts = data;
	Eet_File *efile;

	contacts->c_list->save_poller = NULL;
	contacts->c_list->dirty = EINA_FALSE;

	ecore_file_unlink(contacts->bkp);
	ecore_file_mv(contacts->path, contacts->bkp);
	efile = eet_open(contacts->path, EET_FILE_MODE_WRITE);
	EINA_SAFETY_ON_NULL_GOTO(efile, failed);
	if (!(eet_data_write(efile,
				contacts->edd_list, CONTACTS_ENTRY,
				contacts->c_list, EET_COMPRESSION_DEFAULT)))
		ERR("Could in the contacts database");

	DBG("wrote %s", contacts->path);
	eet_close(efile);
	return EINA_FALSE;

failed:
	ecore_file_mv(contacts->bkp, contacts->path);
	return EINA_FALSE;
}

static void _contacts_save(Contacts *contacts)
{
	if (contacts->c_list->save_poller)
		return;

	contacts->c_list->save_poller = ecore_poller_add
		(ECORE_POLLER_CORE, 32, _contacts_save_do, contacts);
}

static Eina_Bool _contact_info_changed_idler(void *data)
{
	Contact_Info *c = data;
	Contact_Info_On_Changed_Ctx *ctx;

	c->changed_idler = NULL;

	c->on_changed_cbs.walking++;
	EINA_INLIST_FOREACH(c->on_changed_cbs.listeners, ctx) {
		if (ctx->deleted)
			continue;
		ctx->cb((void *)ctx->data, c);
	}
	c->on_changed_cbs.walking--;

	_contacts_save(c->contacts);

	if (c->on_changed_cbs.walking > 0)
		return EINA_FALSE;

	EINA_LIST_FREE(c->on_changed_cbs.deleted, ctx) {
		c->on_changed_cbs.listeners = eina_inlist_remove(
			c->on_changed_cbs.listeners, EINA_INLIST_GET(ctx));
		free(ctx);
	}

	return EINA_FALSE;
}

static void _contact_info_changed(Contact_Info *c)
{
	if (c->changed_idler)
		return;
	c->changed_idler = ecore_idler_add(_contact_info_changed_idler, c);
}

Eina_Bool contact_info_picture_set(Contact_Info *c, const char *filename)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, EINA_FALSE);
	EINA_SAFETY_ON_NULL_RETURN_VAL(filename, EINA_FALSE);

	DBG("c=%p, was=%s, new=%s", c, c->picture, filename);

	if (eina_stringshare_replace(&(c->picture), filename))
		_contact_info_changed(c);

	return EINA_TRUE;
}

Eina_Bool contact_info_first_name_set(Contact_Info *c, const char *name)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, EINA_FALSE);
	EINA_SAFETY_ON_NULL_RETURN_VAL(name, EINA_FALSE);

	DBG("c=%p, was=%s, new=%s", c, c->first_name, name);

	if (eina_stringshare_replace(&(c->first_name), name)) {
		eina_stringshare_replace(&(c->full_name), NULL);
		_contact_info_changed(c);
	}

	return EINA_TRUE;
}

Eina_Bool contact_info_last_name_set(Contact_Info *c, const char *name)
{
	EINA_SAFETY_ON_NULL_RETURN_VAL(c, EINA_FALSE);
	EINA_SAFETY_ON_NULL_RETURN_VAL(name, EINA_FALSE);

	DBG("c=%p, was=%s, new=%s", c, c->last_name, name);

	if (eina_stringshare_replace(&(c->last_name), name)) {
		eina_stringshare_replace(&(c->full_name), NULL);
		_contact_info_changed(c);
	}

	return EINA_TRUE;
}

Eina_Bool contact_info_detail_set(Contact_Info *c, const char *type, const char *number)
{
	Eina_Bool changed = EINA_FALSE;

	EINA_SAFETY_ON_NULL_RETURN_VAL(c, EINA_FALSE);
	EINA_SAFETY_ON_NULL_RETURN_VAL(type, EINA_FALSE);
	EINA_SAFETY_ON_NULL_RETURN_VAL(number, EINA_FALSE);

	DBG("c=%p, type=%s, number=%s", c, type, number);

	if (strcmp(type, "Mobile") == 0)
		changed = eina_stringshare_replace(&(c->mobile), number);
	else if (strcmp(type, "Work") == 0)
		changed = eina_stringshare_replace(&(c->work), number);
	else if (strcmp(type, "Home") == 0)
		changed = eina_stringshare_replace(&(c->home), number);
	else {
		ERR("Unknown type: %s, number: %s", type, number);
		return EINA_FALSE;
	}

	if (changed)
		_contact_info_changed(c);

	return EINA_TRUE;

}

void contact_info_on_changed_callback_add(Contact_Info *c,
					void (*cb)(void *data, Contact_Info *c),
					const void *data)
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
					const void *data)
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

static void _contact_info_free(Contact_Info *c_info);

void contact_info_del(Contact_Info *c)
{
	Contacts *contacts;

	EINA_SAFETY_ON_NULL_RETURN(c);

	_contact_info_on_del_dispatch(c);

	if (c->it)
		elm_object_item_del(c->it);

	contacts = c->contacts;
	contacts->c_list->list = eina_list_remove(contacts->c_list->list, c);
	contacts->c_list->dirty = EINA_TRUE;
	_contacts_save(contacts);

	_contact_info_free(c);
}

static void _contacts_info_descriptor_init(Eet_Data_Descriptor **edd,
						Eet_Data_Descriptor **edd_list)
{
	Eet_Data_Descriptor_Class eddc;

	EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Contact_Info);
	*edd = eet_data_descriptor_stream_new(&eddc);

	EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Contacts_List);
	*edd_list = eet_data_descriptor_stream_new(&eddc);

	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Contact_Info,
					"picture", picture, EET_T_STRING);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Contact_Info,
					"work", work, EET_T_STRING);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Contact_Info,
					"home", home, EET_T_STRING);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Contact_Info,
					"mobile", mobile, EET_T_STRING);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Contact_Info,
					"first_name", first_name, EET_T_STRING);
	EET_DATA_DESCRIPTOR_ADD_BASIC(*edd, Contact_Info,
					"last_name", last_name, EET_T_STRING);

	EET_DATA_DESCRIPTOR_ADD_LIST(*edd_list, Contacts_List, "list", list,
					*edd);
}

static void _contact_info_free(Contact_Info *c_info)
{
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

	if (c_info->changed_idler)
		ecore_idler_del(c_info->changed_idler);

	eina_stringshare_del(c_info->first_name);
	eina_stringshare_del(c_info->last_name);
	eina_stringshare_del(c_info->full_name);
	eina_stringshare_del(c_info->mobile);
	eina_stringshare_del(c_info->home);
	eina_stringshare_del(c_info->work);
	eina_stringshare_del(c_info->picture);
	free(c_info);
}

static void _on_del(void *data, Evas *e __UNUSED__,
			Evas_Object *obj __UNUSED__, void *event __UNUSED__)
{
	Contacts *contacts = data;
	Contact_Info *c_info;

	if (contacts->c_list->save_poller)
		ecore_poller_del(contacts->c_list->save_poller);
	if (contacts->c_list->dirty)
		_contacts_save_do(contacts);

	eet_data_descriptor_free(contacts->edd);
	eet_data_descriptor_free(contacts->edd_list);
	EINA_LIST_FREE(contacts->c_list->list, c_info) {
		_contact_info_on_del_dispatch(c_info);
		_contact_info_free(c_info);
	}
	free(contacts->c_list);
	elm_genlist_item_class_free(contacts->itc);
	elm_genlist_item_class_free(contacts->group);
	free(contacts->path);
	free(contacts->bkp);
	free(contacts);
	eet_shutdown();
}

static void _on_number_clicked(void *data, Evas_Object *obj __UNUSED__,
				void *event_inf __UNUSED__)
{
	const char *number = data;
	ofono_dial(number, NULL, NULL, NULL);
}

static void _on_item_click(void *data, Evas_Object *obj __UNUSED__,
				void *event_inf)
{
	Contacts *contacts = data;
	Elm_Object_Item *item = event_inf;
	Evas_Object *details, *btn, *photo;
	Contact_Info *c_info;
	char *phone;

	details = contacts->details;
	c_info = elm_object_item_data_get(item);
	elm_genlist_item_selected_set(item, EINA_FALSE);
	elm_layout_box_remove_all(details, "box.phones", EINA_TRUE);

	elm_object_part_text_set(details, "text.name", c_info->first_name);
	elm_object_part_text_set(details, "text.last.name", c_info->last_name);

	photo = picture_icon_get(details, c_info->picture);
	elm_object_part_content_set(details, "swallow.photo", photo);

	btn = elm_button_add(details);
	EINA_SAFETY_ON_NULL_RETURN(btn);
	elm_object_style_set(btn, "contacts");
	phone = phone_format(c_info->mobile);
	elm_object_part_text_set(btn, "elm.text.type", "Mobile");
	elm_object_part_text_set(btn, "elm.text.phone", phone);
	free(phone);
	evas_object_size_hint_weight_set(btn, EVAS_HINT_EXPAND,
						EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(btn);
	evas_object_smart_callback_add(btn, "clicked", _on_number_clicked,
					c_info->mobile);
	elm_layout_box_append(details, "box.phones", btn);

	btn = elm_button_add(details);
	EINA_SAFETY_ON_NULL_RETURN(btn);
	elm_object_style_set(btn, "contacts");
	phone = phone_format(c_info->home);
	elm_object_part_text_set(btn, "elm.text.type", "Home");
	elm_object_part_text_set(btn, "elm.text.phone", phone);
	free(phone);
	evas_object_size_hint_weight_set(btn, EVAS_HINT_EXPAND,
						EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(btn);
	evas_object_smart_callback_add(btn, "clicked", _on_number_clicked,
					c_info->home);
	elm_layout_box_append(details, "box.phones", btn);

	btn = elm_button_add(details);
	EINA_SAFETY_ON_NULL_RETURN(btn);
	elm_object_style_set(btn, "contacts");
	phone = phone_format(c_info->work);
	elm_object_part_text_set(btn, "elm.text.type", "Work");
	elm_object_part_text_set(btn, "elm.text.phone", phone);
	free(phone);
	evas_object_size_hint_weight_set(btn, EVAS_HINT_EXPAND,
						EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(btn);
	evas_object_smart_callback_add(btn, "clicked", _on_number_clicked,
					c_info->work);
	elm_layout_box_append(details, "box.phones", btn);
	elm_object_signal_emit(contacts->layout, "show,details", "gui");
}

static int _sort_by_name_cb(const void *v1, const void *v2)
{
	const Contact_Info *c1, *c2;
	int r;

	c1 = v1;
	c2 = v2;

	if (!c1) return 1;
	if (!c2) return -1;

	r = strcmp(c1->first_name, c2->first_name);
	if (r == 0)
		return strcmp(c1->last_name, c2->last_name);

	return r;
}

static void _contacts_read(Contacts *contacts)
{
	Contact_Info *c_info;
	Eina_List *l;
	Eet_File *efile;
	Elm_Object_Item *it = NULL;
	char group;
	efile = eet_open(contacts->path, EET_FILE_MODE_READ);

	if (efile) {
		contacts->c_list = eet_data_read(efile, contacts->edd_list,
							CONTACTS_ENTRY);
		eet_close(efile);
	}

	if (!contacts->c_list) {
		efile = eet_open(contacts->bkp, EET_FILE_MODE_READ);
		if (efile) {
			contacts->c_list = eet_data_read(
				efile, contacts->edd_list, CONTACTS_ENTRY);
			eet_close(efile);
		}
	}

	if (!contacts->c_list)
		contacts->c_list = calloc(1, sizeof(Contacts_List));

	EINA_SAFETY_ON_NULL_RETURN(contacts->c_list);
	contacts->c_list->list = eina_list_sort(contacts->c_list->list, 0,
						_sort_by_name_cb);
	group = '\0';
	EINA_LIST_FOREACH(contacts->c_list->list, l, c_info) {
		if (!c_info)
			continue;
		if (group != c_info->first_name[0]) {
			group = c_info->first_name[0];
			it = elm_genlist_item_append(contacts->genlist,
							contacts->group,
							c_info, NULL,
							ELM_GENLIST_ITEM_GROUP,
							NULL,
							NULL);
			elm_genlist_item_select_mode_set(it, ELM_OBJECT_SELECT_MODE_DISPLAY_ONLY);
		}
		it = elm_genlist_item_append(contacts->genlist,contacts->itc,
						c_info, it,
						ELM_GENLIST_ITEM_NONE,
						_on_item_click, contacts);
		c_info->it = it;
		c_info->contacts = contacts;
	}
}

static char *_item_label_get(void *data, Evas_Object *obj __UNUSED__,
				const char *part)
{
	Contact_Info *c_info = data;

	if (strncmp(part, "text.contacts.", strlen("text.contacts.")))
		return NULL;

	part += strlen("text.contacts.");

	if (strcmp(part, "name") == 0)
		return strdup(c_info->first_name);
	else if (strcmp(part, "last") == 0)
		return strdup(c_info->last_name);

	ERR("Unexpected part name: %s", part);
	return NULL;
}

static char *_group_label_get(void *data, Evas_Object *obj __UNUSED__,
				const char *part __UNUSED__)
{
	Contact_Info *c_info = data;
	char buf[2];
	snprintf(buf, sizeof(buf), "%c", c_info->first_name[0]);
	return strdup(buf);
}


static Evas_Object *_item_content_get(void *data,
					Evas_Object *obj,
					const char *part)
{
	Contact_Info *c_info = data;
	Evas_Object *photo;

	if (strncmp(part, "swallow.", strlen("swallow.")))
		return NULL;

	part += strlen("swallow.");

	if (strcmp(part, "photo") != 0) {
		ERR("Unexpected part name: %s", part);
		return NULL;
	}

	photo = picture_icon_get(obj, c_info->picture);

	return photo;
}

static void _on_back_clicked(void *data, Evas_Object *obj __UNUSED__,
				const char *emission __UNUSED__,
				const char *source __UNUSED__)
{
	Evas_Object *layout = data;
	elm_object_signal_emit(layout, "hide,details", "gui");
}

Evas_Object *contacts_add(Evas_Object *parent)
{
	int r;
	const char *config_path;
	char base_dir[PATH_MAX], *path;
	Contacts *contacts;
	Evas_Object *obj, *genlist, *details;
	Elm_Genlist_Item_Class *itc, *group;

	eet_init();
	contacts = calloc(1, sizeof(Contacts));
	EINA_SAFETY_ON_NULL_RETURN_VAL(contacts, NULL);


	details = gui_layout_add(parent, "contacts_details");
	EINA_SAFETY_ON_NULL_GOTO(details, err_layout);

	obj = gui_layout_add(parent, "contacts_bg");
	EINA_SAFETY_ON_NULL_GOTO(obj, err_obj);

	genlist = elm_genlist_add(obj);
	EINA_SAFETY_ON_NULL_GOTO(genlist, err_genlist);
	elm_object_style_set(genlist, "contacts");
	elm_genlist_homogeneous_set(genlist, EINA_TRUE);

	itc = elm_genlist_item_class_new();
	EINA_SAFETY_ON_NULL_GOTO(itc, err_genlist);
	itc->item_style = "contacts";
	itc->func.text_get =  _item_label_get;
	itc->func.content_get = _item_content_get;
	itc->func.state_get = NULL;
	itc->func.del = NULL;

	group = elm_genlist_item_class_new();
	EINA_SAFETY_ON_NULL_GOTO(group, err_group);
	group->item_style = "group_contacts";
	group->func.text_get = _group_label_get;
	group->func.content_get = NULL;
	group->func.state_get = NULL;
	group->func.del = NULL;
	contacts->group = group;
	contacts->genlist = genlist;
	contacts->itc = itc;
	contacts->layout = obj;
	contacts->details = details;

	elm_object_part_content_set(obj, "elm.swallow.genlist", genlist);
	elm_object_part_content_set(obj, "elm.swallow.details", details);

	elm_object_signal_callback_add(details, "clicked,back", "gui",
					_on_back_clicked, obj);

	config_path = efreet_config_home_get();
	snprintf(base_dir, sizeof(base_dir), "%s/%s", config_path,
			PACKAGE_NAME);
	ecore_file_mkpath(base_dir);

	r = asprintf(&path,  "%s/%s/contacts.eet", config_path, PACKAGE_NAME);

	if (r < 0)
		goto err_path;
	contacts->path = path;

	r = asprintf(&path,  "%s/%s/contacts.eet.bkp", config_path,
			PACKAGE_NAME);

	if (r < 0)
		goto err_bkp;
	contacts->bkp = path;

	_contacts_info_descriptor_init(&contacts->edd, &contacts->edd_list);
	_contacts_read(contacts);
	EINA_SAFETY_ON_NULL_GOTO(contacts->c_list, err_read);

	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _on_del,
					contacts);

	evas_object_data_set(obj, "contacts.ctx", contacts);
	return obj;

err_read:
	eet_data_descriptor_free(contacts->edd);
	eet_data_descriptor_free(contacts->edd_list);
	free(contacts->bkp);
err_bkp:
	free(contacts->path);
err_path:
	elm_genlist_item_class_free(group);
err_group:
	elm_genlist_item_class_free(itc);
err_genlist:
	free(obj);
err_obj:
	free(details);
err_layout:
	free(contacts);
	eet_shutdown();
	return NULL;
}
