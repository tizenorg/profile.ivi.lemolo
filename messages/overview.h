#ifndef _EFL_OFONO_OVERVIEW_H__
#define _EFL_OFONO_OVERVIEW_H__ 1

#include "ofono.h"

typedef struct _Message Message;

Evas_Object *overview_add(Evas_Object *parent);
long long message_time_get(const Message *msg);
const char * message_content_get(const Message *msg);
const char * message_phone_get(const Message *msg);
unsigned char message_outgoing_get(const Message *msg);
unsigned char message_state_get(const Message *msg);
void message_state_set(Message *msg, unsigned char state);
void message_ref(Message *msg);

void overview_message_from_file_delete(Evas_Object *obj, Message *msg, const char *contact);

void message_del(Message *msg);

void message_data_set(Message *msg, void *data);
void *message_data_get(const Message *msg);

Elm_Object_Item *message_object_item_get(const Message *msg);
void message_object_item_set(Message *msg, Elm_Object_Item *it);

void overview_genlist_update(Evas_Object *obj, Message *msg,const char *contact);
void overview_all_contact_messages_clear(Evas_Object *obj, const char *contact);

Message *message_new(time_t timestamp, const char *content,
			Eina_Bool outgoing, OFono_Sent_SMS_State state);
#endif
