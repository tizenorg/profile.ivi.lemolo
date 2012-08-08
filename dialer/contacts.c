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
} Contacts_List;

typedef struct _Contacts {
	char *path;
	Eet_Data_Descriptor *edd;
	Eet_Data_Descriptor *edd_list;
	Elm_Genlist_Item_Class *itc;
	Evas_Object *genlist, *layout, *details;
	Contacts_List *c_list;
} Contacts;

struct _Contact_Info {
	const char *name;
	const char *mobile;
	const char *home;
	const char *work;
	const char *picture;
};

Contact_Info *contact_search(Evas_Object *obj, const char *number, const char **type)
{
	Contact_Info *c_info;
	Eina_List *l;
	Contacts *contacts;
	char *found_type;

	EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);
	EINA_SAFETY_ON_NULL_RETURN_VAL(number, NULL);
	contacts = evas_object_data_get(obj, "contacts.ctx");
	EINA_SAFETY_ON_NULL_RETURN_VAL(contacts, NULL);

	EINA_LIST_FOREACH(contacts->c_list->list, l, c_info) {
		if (!c_info)
			continue;

		if (strcmp(number, c_info->mobile) == 0) {
			found_type = "Mobile";
			goto found;
		}
		else if (strcmp(number, c_info->work) == 0) {
			found_type = "Work";
			goto found;
		}
		else if (strcmp(number, c_info->home) == 0) {
			found_type = "Home";
			goto found;
		}
	}

	return NULL;
found:
	if (type)
		*type = found_type;
	return c_info;
}

const char *contact_info_name_get(const Contact_Info *c)
{
	return c->name;
}

const char *contact_info_picture_get(const Contact_Info *c)
{
	return c->picture;
}

const char *contact_info_detail_get(const Contact_Info *c, const char *type)
{
	if (strcmp(type, "Mobile") == 0)
		return c->mobile;
	else if (strcmp(type, "Work") == 0)
		return c->work;
	else if (strcmp(type, "Home") == 0)
		return c->home;
	else
		return NULL;
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
					"name", name, EET_T_STRING);

	EET_DATA_DESCRIPTOR_ADD_LIST(*edd_list, Contacts_List, "list", list,
					*edd);
}

static void _contacts_info_free(Contact_Info *c_info)
{
	eina_stringshare_del(c_info->name);
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
	eet_data_descriptor_free(contacts->edd);
	eet_data_descriptor_free(contacts->edd_list);
	EINA_LIST_FREE(contacts->c_list->list, c_info)
		_contacts_info_free(c_info);
	free(contacts->c_list);
	elm_genlist_item_class_free(contacts->itc);
	free(contacts->path);
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
	Evas_Object *details, *btn;
	char buf[1024];
	Contact_Info *c_info;
	char *phone;

	details = contacts->details;
	c_info = elm_object_item_data_get(item);
	elm_genlist_item_selected_set(item, EINA_FALSE);
	elm_layout_box_remove_all(details, "box.phones", EINA_TRUE);

	/* TODO SET IMAGE AS WELL */
	elm_object_part_text_set(details, "text.name", c_info->name);

	btn = elm_button_add(details);
	EINA_SAFETY_ON_NULL_RETURN(btn);
	phone = phone_format(c_info->mobile);
	snprintf(buf, sizeof(buf), "Mobile: %s", phone);
	free(phone);
	elm_object_part_text_set(btn, NULL, buf);
	evas_object_size_hint_weight_set(btn, EVAS_HINT_EXPAND,
						EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(btn);
	evas_object_smart_callback_add(btn, "clicked", _on_number_clicked,
					c_info->mobile);
	elm_layout_box_append(details, "box.phones", btn);

	btn = elm_button_add(details);
	EINA_SAFETY_ON_NULL_RETURN(btn);
	phone = phone_format(c_info->home);
	snprintf(buf, sizeof(buf), "Home: %s", phone);
	free(phone);
	elm_object_part_text_set(btn, NULL, buf);
	evas_object_size_hint_weight_set(btn, EVAS_HINT_EXPAND,
						EVAS_HINT_EXPAND);
	evas_object_size_hint_align_set(btn, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_show(btn);
	evas_object_smart_callback_add(btn, "clicked", _on_number_clicked,
					c_info->home);
	elm_layout_box_append(details, "box.phones", btn);

	btn = elm_button_add(details);
	EINA_SAFETY_ON_NULL_RETURN(btn);
	phone = phone_format(c_info->work);
	snprintf(buf, sizeof(buf), "Work: %s", phone);
	free(phone);
	elm_object_part_text_set(btn, NULL, buf);
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
	c1 = v1;
	c2 = v2;

	if (!c1) return 1;
	if (!c2) return -1;

	return strcmp(c1->name, c2->name);
}

static void _contacts_read(Contacts *contacts)
{
	Contact_Info *c_info;
	Eina_List *l;
	Eet_File *efile;

	efile = eet_open(contacts->path, EET_FILE_MODE_READ);

	if (efile) {
		contacts->c_list = eet_data_read(efile, contacts->edd_list,
							CONTACTS_ENTRY);
		eet_close(efile);
	} else
		contacts->c_list = calloc(1, sizeof(Contacts_List));

	EINA_SAFETY_ON_NULL_RETURN(contacts->c_list);
	contacts->c_list->list = eina_list_sort(contacts->c_list->list, 0,
						_sort_by_name_cb);

	EINA_LIST_FOREACH(contacts->c_list->list, l, c_info) {
		if (!c_info)
			continue;
		elm_genlist_item_append(contacts->genlist,contacts->itc, c_info,
					NULL,ELM_GENLIST_ITEM_NONE,
					_on_item_click, contacts);
	}
}

static char *_item_label_get(void *data, Evas_Object *obj __UNUSED__,
				const char *part)
{
	Contact_Info *c_info = data;

	if (strncmp(part, "text.contacts", strlen("text.contacts")))
		return NULL;

	part += strlen("text.contacts.");

	if (strcmp(part, "name") == 0)
		return strdup(c_info->name);

	ERR("Unexpected part name: %s", part);
	return NULL;
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
	Elm_Genlist_Item_Class *itc;

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

	itc = elm_genlist_item_class_new();
	EINA_SAFETY_ON_NULL_GOTO(itc, err_genlist);
	itc->item_style = "contacts";
	itc->func.text_get =  _item_label_get;
	itc->func.content_get = NULL;
	itc->func.state_get = NULL;
	itc->func.del = NULL;
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

	_contacts_info_descriptor_init(&contacts->edd, &contacts->edd_list);
	_contacts_read(contacts);
	EINA_SAFETY_ON_NULL_GOTO(contacts->c_list, err_read);

	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _on_del,
					contacts);

	evas_object_data_set(obj, "contacts.ctx", contacts);
	return obj;

err_read:
	free(contacts->path);
	eet_data_descriptor_free(contacts->edd);
	eet_data_descriptor_free(contacts->edd_list);
err_path:
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
